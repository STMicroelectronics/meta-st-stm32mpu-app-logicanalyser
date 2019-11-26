/*
 * logic_analyser_backend.c
 * Implements the logic analyser backend to intereact with the GUI.
 *
 * Copyright (C) 2019, STMicroelectronics - All Rights Reserved
 * Author: Michel Catrouillet <michel.catrouillet@st.com> for STMicroelectronics.
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
 * <http://www.gnu.org/licenses/>.
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
#include <errno.h>
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
#define NB_BUF 3

typedef struct
{
    int bufferId, eventfd;
} rpmsg_sdb_ioctl_set_efd;

struct connection_info_struct
{
  int connectiontype;
  char *answerstring;
  struct MHD_PostProcessor *postprocessor;
};

//pthread_mutex_t ttyMutex;

struct MHD_Daemon *mHttpDaemon;

char FIRM_NAME[50];
struct timeval tval_before, tval_after, tval_result;

typedef enum {
  STATE_READY = 0,
  STATE_SAMPLING,
} machine_state_t;

static char machine_state_str[5][13] = {"READY", "SAMPLING"};
static char SELECTED[10] = {" selected"};
static char NOT_SELECTED[10] = {""};
static char freq_unit_str[3][4] = {"MHz", "kHz", "Hz"};
static char FREQU[3] = {'M', 'k', 'H'};

/* The file descriptor used to manage our TTY over RPMSG */
static int mFdRpmsg = -1;

static int virtual_tty_send_command(int len, char* commandStr);

static char mByteBuffer[512];
static char mByteBuffCpy[512];

static pthread_t thread_tty;

static char mSamplingStr[15];
static int32_t mSampFreq_Hz = 4;
static int32_t mSampFreqH=0, mSampFreqM=0, mSampFreqL=1;
static char mSampFreqU='M';
static int32_t mHtmlNbParmRec;
static machine_state_t mMachineState;
static int32_t mSampParmCount;
static char mHtmlRefreshStr[100];
static char mHtmlFHSelectStr[150], mHtmlFMSelectStr[150], mHtmlFLSelectStr[150], mHtmlFUSelectStr[150];
static char mHtmlButtValueStr[150];
static uint8_t mExitRequested = 0, mErrorDetected = 0, mUIrefreshReq = 0;
static uint32_t mNbCompData=0, mNbUncompData=0, mNbWrittenInFileData;
static uint8_t mDdrBuffAwaited;
static uint8_t mThreadCancel = 0;
//static    char tmpStr[80];

void* mmappedData[NB_BUF];
static    int fMappedData = 0;
FILE *pOutFile;
static char mFileNameStr[150];
static pthread_t thread, thread2;

static    GtkWidget *window;
static    GtkWidget *f_scale;
static    GtkAdjustment *fadjustment;
static    GtkWidget *fullVBox;
    
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
static    GtkWidget *nbWrittenData_label;
static    GtkWidget *nbWrittenData_value;
static    GtkWidget *fileName_label;
static    GtkWidget *fileName_value;
static    GtkWidget *butSingle;
    
