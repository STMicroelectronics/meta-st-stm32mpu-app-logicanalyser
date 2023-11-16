/*
* logic_analyser_backend.c
* Implements the logic analyser backend to intereact with the GUI.
*
* Copyright (C) 2019, STMicroelectronics - All Rights Reserved
* Author: Michel Catrouillet michel.catrouillet@st.com for STMicroelectronics.
*
* License type: GPLv2
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License version 2 as published by
* the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with
* this program. If not, see
* http://www.gnu.org/licenses/.
*/
 
#define _GNU_SOURCE             /* To get DN_* constants from <fcntl.h> */
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <inttypes.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h> // for usleep
#include <math.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <regex.h>
#include <sched.h>
#include <assert.h>
#include <errno.h>
#include <error.h>
#include <gtk/gtk.h>
 
#define SAMP_SRAM_PACKET_SIZE (256*2)
#define SAMP_DDR_BUFFER_SIZE (1024*1024)

#define TTY_CTRL_OPTS (CS8 | CLOCAL | CREAD)
#define TTY_INPUT_OPTS IGNPAR
#define TTY_OUTPUT_OPTS 0
#define TTY_LOCAL_OPTS 0

#define DATA_BUF_POOL_SIZE 1024*1024 /* 1MB */
#define MAX_BUF 80
 
#define PORT 8888
#define GET             0
#define POST            1
#define POSTBUFFERSIZE  512
 
#define DMA_DDR_BUFF 1
#define PHYS_RESERVED_REGION_ADDR 0xdb000000
#define PHYS_RESERVED_REGION_SIZE 0x1000000
 
#define RPMSG_SDB_IOCTL_SET_EFD _IOW('R', 0x00, struct rpmsg_sdb_ioctl_set_efd *)
#define RPMSG_SDB_IOCTL_GET_DATA_SIZE _IOWR('R', 0x01, struct rpmsg_sdb_ioctl_get_data_size *)
 
#define TIMEOUT 60
#define NB_BUF 10
 
typedef struct
{
    int bufferId, eventfd;
} rpmsg_sdb_ioctl_set_efd;
 
typedef struct
{
    int bufferId;
    uint32_t size;
} rpmsg_sdb_ioctl_get_data_size;
 
struct connection_info_struct
{
  int connectiontype;
  char *answerstring;
  struct MHD_PostProcessor *postprocessor;
};

struct MHD_Daemon *mHttpDaemon;

char FIRM_NAME[50];
struct timeval tval_before, tval_after, tval_result;

typedef enum {
  STATE_READY = 0,
  STATE_SAMPLING_LOW,
  STATE_SAMPLING_HIGH,
} machine_state_t;

static char machine_state_str[5][13] = {"READY", "SAMPLING_LOW", "SAMPLING_HIGH"};
static char SELECTED[10] = {" selected"};
static char NOT_SELECTED[10] = {""};
static char freq_unit_str[3][4] = {"MHz", "kHz", "Hz"};
static char FREQU[3] = {'M', 'k', 'H'};

/* The file descriptor used to manage our TTY over RPMSG */
static int mFdRpmsg[2] = {-1, -1};

/* The file descriptor used to manage our SDB over RPMSG */
static int mFdSdbRpmsg = -1;

static int virtual_tty_send_command(int len, char* commandStr);

static char mByteBuffer[512];
static char mByteBuffCpy[512];
static int mNbReadTty = 0;

static char mRxTraceBuffer[512];

static char mSamplingStr[15];
static int32_t mSampFreq_Hz = 4;
static machine_state_t mMachineState;
static int32_t mSampParmCount;
static uint8_t mExitRequested = 0, mErrorDetected = 0;
static uint32_t mNbUncompData=0, mNbWrittenInFileData;
static uint32_t mNbUncompMB=0, mNbPrevUncompMB=0, mNbTty0Frame=0;
static uint8_t mDdrBuffAwaited;
static uint8_t mThreadCancel = 0;

void* mmappedData[NB_BUF];
static    int fMappedData = 0;
FILE *pOutFile = NULL;
static char mFileNameStr[150];
static pthread_t threadTTY, threadSDB, threadUI;

static int efd[NB_BUF];
static struct pollfd fds[NB_BUF];

static    GtkWidget *window;
static    GtkWidget *f_scale;
static    GtkAdjustment *fadjustment;

static    GtkWidget *controlTitle_label;
static    GtkWidget *fTitle_label;
static    GtkWidget *fValue_label;
static    GtkWidget *measurTitle_label;
static    GtkWidget *state_label;
static    GtkWidget *state_value;
static    GtkWidget *nbCompData_label;
static    GtkWidget *nbCompData_value;
static    GtkWidget *nbRealData_label;
static    GtkWidget *nbRealData_value;
static    GtkWidget *nbRpmsgFrame_label    ;
static    GtkWidget *nbRpmsgFrame_value;
static    GtkWidget *data_label;
static    GtkWidget *data_value;
static    GtkWidget *butSingle;
static    GtkWidget *notchSetdata;