/********************************************************************************
Copro functions allowing to manage a virtual TTY over RPMSG
*********************************************************************************/
int copro_isFwRunning(void)
{
    int fd;
    size_t byte_read;
    int result = 0;
    unsigned char bufRead[MAX_BUF];
    fd = open("/sys/class/remoteproc/remoteproc0/state", O_RDWR);
    if (fd < 0) {
        printf("Error opening remoteproc0/state, err=-%d\n", errno);
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
    fd = open("/sys/class/remoteproc/remoteproc0/state", O_RDWR);
    if (fd < 0) {
        printf("Error opening remoteproc0/state, err=-%d\n", errno);
        return (errno * -1);
    }
    write(fd, "stop", strlen("stop"));
    close(fd);
    return 0;
}

int copro_startFw(void)
{
    int fd;
    fd = open("/sys/class/remoteproc/remoteproc0/state", O_RDWR);
    if (fd < 0) {
        printf("Error opening remoteproc0/state, err=-%d\n", errno);
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
    fd = open("/sys/module/firmware_class/parameters/path", O_RDWR);
    if (fd < 0) {
        printf("Error opening firmware_class/parameters/path, err=-%d\n", errno);
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
    fd = open("/sys/module/firmware_class/parameters/path", O_RDWR);
    if (fd < 0) {
        printf("Error opening firmware_class/parameters/path, err=-%d\n", errno);
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
    fd = open("/sys/class/remoteproc/remoteproc0/firmware", O_RDWR);
    if (fd < 0) {
        printf("Error opening remoteproc0/firmware, err=-%d\n", errno);
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
    fd = open("/sys/class/remoteproc/remoteproc0/firmware", O_RDWR);
    if (fd < 0) {
        printf("Error opening remoteproc0/firmware, err=-%d\n", errno);
        return (errno * -1);
    }
    result = write(fd, nameStr, strlen(nameStr));
    close(fd);
    return result;
}

int copro_openTtyRpmsg(int modeRaw)
{
    struct termios tiorpmsg;
    mFdRpmsg = open("/dev/ttyRPMSG0", O_RDWR |  O_NOCTTY | O_NONBLOCK);
    if (mFdRpmsg < 0) {
        printf("Error opening ttyRPMSG0, err=-%d\n", errno);
        return (errno * -1);
    }
    /* get current port settings */
    tcgetattr(mFdRpmsg,&tiorpmsg);
    if (modeRaw) {
        tiorpmsg.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                      | INLCR | IGNCR | ICRNL | IXON);
        tiorpmsg.c_oflag &= ~OPOST;
        tiorpmsg.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        tiorpmsg.c_cflag &= ~(CSIZE | PARENB);
        tiorpmsg.c_cflag |= CS8;
    } else {
        /* ECHO off, other bits unchanged */
        tiorpmsg.c_lflag &= ~ECHO;
        /*do not convert LF to CR LF */
        tiorpmsg.c_oflag &= ~ONLCR;
    }
    tcsetattr(mFdRpmsg, TCSANOW, &tiorpmsg);
    return 0;
}

int copro_closeTtyRpmsg(void)
{
    close(mFdRpmsg);
    mFdRpmsg = -1;
    return 0;
}

int copro_writeTtyRpmsg(int len, char* pData)
{
    int result = 0;
    if (mFdRpmsg < 0) {
        printf("Error writing ttyRPMSG0, fileDescriptor is not set\n");
        return mFdRpmsg;
    }

    result = write(mFdRpmsg, pData, len);
    return result;
}

int copro_readTtyRpmsg(int len, char* pData)
{
    int byte_rd, byte_avail;
    int result = 0;
    if (mFdRpmsg < 0) {
        printf("Error reading ttyRPMSG0, fileDescriptor is not set\n");
        return mFdRpmsg;
    }
    ioctl(mFdRpmsg, FIONREAD, &byte_avail);
    if (byte_avail > 0) {
        if (byte_avail >= len) {
            byte_rd = read (mFdRpmsg, pData, len);
        } else {
            byte_rd = read (mFdRpmsg, pData, byte_avail);
        }
        //printf("read successfully %d bytes to %p, [0]=0x%x\n", byte_rd, pData, pData[0]);
        result = byte_rd;
    } else {
        result = 0;
    }
    return result;
}
/********************************************************************************
End of Copro functions
*********************************************************************************/


int64_t
print_time() {
    struct timespec ts;
    clock_gettime(1, &ts);
    printf("Time: %lld\n", (ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL));
}

int acount = 0;

/********************************************************************************
GTK UI functions
*********************************************************************************/
static ssize_t
file_reader (void *cls, uint64_t pos, char *buf, size_t max)
{
  FILE *file = cls;

  (void) fseek (file, pos, SEEK_SET);
  return fread (buf, 1, max, file);
}

static void
file_free_callback (void *cls)
{
  FILE *file = cls;
  fclose (file);
}

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
    fclose(pOutFile);
}

static gboolean refreshUI_CB (gpointer data)
{
    char tmpStr[100];
   
    gtk_label_set_text (GTK_LABEL (state_value), machine_state_str[mMachineState]);
    sprintf(tmpStr, "%u", mNbCompData);
    gtk_label_set_text (GTK_LABEL (nbCompData_value), tmpStr);
    sprintf(tmpStr, "%u", mNbUncompData);
    gtk_label_set_text (GTK_LABEL (nbRealData_value), tmpStr);
    sprintf(tmpStr, "%u", mNbWrittenInFileData);
    gtk_label_set_text (GTK_LABEL (nbWrittenData_value), tmpStr);
    gtk_label_set_text (GTK_LABEL (fileName_value), mFileNameStr);
    
    gtk_widget_show_all(window);

   return FALSE;
}

static void single_clicked (GtkWidget *widget, gpointer data)
{
    if (mMachineState == STATE_READY) {
        mMachineState = STATE_SAMPLING;        // needed to force Html refresh => TODO clean this
        mNbCompData=0;
        mNbUncompData=0;
        mNbWrittenInFileData=0;
        mDdrBuffAwaited=0;
        // build sampling string
        sprintf(mSamplingStr, "S%03dM", mSampFreq_Hz);
        //sprintf(mSamplingStr, "S004M", mSampFreq_Hz);
        virtual_tty_send_command(strlen(mSamplingStr), mSamplingStr);
        printf("Start sampling at %dMHz\n", mSampFreq_Hz);
        open_raw_file();
    } else if (mMachineState == STATE_SAMPLING) {
        virtual_tty_send_command(strlen("Exit"), "Exit");
        printf("Stop sampling\n");
        mMachineState = STATE_READY;
        close_raw_file();
        gdk_threads_add_idle (refreshUI_CB, window);
    } else {
        printf("Start sampling param error: mMachineState=%d mSampFreq_Hz=%d \n",
            mMachineState, mSampFreq_Hz);
    }
}

static void f_scale_moved (GtkRange *range, gpointer user_data)
{
   GtkWidget *label = user_data;

   gdouble pos = gtk_range_get_value (range);
   gdouble val = 1 + 19 * pos / 100;
   gchar *str = g_strdup_printf ("%.0f", val);
   mSampFreq_Hz = atoi(str);
   gtk_label_set_text (GTK_LABEL (label), str);
   printf("fscale = %d\n", mSampFreq_Hz);

   g_free(str);
}

void *ui_thread(void *arg)
{
    
    GdkColor color;
    GtkWidget *mainGrid;
    char tmpStr[100];
    
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(mFileNameStr, "/usr/local/demo/la/%04d%02d%02d-%02d%02d%02d.dat",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        
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
    gtk_widget_set_name (controlTitle_label, "control_title");
    
    fTitle_label = gtk_label_new ("Sampling freq. (MHz) :");
    gtk_label_set_xalign (GTK_LABEL (fTitle_label), 0);
    fValue_label = gtk_label_new ("4");
    gtk_label_set_xalign (GTK_LABEL (fValue_label), 0);
    
    fadjustment = gtk_adjustment_new (16, 0, 100, 5, 10, 0);
    f_scale = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, fadjustment);
    gtk_scale_set_draw_value (GTK_SCALE (f_scale), FALSE);
  
    g_signal_connect (f_scale, 
                    "value-changed", 
                    G_CALLBACK (f_scale_moved), 
                    fValue_label);
    
    measurTitle_label = gtk_label_new ("Measurements");
    gtk_widget_set_name (measurTitle_label, "measur_title");

    state_label = gtk_label_new ("Machine state :");
    gtk_label_set_xalign (GTK_LABEL (state_label), 0);
    state_value = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (state_value), 0);
    
    nbCompData_label = gtk_label_new ("Nb of compressed data :");
    gtk_label_set_xalign (GTK_LABEL (nbCompData_label), 0);
    nbCompData_value = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (nbCompData_value), 0);
    nbRealData_label = gtk_label_new ("Nb of uncompressed data :");
    gtk_label_set_xalign (GTK_LABEL (nbRealData_label), 0);
    nbRealData_value = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (nbRealData_value), 0);
    nbWrittenData_label = gtk_label_new ("Nb of written in file data :");
    gtk_label_set_xalign (GTK_LABEL (nbWrittenData_label), 0);
    nbWrittenData_value = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (nbWrittenData_value), 0);
    fileName_label = gtk_label_new ("File name :");
    gtk_label_set_xalign (GTK_LABEL (fileName_label), 0);
    fileName_value = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (fileName_value), 0);
    
    gtk_label_set_text (GTK_LABEL (state_value), machine_state_str[mMachineState]);
    sprintf(tmpStr, "%u", mNbCompData);
    gtk_label_set_text (GTK_LABEL (nbCompData_value), tmpStr);
    sprintf(tmpStr, "%u", mNbUncompData);
    gtk_label_set_text (GTK_LABEL (nbRealData_value), tmpStr);
    sprintf(tmpStr, "%u", mNbWrittenInFileData);
    gtk_label_set_text (GTK_LABEL (nbWrittenData_value), tmpStr);
    gtk_label_set_text (GTK_LABEL (fileName_value), mFileNameStr);
    
    butSingle = gtk_button_new_with_label("Start");
    g_signal_connect(butSingle, 
                    "clicked", 
                    G_CALLBACK (single_clicked), 
                    NULL);
                    
    mainGrid = gtk_grid_new ();
    // Control title in (1,0) is 3 columns large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), controlTitle_label, 1, 0, 3, 1);
    // Freq. title in (0,1) is 1 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), fTitle_label, 0, 1, 1, 1);
    // Freq. value in (1,1) is 1 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), fValue_label, 1, 1, 1, 1);
    // Freq. scale in (2,1) is 3 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), f_scale, 2, 1, 3, 1);
    
    // Start button in (1,2) is 3 column large & 2 row high
    gtk_grid_attach (GTK_GRID (mainGrid), butSingle, 1, 2, 3, 2);
    
    // Measurement title in (1,4) is 3 columns large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), measurTitle_label, 1, 4, 3, 1);
    // State label in (0,5) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), state_label, 0, 5, 2, 1);
    // State value in (2,5) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), state_value, 2, 5, 2, 1);
    
    // Comp data label in (0,6) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbCompData_label, 0, 6, 2, 1);
    // Comp data value in (2,6) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbCompData_value, 2, 6, 2, 1);
    
    // Real data label in (0,7) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbRealData_label, 0, 7, 2, 1);
    // Real data value in (2,7) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbRealData_value, 2, 7, 2, 1);
    
    // Written data label in (0,8) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbWrittenData_label, 0, 8, 2, 1);
    // Written data value in (2,8) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), nbWrittenData_value, 2, 8, 2, 1);
    
    // File name label in (0,9) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), fileName_label, 0, 9, 2, 1);
    // File name value in (2,9) is 2 column large & 1 row high
    gtk_grid_attach (GTK_GRID (mainGrid), fileName_value, 2, 9, 2, 1);
    
    gtk_grid_set_row_homogeneous (GTK_GRID (mainGrid), TRUE);
    
    gtk_container_add (GTK_CONTAINER (window), mainGrid);

    gtk_widget_show_all(window);
    

    gtk_main ();
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
    printf("[%lld] virtual_tty_send_command len=%d => %s\n",
        (ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL), len, commandStr);
    return copro_writeTtyRpmsg(len, commandStr);
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
        printf("Buffers successfully unmapped\n");
    }

    if (copro_isFwRunning()) {
        mExitRequested = 1;
        //while (mExitRequested);
        copro_closeTtyRpmsg();
        copro_stopFw();
        printf("stop the firmware before exit\n");
    }
    exit(signum);
}