/********************************************************************************
Copro functions allowing to manage a virtual TTY over RPMSG
*********************************************************************************/
int copro_isFwRunning(void)
{
    int fd;
    size_t byte_read;
    int result = 0;
    unsigned char bufRead[MAX_BUF];
    char *user  = getenv("USER");
    if (user && strncmp(user, "root", 4)) {
        system("XTERM=xterm su root -c 'cat /sys/class/remoteproc/remoteproc0/state' > /tmp/remoteproc0_state");
        fd = open("/tmp/remoteproc0_state", O_RDONLY);
    } else {
        fd = open("/sys/class/remoteproc/remoteproc0/state", O_RDONLY);
    }
    if (fd < 0) {
        printf("CA7 : Error opening remoteproc0/state, err=-%d\n", errno);
        return (errno * -1);
    }
    byte_read = (size_t) read (fd, bufRead, MAX_BUF);
    if (byte_read >= strlen("running")) {
        char* pos = strstr((char*)bufRead, "running");
        if(pos) {
            result = 1;
        }
    }
    close(fd);
    return result;
}

int copro_stopFw(void)
{
    int fd;
    char *user  = getenv("USER");
    if (user && strncmp(user, "root",4)) {
        system("su root -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'");
        return 0;
    }
    fd = open("/sys/class/remoteproc/remoteproc0/state", O_RDWR);
    if (fd < 0) {
        printf("CA7 : Error opening remoteproc0/state, err=-%d\n", errno);
        return (errno * -1);
    }
    write(fd, "stop", strlen("stop"));
    close(fd);
    return 0;
}
 
int copro_startFw(void)
{
    int fd;
    char *user  = getenv("USER");
    if (user && strncmp(user, "root",4)) {
        system("su root -c 'echo start > /sys/class/remoteproc/remoteproc0/state'");
        return 0;
    }
    fd = open("/sys/class/remoteproc/remoteproc0/state", O_RDWR);
    if (fd < 0) {
        printf("CA7 : Error opening remoteproc0/state, err=-%d\n", errno);
        return (errno * -1);
    }
    write(fd, "start", strlen("start"));
    close(fd);
    return 0;
}
 
int copro_getFwPath(char* pathStr)
{
    int fd;
    int byte_read;
    char *user  = getenv("USER");
    if (user && strncmp(user, "root",4)) {
        system("XTERM=xterm su root -c 'cat /sys/module/firmware_class/parameters/path' > /tmp/parameters_path");
        fd = open("/tmp/parameters_path", O_RDONLY);
    } else {
        fd = open("/sys/module/firmware_class/parameters/path", O_RDONLY);
    }
    if (fd < 0) {
        printf("CA7 : Error opening firmware_class/parameters/path, err=-%d\n", errno);
        return (errno * -1);
    }
    byte_read = read (fd, pathStr, MAX_BUF);
    close(fd);
    return byte_read;
}

int copro_setFwPath(char* pathStr)
{
    int fd;
    int result = 0;
    char *user  = getenv("USER");
    if (user && strncmp(user, "root",4)) {
        char cmd[1024];
        snprintf(cmd, 1024, "su root -c 'echo %s > /sys/module/firmware_class/parameters/path'", pathStr);
        system(cmd);
        return strlen(pathStr);
    }
    fd = open("/sys/module/firmware_class/parameters/path", O_RDWR);
    if (fd < 0) {
        printf("CA7 : Error opening firmware_class/parameters/path, err=-%d\n", errno);
        return (errno * -1);
    }
    result = write(fd, pathStr, strlen(pathStr));
    close(fd);
    return result;
}
 
int copro_getFwName(char* pathStr)
{
    int fd;
    int byte_read;
    char *user  = getenv("USER");
    if (user && strncmp(user, "root",4)) {
        system("XTERM=xterm su root -c 'cat /sys/class/remoteproc/remoteproc0/firmware' > /tmp/remoteproc0_firmware");
        fd = open("/tmp/remoteproc0_firmware", O_RDONLY);
    } else {
        fd = open("/sys/class/remoteproc/remoteproc0/firmware", O_RDWR);
    }
    if (fd < 0) {
        printf("CA7 : Error opening remoteproc0/firmware, err=-%d\n", errno);
        return (errno * -1);
    }
    byte_read = read (fd, pathStr, MAX_BUF);
    close(fd);
    return byte_read;
}
 