void *vitural_tty_thread(void *arg)
{
    int read;
    int8_t mesgOK = 1;
    uint32_t length;
    int32_t wsize;
    int buffIdx = 3;
    char tmpStr[100];

    while (1) {
        if (mThreadCancel) break;    // kill thread requested
        read = copro_readTtyRpmsg(512, mByteBuffer);
        if (read > 0) {
            mByteBuffer[read] = 0;
            gettimeofday(&tval_after, NULL);
            timersub(&tval_after, &tval_before, &tval_result);
            printf("[%ld.%06ld] virtTTY_RX %d bytes: %s\n", (long int)tval_result.tv_sec, (long int)tval_result.tv_usec, read, mByteBuffer);
        }
        // check read multiple of 19 and treat in a loop
        if ((read > 0) && ((read % 19) == 0)) {
            for (int m=0; m<read; m+=19) {
                strncpy(mByteBuffCpy, &mByteBuffer[m], 19);
                mByteBuffCpy[19] = 0;    // insure end of string
                mesgOK = 1;
                length = 0;
                // Treat M4FW event, format: "DMA2DDR-BnLxxxxxxxx"
                if (strstr(mByteBuffCpy, "DMA2DDR-B")) {
                    if (mByteBuffCpy[9] >= '0' && mByteBuffCpy[9] <= '2') {
                        buffIdx = mByteBuffCpy[9] - '0';
                        if (mDdrBuffAwaited == buffIdx) {
                            if (mByteBuffCpy[10] == 'L') {
                                for (int i=0; i<8; i++) {
                                    length <<=4;
                                    if (mByteBuffCpy[11+i] >= '0' && mByteBuffCpy[11+i] <= '9') {
                                        length |= (mByteBuffCpy[11+i] - '0');
                                    } else if (mByteBuffCpy[11+i] >= 'a' && mByteBuffCpy[11+i] <= 'f') {
                                        length |= (mByteBuffCpy[11+i] - 'a' + 10);
                                    } else {
                                        mesgOK = -1;
                                        break;
                                    }
                                }
                                if (mesgOK == 1) {
                                    mDdrBuffAwaited++;
                                    if (mDdrBuffAwaited > 2) {
                                        mDdrBuffAwaited = 0;
                                    }
                                    mNbCompData += length;

                                    unsigned char* pCompData = (unsigned char*)mmappedData[buffIdx];
                                    for (int i=0; i<length; i++) {
                                        mNbUncompData += (1 + (*(pCompData+i) >> 5));
                                    }

                                    wsize= write_raw_file(mmappedData[buffIdx], length);
                                    if (wsize == (int32_t)length) {
                                        mNbWrittenInFileData += wsize;
                                        printf("[%ld.%06ld] vitural_tty_thread data EVENT buffIdx=%d mNbCompData=%u mNbUncompData=%u mNbWrittenInFileData=%u\n", 
                                            (long int)tval_result.tv_sec, (long int)tval_result.tv_usec, buffIdx, mNbCompData, mNbUncompData, mNbWrittenInFileData);
                                        
                                        gdk_threads_add_idle (refreshUI_CB, window);

                                    } else {
                                        mErrorDetected = 2;
                                    }
                                }
                            } else {
                                mesgOK = -2;
                            }
                        } else {
                            printf("vitural_tty_thread ERROR, mDdrBuffAwaited=%d buffIdx=%d", mDdrBuffAwaited, buffIdx);
                            mesgOK = -3;
                        }
                    } else {
                        mesgOK = -4;
                    }
                } else {
                    mesgOK = 0;
                }
            }
            if (mesgOK < 0) {
                mErrorDetected = 1;
                printf("[%ld.%06ld] vitural_tty_thread DATA EVENT ERROR mesgOK=%d\n", 
                    (long int)tval_result.tv_sec, (long int)tval_result.tv_usec, mesgOK);
            }
        }
        sleep_ms(5);      // give time to UI
    }
}