int copro_setFwName(char* nameStr)
{
    int fd;
    int result = 0;
    char *user  = getenv("USER");
    if (user && strncmp(user, "root",4)) {
        char cmd[1024];
        snprintf(cmd, 1024, "su root -c 'echo %s > /sys/class/remoteproc/remoteproc0/firmware'", nameStr);
        system(cmd);
        return strlen(nameStr);
    }
    fd = open("/sys/class/remoteproc/remoteproc0/firmware", O_RDWR);
    if (fd < 0) {
        printf("CA7 : Error opening remoteproc0/firmware, err=-%d\n", errno);
        return (errno * -1);
    }
    result = write(fd, nameStr, strlen(nameStr));
    close(fd);
    return result;
}
 
int copro_openTtyRpmsg(int ttyNb, int modeRaw)
{
    struct termios tiorpmsg;
    char devName[50];
    sprintf(devName, "/dev/ttyRPMSG%d", ttyNb%2);
    mFdRpmsg[ttyNb%2] = open(devName, O_RDWR |  O_NOCTTY | O_NONBLOCK);
    if (mFdRpmsg[ttyNb%2] < 0) {
        printf("CA7 : Error opening ttyRPMSG%d, err=-%d\n", ttyNb%2, errno);
        return (errno * -1);
    }
#if 1
    /* get current port settings */
    tcgetattr(mFdRpmsg[ttyNb%2],&tiorpmsg);
    if (modeRaw) {
#if 0
        tiorpmsg.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                      | INLCR | IGNCR | ICRNL | IXON);
        tiorpmsg.c_oflag &= ~OPOST;
        tiorpmsg.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        tiorpmsg.c_cflag &= ~(CSIZE | PARENB);
        tiorpmsg.c_cflag |= CS8;
#else
        memset(&tiorpmsg, 0, sizeof(tiorpmsg));
        tiorpmsg.c_cflag = TTY_CTRL_OPTS;
        tiorpmsg.c_iflag = TTY_INPUT_OPTS;
        tiorpmsg.c_oflag = TTY_OUTPUT_OPTS;
        tiorpmsg.c_lflag = TTY_LOCAL_OPTS;
        tiorpmsg.c_cc[VTIME] = 0;
        tiorpmsg.c_cc[VMIN] = 1;
        cfmakeraw(&tiorpmsg);
#endif
    } else {
        /* ECHO off, other bits unchanged */
        tiorpmsg.c_lflag &= ~ECHO;
        /*do not convert LF to CR LF */
        tiorpmsg.c_oflag &= ~ONLCR;
    }
    if (tcsetattr(mFdRpmsg[ttyNb%2], TCSANOW, &tiorpmsg) < 0) {
        printf("Error %d in copro_openTtyRpmsg(%d) tcsetattr", errno, ttyNb);
        return (errno * -1);
    }
#endif
    return 0;
}
 
int copro_closeTtyRpmsg(int ttyNb)
{
    close(mFdRpmsg[ttyNb%2]);
    mFdRpmsg[ttyNb%2] = -1;
    return 0;
}
 
int copro_writeTtyRpmsg(int ttyNb, int len, char* pData)
{
    int result = 0;
    if (mFdRpmsg[ttyNb%2] < 0) {
        printf("CA7 : Error writing ttyRPMSG%d, fileDescriptor is not set\n", ttyNb%2);
        return mFdRpmsg[ttyNb%2];
    }
 
    result = write(mFdRpmsg[ttyNb%2], pData, len);
    return result;
}
 
int copro_readTtyRpmsg(int ttyNb, int len, char* pData)
{
    int byte_rd, byte_avail;
    int result = 0;
    if (mFdRpmsg[ttyNb%2] < 0) {
        printf("CA7 : Error reading ttyRPMSG%d, fileDescriptor is not set\n", ttyNb%2);
        return mFdRpmsg[ttyNb%2];
    }
    ioctl(mFdRpmsg[ttyNb%2], FIONREAD, &byte_avail);
    if (byte_avail > 0) {
        if (byte_avail >= len) {
            byte_rd = read (mFdRpmsg[ttyNb%2], pData, len);
        } else {
            byte_rd = read (mFdRpmsg[ttyNb%2], pData, byte_avail);
        }
        //printf("CA7 : read successfully %d bytes to %p, [0]=0x%x\n", byte_rd, pData, pData[0]);
        result = byte_rd;
    } else {
        result = 0;
    }
    return result;
}
/********************************************************************************
End of Copro functions
*********************************************************************************/

void
print_time() {
    struct timespec ts;
    clock_gettime(1, &ts);
    printf("Time: %lld\n", (ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL));
}
 
/********************************************************************************
GTK UI functions
*********************************************************************************/
static void
open_raw_file(void) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(mFileNameStr, "/usr/local/demo/la/%04d%02d%02d-%02d%02d%02d.dat",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    pOutFile = fopen(mFileNameStr,"wb");
}
 
static int32_t
write_raw_file(unsigned char* pData, unsigned int size) {
    // perform the write by packets of 4kB
    int rest2write = size;
    int index = 0;
    int size2write = 4096;
    int writtenSize = 0;
    do {
        if (rest2write < 4096) {
            size2write = rest2write;
        }
        writtenSize += fwrite(pData+index, 1, size2write, pOutFile);
        rest2write -= size2write;
        index += size2write;
    } while (rest2write > 0);
    return writtenSize;
}
 
static void
close_raw_file(void) {
    if (pOutFile != NULL) {
        fclose(pOutFile);
        pOutFile = NULL;
    }
}
 
static gboolean refreshUI_CB (gpointer data)
{
    char tmpStr[200];
 
    if (mMachineState >= STATE_SAMPLING_LOW) {
        gtk_button_set_label (GTK_BUTTON (butSingle), "Stop");
    } else {
        gtk_button_set_label (GTK_BUTTON (butSingle), "Start");
    }
    gtk_label_set_text (GTK_LABEL (state_value), machine_state_str[mMachineState]);
    sprintf(tmpStr, "%uMB : %u", mNbUncompMB, mNbUncompData);
    gtk_label_set_text (GTK_LABEL (nbRealData_value), tmpStr);
    sprintf(tmpStr, "%u", mNbTty0Frame);
    gtk_label_set_text (GTK_LABEL (nbRpmsgFrame_value), tmpStr);
    //gtk_label_set_text (GTK_LABEL (fileName_value), mFileNameStr);
    sprintf(tmpStr, "%x", mByteBuffCpy[0]);
    gtk_label_set_text (GTK_LABEL (data_value), tmpStr);
   
    gtk_widget_show_all(window);
 
   return FALSE;
}
 
static void single_clicked (GtkWidget *widget, gpointer data)
{
    char setData = 'n';
    if (mMachineState == STATE_READY) {
        if (mSampFreq_Hz > 2) {
            mMachineState = STATE_SAMPLING_HIGH;
        } else {
            mMachineState = STATE_SAMPLING_LOW;
        }
        mNbUncompData=0;
        mNbPrevUncompMB = 0;
        mNbUncompMB = 0;
        mNbWrittenInFileData=0;
        mDdrBuffAwaited=0;
        mNbTty0Frame=0;
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (notchSetdata))) {
            setData = 'y';
        }
        // build sampling string
        sprintf(mSamplingStr, "S%03dMs%c", mSampFreq_Hz, setData);
        printf("CA7 : Start sampling at %dMHz\n", mSampFreq_Hz);
        virtual_tty_send_command(strlen(mSamplingStr), mSamplingStr);
        gdk_threads_add_idle (refreshUI_CB, window);
    } else if (mMachineState >= STATE_SAMPLING_LOW) {
        mMachineState = STATE_READY;
        printf("CA7 : Stop sampling\n");
        virtual_tty_send_command(strlen("Exit"), "Exit");
        gdk_threads_add_idle (refreshUI_CB, window);
    } else {
        printf("CA7 : Start sampling param error: mMachineState=%d mSampFreq_Hz=%d \n",
            mMachineState, mSampFreq_Hz);
    }
}
 
static void f_scale_moved (GtkRange *range, gpointer user_data)
{
   GtkWidget *label = user_data;
 
   gdouble pos = gtk_range_get_value (range);
   gdouble val = 1 + 11 * pos / 100;
   gchar *str = g_strdup_printf ("%.0f", val);
   mSampFreq_Hz = atoi(str);
   gtk_label_set_text (GTK_LABEL (label), str);
   printf("CA7 : fscale = %d\n", mSampFreq_Hz);
 
   g_free(str);
}
 