int main(int argc, char **argv)
{
    int ret = 0, i, j, fd, cmd, rc, mesgOK;
    char *filename = "/dev/rpmsg-sdb";
    int efd[NB_BUF];
    struct pollfd fds[NB_BUF];
    char buf[32];
    rpmsg_sdb_ioctl_set_efd q;
    unsigned int length;
    char FwName[30];
    //strcpy(FIRM_NAME, "rprochdrlawc01100.elf");
    //strcpy(FIRM_NAME, "how2eldb_CM4.elf");
    strcpy(FIRM_NAME, "how2eldb02110.elf");
    /* check if copro is already running */
    ret = copro_isFwRunning();
    if (ret) {
        // check FW name
        int nameLn = copro_getFwName(FwName);
        if (FwName[nameLn-1] == 0x0a) {
            FwName[nameLn-1] = 0x00;   // replace \n by \0
        }
        if (strcmp(FwName, FIRM_NAME) == 0) {
            printf("%s is already running.\n", FIRM_NAME);
            goto fwrunning;
        }else {
            printf("wrong FW running. Try to stop it... \n");
            if (copro_stopFw()) {
                printf("fails to stop firmware\n");
                goto end;
            }
        }
    }

setname:
    /* set the firmware name to load */
    ret = copro_setFwName(FIRM_NAME);
    if (ret <= 0) {
        printf("fails to change the firmware name\n");
        goto end;
    }

    /* start the firmware */
    if (copro_startFw()) {
        printf("fails to start firmware\n");
        goto end;
    }
    /* wait for 1 seconds the creation of the virtual ttyRPMSGx */
    sleep_ms(1000);

fwrunning:
    signal(SIGINT, exit_fct); /* Ctrl-C signal */
    signal(SIGTERM, exit_fct); /* kill command */
    gettimeofday(&tval_before, NULL);    // get current time
    // open the ttyRPMSG in raw mode
    if (copro_openTtyRpmsg(1)) {
        printf("fails to open the tty RPMESG\n");
        return (errno * -1);
    }
    virtual_tty_send_command(strlen("r"), "r");    // needed to allow M4 to send any data over virtualTTY
    if (pthread_create( &thread, NULL, vitural_tty_thread, NULL) != 0) {
        printf("vitural_tty_thread creation fails\n");
        goto end;
    }

/****** new production way => use rpmsg-sdb driver to perform CMA buff allocation ******/
    size_t filesize = DATA_BUF_POOL_SIZE;
    printf("DBG filesize:%d\n",filesize);

    //Open file
    fd = open(filename, O_RDWR);
    assert(fd != -1);
    for (i=0;i<NB_BUF;i++){
        // Create the evenfd, and sent it to kernel driver, for notification of buffer full
        efd[i] = eventfd(0, 0);
        if (efd[i] == -1)
            error(EXIT_FAILURE, errno,
                "failed to get eventfd");
        printf("\nForward efd info for buf%d via cmd:%d with fd:%d and efd:%d\n",i,cmd,fd,efd[i]);
        q.bufferId = i;
        q.eventfd = efd[i];
        if(ioctl(fd, RPMSG_SDB_IOCTL_SET_EFD,&q) < 0)
            error(EXIT_FAILURE, errno,
                "failed to set efd");
        // watch eventfd for input
        fds[i].fd = efd[i];
        fds[i].events = POLLIN;
        mmappedData[i] = mmap(NULL,
                                filesize,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE,
                                fd,
                                0);
        printf("\nDBG mmappedData[%d]:%p\n", i, mmappedData[i]);
        assert(mmappedData[i] != MAP_FAILED);
        fMappedData = 1;
    }

    mMachineState = STATE_READY;
    mSampFreq_Hz = 4;
    mSampParmCount = 0;
    
    gtk_init (&argc, &argv);
    
    if (pthread_create( &thread2, NULL, ui_thread, NULL) != 0) {
        printf("ui_thread creation fails\n");
        goto end;
    }
    
    printf("Entering in Main loop\n");

    while (1) {
        if (mExitRequested) break;
        if (mErrorDetected) {
            if (mMachineState == STATE_SAMPLING) {
            virtual_tty_send_command(strlen("Exit"), "Exit");
            if (mErrorDetected == 1) printf("ERROR in DDR Buffer order => Stop sampling!!!\n");
            else if (mErrorDetected == 2) printf("File System full => Stop sampling!!!\n");
            mErrorDetected = 0;
            mMachineState = STATE_READY;
            close_raw_file();
            }
        }
        sleep_ms(1);      // give time to UI
    }
    for (i=0;i<NB_BUF;i++){
        int rc = munmap(mmappedData[i], filesize);
        assert(rc == 0);
    }
    fMappedData = 0;
    printf("Buffers successfully unmapped\n");

end:
    mThreadCancel = 1;
    sleep_ms(100);
    /* check if copro is already running */
    if (copro_isFwRunning()) {
        printf("stop the firmware before exit\n");
        copro_closeTtyRpmsg();
        copro_stopFw();
    }
    return ret;
}