void *ui_thread(void *arg)
{
   
    GtkWidget *mainGrid;
    char tmpStr[100];
   
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title (GTK_WINDOW (window), "STHowToM4ToA7LargeDataExchange");
    g_signal_connect (window, "destroy",
                  G_CALLBACK (gtk_main_quit), NULL);
    gtk_container_set_border_width (GTK_CONTAINER (window), 10);
    gtk_window_fullscreen(GTK_WINDOW (window));
   
    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_path (provider, "/usr/local/demo/la/bin/la.css", NULL);
   
    GdkDisplay* Display = gdk_display_get_default();
    GdkScreen* Screen = gdk_display_get_default_screen(Display);
    gtk_style_context_add_provider_for_screen(Screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
 
    controlTitle_label = gtk_label_new ("Control");
    gtk_widget_set_name(controlTitle_label, "title1");
   
    fTitle_label = gtk_label_new ("Sampling freq. (MHz) :");
    gtk_label_set_xalign (GTK_LABEL (fTitle_label), 0);
    gtk_widget_set_name(fTitle_label, "header");

    fValue_label = gtk_label_new ("4");
    gtk_label_set_xalign (GTK_LABEL (fValue_label), 0);
    gtk_widget_set_name(fValue_label, "value");
    
    fadjustment = gtk_adjustment_new (16, 0, 100, 5, 10, 0);
    f_scale = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, fadjustment);
    gtk_scale_set_draw_value (GTK_SCALE (f_scale), FALSE);
    g_object_set (GTK_SCALE (f_scale), "expand", TRUE, NULL);
 
    g_signal_connect (f_scale,
                    "value-changed",
                    G_CALLBACK (f_scale_moved),
                    fValue_label);
   
    measurTitle_label = gtk_label_new ("Measurements");
    gtk_widget_set_name(measurTitle_label, "title1");
 
    state_label = gtk_label_new ("Machine state :");
    gtk_label_set_xalign (GTK_LABEL (state_label), 0);
    gtk_widget_set_name(state_label, "header");
    state_value = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (state_value), 0);
    gtk_widget_set_name(state_value, "value");
   
    nbRealData_label = gtk_label_new ("Nb of received data :");
    gtk_label_set_xalign (GTK_LABEL (nbRealData_label), 0);
    gtk_widget_set_name(nbRealData_label, "header");
    nbRealData_value = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (nbRealData_value), 0);
    gtk_widget_set_name(nbRealData_value, "value");

    nbRpmsgFrame_label     = gtk_label_new ("Nb of RPMSG data frame :");
    gtk_label_set_xalign (GTK_LABEL (nbRpmsgFrame_label    ), 0);
    gtk_widget_set_name(nbRpmsgFrame_label, "header");

    nbRpmsgFrame_value = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (nbRpmsgFrame_value), 0);
    gtk_widget_set_name(nbRpmsgFrame_value, "value");

    data_label = gtk_label_new ("Data :");
    gtk_label_set_xalign (GTK_LABEL (data_label), 0);
    gtk_widget_set_name(data_label, "header");

    data_value = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (data_value), 0);
    gtk_widget_set_name(data_value, "value");
   
    gtk_label_set_text (GTK_LABEL (state_value), machine_state_str[mMachineState]);
    sprintf(tmpStr, "%u", mNbUncompData);
    gtk_label_set_text (GTK_LABEL (nbCompData_value), tmpStr);
    sprintf(tmpStr, "%u", mNbUncompData);
    gtk_label_set_text (GTK_LABEL (nbRealData_value), tmpStr);
    sprintf(tmpStr, "%u", mNbWrittenInFileData);
    gtk_label_set_text (GTK_LABEL (nbRpmsgFrame_value), tmpStr);
   
    butSingle = gtk_button_new_with_label("Start");
    g_signal_connect(butSingle,
                    "clicked",
                    G_CALLBACK (single_clicked),
                    NULL);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(butSingle)), "circular");
    gtk_widget_set_name(butSingle, "mybutt");

    notchSetdata = gtk_check_button_new_with_label("Set DATA");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(notchSetdata), FALSE);

                   
    mainGrid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (mainGrid), 5);
    gtk_grid_set_column_spacing (GTK_GRID (mainGrid), 20);
    // Control title in (0,0) is 3 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), controlTitle_label, 0, 0, 3, 1);
    // Freq. title in (0,1) is 1 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), fTitle_label, 0, 1, 1, 1);
    // Freq. value in (1,1) is 1 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), fValue_label, 1, 1, 1, 1);
    // Freq. scale in (2,1) is 1 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), f_scale, 2, 1, 1, 1);
   
    // Start button in (0,2) is 2 column large & 2 row high
    gtk_grid_attach (GTK_GRID (mainGrid), butSingle, 0, 2, 2, 2);
   
    // SetDATA notch in (3,2) is 2 column large & 2 row high
    gtk_grid_attach (GTK_GRID (mainGrid), notchSetdata, 2, 2, 2, 1);
   
    // Measurement title in (0,4) is 3 columns large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), measurTitle_label, 0, 4, 3, 1);
    // State label in (0,5) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), state_label, 0, 5, 2, 1);
    // State value in (2,5) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), state_value, 2, 5, 2, 1);

    // Real data label in (0,7) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbRealData_label, 0, 6, 2, 1);
    // Real data value in (2,7) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbRealData_value, 2, 6, 2, 1);

    // Nb of RPMSG frame label in (0,8) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbRpmsgFrame_label, 0, 7, 2, 1);
    // Nb of RPMSG frame value in (2,8) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbRpmsgFrame_value, 2, 7, 2, 1);

   
    // Data label in (0,9) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), data_label, 0, 8, 2, 1);
    // File name value in (2,9) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), data_value, 2, 8, 2, 1);

    gtk_grid_set_row_homogeneous (GTK_GRID (mainGrid), TRUE);
   
    gtk_container_add (GTK_CONTAINER (window), mainGrid);
 
    gtk_widget_show_all(window);
   
 
    gtk_main ();
    return 0;
}
  
/*************************************************************************************
End of GTK UI functions
*************************************************************************************/
 
static void sleep_ms(int milliseconds)
{
    usleep(milliseconds * 1000);
}
 
static int virtual_tty_send_command(int len, char* commandStr) {
 
    struct timespec ts;
    clock_gettime(1, &ts);
    printf("CA7 [%lld] : virtual_tty_send_command len=%d => %s\n",
        (ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL), len, commandStr);
    return copro_writeTtyRpmsg(0, len, commandStr);
}
 
size_t getFilesize(const char* filename) {
    struct stat st;
    stat(filename, &st);
    return st.st_size;
}
 
void incatchr(int signum){
      printf("%s!\n",__func__);
}
 
void exit_fct(int signum)
{
    gtk_main_quit();
    mThreadCancel = 1;
    sleep_ms(100);
    if (fMappedData) {
        for (int i=0;i<NB_BUF;i++){
            int rc = munmap(mmappedData[i], DATA_BUF_POOL_SIZE);
            assert(rc == 0);
        }
        fMappedData = 0;
        printf("CA7 : Buffers successfully unmapped\n");
    }
 
    if (copro_isFwRunning()) {
        mExitRequested = 1;
        //while (mExitRequested);
        close(mFdSdbRpmsg);
        mFdSdbRpmsg = -1;
        copro_closeTtyRpmsg(0);
        copro_closeTtyRpmsg(1);
        copro_stopFw();
        printf("CA7 : stop the firmware before exit\n");
    }
#if 0
    if (pOutFile != NULL) {
        printf("CA7 : closing file before exit\n");
        close_raw_file();
    }
#endif
    exit(signum);
}
 
void *virtual_tty_thread(void *arg)
{
    int read0, read1;
    int32_t wsize;
    int nb2copy = 0;
    char cmdmsg[20];

    // open tty0
    if (copro_openTtyRpmsg(0, 1)) {
        printf("CA7 : fails to open the ttyRPMSG0\n");
        return (errno * -1);
    }
    //system("stty -F /dev/ttyRPMGS0 -isig");

    // needed to allow M4 to send any data over virtualTTY
    copro_writeTtyRpmsg(0, 1, "r");
 
    // open tty1
    if (copro_openTtyRpmsg(1, 1)) {
        printf("CA7 : fails to open the ttyRPMSG0\n");
        return (errno * -1);
    }
    // needed to allow M4 to send any data over virtualTTY
    copro_writeTtyRpmsg(1, 1, "r");

    usleep(500000);
    sprintf(cmdmsg, "B%02d", NB_BUF);
    copro_writeTtyRpmsg(0, strlen(cmdmsg), cmdmsg);
 
    while (1) {
        if (mThreadCancel) break;    // kill thread requested

        // tty0 is used for low rate compressed data transfer (less or equal to 2MHz sampling)
        read0 = copro_readTtyRpmsg(0, SAMP_SRAM_PACKET_SIZE, mByteBuffer);
        if (read0 > 0) {
            mNbTty0Frame++;
            mNbUncompData += read0;

            mNbUncompMB = mNbUncompData / 1024 / 1024;
            if (mNbUncompMB != mNbPrevUncompMB) {
                // a new MB has been received, update display
                mNbPrevUncompMB = mNbUncompMB;
                mByteBuffCpy[0] = mByteBuffer[0];
                gdk_threads_add_idle (refreshUI_CB, window);
            }
        }

        // tty1 is dedicated to trace of M4
        read1 = copro_readTtyRpmsg(1, 512, mRxTraceBuffer);
        mRxTraceBuffer[read1] = 0;  // to be sure to get a end of string
        if (read1 > 0) {
            if (strcmp(mRxTraceBuffer, "CM4 : DMA TransferError") == 0) {
                // sampling is aborted, refresh the UI
                mErrorDetected = 1;
                //mMachineState = STATE_READY;
                //gdk_threads_add_idle (refreshUI_CB, window);
            }
            gettimeofday(&tval_after, NULL);
            timersub(&tval_after, &tval_before, &tval_result);
            if (mRxTraceBuffer[0] == 'C') {
                printf("[%ld.%06ld] : %s\n",
                    (long int)tval_result.tv_sec, (long int)tval_result.tv_usec, 
                    mRxTraceBuffer);
            } else {
                printf("[%ld.%06ld] : CA7 : tty1 got %d [%x] bytes\n",
                    (long int)tval_result.tv_sec, (long int)tval_result.tv_usec, 
                    read1, mRxTraceBuffer[0]);
            }
        }
        //usleep(500);

        //sleep_ms(1);      // give time to UI
    }
    return 0;
}
 
void *sdb_thread(void *arg)
{
    int ret, rc, i, n;
    int buffIdx = 0;
    char buf[16];
    char dbgmsg[80];
    rpmsg_sdb_ioctl_get_data_size q_get_data_size;
    char *filename = "/dev/rpmsg-sdb";
    rpmsg_sdb_ioctl_set_efd q_set_efd;
 
    mFdSdbRpmsg = open(filename, O_RDWR);
    assert(mFdSdbRpmsg != -1);
    for (i=0;i<NB_BUF;i++){
        // Create the evenfd, and sent it to kernel driver, for notification of buffer full
        efd[i] = eventfd(0, 0);
        if (efd[i] == -1)
            error(EXIT_FAILURE, errno,
                "failed to get eventfd");
        printf("\nCA7 : Forward efd info for buf%d with mFdSdbRpmsg:%d and efd:%d\n",i,mFdSdbRpmsg,efd[i]);
        q_set_efd.bufferId = i;
        q_set_efd.eventfd = efd[i];
        if(ioctl(mFdSdbRpmsg, RPMSG_SDB_IOCTL_SET_EFD, &q_set_efd) < 0)
            error(EXIT_FAILURE, errno,
                "failed to set efd");
        // watch eventfd for input
        fds[i].fd = efd[i];
        fds[i].events = POLLIN;
        mmappedData[i] = mmap(NULL,
                                DATA_BUF_POOL_SIZE,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE,
                                mFdSdbRpmsg,
                                0);
        printf("\nCA7 : DBG mmappedData[%d]:%p\n", i, mmappedData[i]);
        assert(mmappedData[i] != MAP_FAILED);
        fMappedData = 1;
        sleep_ms(50);
    }

    while (1) {
        if (mMachineState == STATE_SAMPLING_HIGH) {
            // wait till at least one buffer becomes available
            ret = poll(fds, NB_BUF, TIMEOUT * 1000);
            if (ret == -1)
                perror("poll()");
            else if (ret == 0){
                printf("CA7 : No buffer data within %d seconds.\n", TIMEOUT);
            }
            if (fds[mDdrBuffAwaited].revents & POLLIN) {
                rc = read(efd[mDdrBuffAwaited], buf, 16);
                if (!rc) {
                    printf("CA7 : stdin closed\n");
                    return 0;
                }
                /* Get buffer data size*/
                q_get_data_size.bufferId = mDdrBuffAwaited;
 
                if(ioctl(mFdSdbRpmsg, RPMSG_SDB_IOCTL_GET_DATA_SIZE, &q_get_data_size) < 0) {
                    error(EXIT_FAILURE, errno, "Failed to get data size");
                }
 
                if (q_get_data_size.size) {
                    mNbUncompMB++;
                    mNbUncompData += q_get_data_size.size;
                    unsigned char* pData = (unsigned char*)mmappedData[mDdrBuffAwaited];
                    // save a copy of 1st data
                    mByteBuffCpy[0] = *pData;
                    gettimeofday(&tval_after, NULL);
                    timersub(&tval_after, &tval_before, &tval_result);
                        printf("[%ld.%06ld] sdb_thread data EVENT mDdrBuffAwaited=%d mNbUncompData=%u \n", 
                            (long int)tval_result.tv_sec, (long int)tval_result.tv_usec, mDdrBuffAwaited, 
                            mNbUncompData);
                    gdk_threads_add_idle (refreshUI_CB, window);
                }
                else {
                    printf("CA7 : sdb_thread => buf[%d] is empty\n", mDdrBuffAwaited);
                }
                mDdrBuffAwaited++;
                if (mDdrBuffAwaited >= NB_BUF) {
                    mDdrBuffAwaited = 0;
                }
            } else {
                // we face message lost due to SDB driver not managing RPMSG DATA containing several messages
                // in this case, just treat several message
                mDdrBuffAwaited++;
                if (mDdrBuffAwaited >= NB_BUF) {
                    mDdrBuffAwaited = 0;
                }
                if (fds[mDdrBuffAwaited].revents & POLLIN) {
                    rc = read(efd[mDdrBuffAwaited], buf, 16);
                    if (!rc) {
                        printf("CA7 : stdin closed\n");
                        return 0;
                    }
                    /* Get buffer data size*/
                    q_get_data_size.bufferId = mDdrBuffAwaited;
     
                    if(ioctl(mFdSdbRpmsg, RPMSG_SDB_IOCTL_GET_DATA_SIZE, &q_get_data_size) < 0) {
                        error(EXIT_FAILURE, errno, "Failed to get data size");
                    }
     
                    if (q_get_data_size.size) {
                        mNbUncompMB += 2;
                        mNbUncompData += q_get_data_size.size;
                        mNbUncompData += q_get_data_size.size;    // need twice as we missed one
                        unsigned char* pData = (unsigned char*)mmappedData[mDdrBuffAwaited];
                        // save a copy of 1st data
                        mByteBuffCpy[0] = *pData;
                        gettimeofday(&tval_after, NULL);
                        timersub(&tval_after, &tval_before, &tval_result);
                            printf("[%ld.%06ld] sdb_thread data EVENT mDdrBuffAwaited=%d mNbUncompData=%u \n", 
                                (long int)tval_result.tv_sec, (long int)tval_result.tv_usec, mDdrBuffAwaited, 
                                mNbUncompData);
                        gdk_threads_add_idle (refreshUI_CB, window);
                    }
                    else {
                        printf("CA7 : sdb_thread => buf[%d] is empty\n", mDdrBuffAwaited);
                    }
                    mDdrBuffAwaited++;
                    if (mDdrBuffAwaited >= NB_BUF) {
                        mDdrBuffAwaited = 0;
                    }
                } else {
                    // we may have started the timeout, but have stopped and started sampling in RPMSG
                    if (mMachineState == STATE_SAMPLING_HIGH) {
                        n = 0;
                        for (i=0; i<NB_BUF; i++) {
                            n += sprintf(dbgmsg+n, "[%d] ", (fds[i].revents & POLLIN));
                        }
                        printf("CA7 : sdb_thread wrong buffer index ERROR, waiting idx=%d buff status=%s\n", 
                            mDdrBuffAwaited, dbgmsg);
                        mErrorDetected = 2;
                    }
                }
            }
        }
        sleep_ms(5);      // give time to UI
       
    }
    return 0;
}
int main(int argc, char **argv)
{
    int ret = 0, i, cmd;
    char FwName[30];
    strcpy(FIRM_NAME, "how2eldb04140.elf");
    /* check if copro is already running */
    ret = copro_isFwRunning();
    if (ret) {
        // check FW name
        int nameLn = copro_getFwName(FwName);
        if (FwName[nameLn-1] == 0x0a) {
            FwName[nameLn-1] = 0x00;   // replace \n by \0
        }
        if (strcmp(FwName, FIRM_NAME) == 0) {
            printf("CA7 : %s is already running.\n", FIRM_NAME);
            goto fwrunning;
        }else {
            printf("CA7 : wrong FW running. Try to stop it... \n");
            if (copro_stopFw()) {
                printf("CA7 : fails to stop firmware\n");
                goto end;
            }
        }
    }
 
setname:
    /* set the firmware name to load */
    ret = copro_setFwName(FIRM_NAME);
    if (ret <= 0) {
        printf("CA7 : fails to change the firmware name\n");
        goto end;
    }
 
    /* start the firmware */
    if (copro_startFw()) {
        printf("CA7 : fails to start firmware\n");
        goto end;
    }
    /* wait for 1 seconds the creation of the virtual ttyRPMSGx */
    sleep_ms(1000);
 
fwrunning:
    signal(SIGINT, exit_fct); /* Ctrl-C signal */
    signal(SIGTERM, exit_fct); /* kill command */
    gettimeofday(&tval_before, NULL);    // get current time
   
    if (pthread_create( &threadTTY, NULL, virtual_tty_thread, NULL) != 0) {
        printf("CA7 : virtual_tty_thread creation fails\n");
        goto end;
    }

    sleep_ms(500);  // let tty send the DDR buffer command
    if (pthread_create( &threadSDB, NULL, sdb_thread, NULL) != 0) {
        printf("CA7 : sdb_thread creation fails\n");
        goto end;
    }

/****** new production way => use rpmsg-sdb driver to perform CMA buff allocation ******/
 
    mMachineState = STATE_READY;
    mSampFreq_Hz = 4;
    mSampParmCount = 0;

    gtk_init (&argc, &argv);
   
    if (pthread_create( &threadUI, NULL, ui_thread, NULL) != 0) {
        printf("CA7 : ui_thread creation fails\n");
        goto end;
    }

    printf("CA7 : Entering in Main loop\n");
 
    while (1) {
        if (mExitRequested) break;
        if (mErrorDetected) {
            if (mMachineState >= STATE_SAMPLING_LOW) {
                virtual_tty_send_command(strlen("Exit"), "Exit");
                if (mErrorDetected == 2) printf("CA7 : ERROR in DDR Buffer order => Stop sampling!!!\n");
                //else if (mErrorDetected == 2) printf("CA7 : File System full => Stop sampling!!!\n");
                else if (mErrorDetected == 1) printf("CA7 : M4 reported DMA error !!!\n");
                mErrorDetected = 0;
                mMachineState = STATE_READY;
                gdk_threads_add_idle (refreshUI_CB, window);
#if 0
            close_raw_file();
#endif
            }
        }
        sleep_ms(1);      // give time to UI
    }
    for (i=0;i<NB_BUF;i++){
        int rc = munmap(mmappedData[i], DATA_BUF_POOL_SIZE);
        assert(rc == 0);
    }
    fMappedData = 0;
    printf("CA7 : Buffers successfully unmapped\n");
 
end:
    mThreadCancel = 1;
    sleep_ms(100);
    /* check if copro is already running */
    if (copro_isFwRunning()) {
        printf("CA7 : stop the firmware before exit\n");
        copro_closeTtyRpmsg(0);
        copro_closeTtyRpmsg(1);
        copro_stopFw();
    }
    return ret;
}
