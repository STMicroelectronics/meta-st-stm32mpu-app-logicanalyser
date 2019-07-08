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
#include <microhttpd.h>

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

pthread_mutex_t ttyMutex;

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
static int32_t mSampFreq_Hz;
static int32_t mSampFreqH=0, mSampFreqM=0, mSampFreqL=1;
static char mSampFreqU='M';
static int32_t mHtmlNbParmRec;
static machine_state_t mMachineState;
static int32_t mSampParmCount;
static char mHtmlRefreshStr[100];
static char mHtmlFHSelectStr[150], mHtmlFMSelectStr[150], mHtmlFLSelectStr[150], mHtmlFUSelectStr[150];
static char mHtmlButtValueStr[150];
static uint8_t mExitRequested = 0, mErrorDetected = 0;
static uint32_t mNbCompData=0, mNbUncompData=0, mNbWrittenInFileData;
static uint8_t mDdrBuffAwaited;
static uint8_t mThreadCancel = 0;

void* mmappedData[NB_BUF];
FILE *pOutFile;
static char mFileNameStr[150];
static pthread_t thread;

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
Remote UI functions called thanks microhttpd library (Web server)
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

char html_answer_buff[10000];

static int
iterate_post (void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
              const char *filename, const char *content_type,
              const char *transfer_encoding, const char *data, uint64_t off,
              size_t size)
{
  struct connection_info_struct *con_info = coninfo_cls;
  int ret = MHD_YES;

  pthread_mutex_lock(&ttyMutex);
  if (0 == strcmp (key, "onoff")) {
    if (0 == strcmp (data, "Start")) {
        // check machine state and Samp. params
        if ((mMachineState == STATE_READY) && (mSampFreq_Hz > 0)) {
            if (mSampParmCount == 4) {
                mSampParmCount = 0;        // ready for next command
                mMachineState = STATE_SAMPLING;        // needed to force Html refresh => TODO clean this
                mNbCompData=0;
                mNbUncompData=0;
                mNbWrittenInFileData=0;
                mDdrBuffAwaited=0;
                virtual_tty_send_command(strlen(mSamplingStr), mSamplingStr);
                printf("Start sampling at %dHz\n", mSampFreq_Hz);
                open_raw_file();
            } else {
                printf("Start sampling param error due to mSampParmCount=%d\n", mSampParmCount);
            }
        } else {
            printf("Start sampling param error: mMachineState=%d mSampFreq_Hz=%d \n",
                mMachineState, mSampFreq_Hz);
        }
    }
    if (0 == strcmp (data, "Stop")) {
        // check machine state and Samp. params
        if (mMachineState == STATE_SAMPLING) {
            virtual_tty_send_command(strlen("Exit"), "Exit");
            printf("Stop sampling\n");
            mMachineState = STATE_READY;
            close_raw_file();
        } else {
            printf("Stop sampling state error: mMachineState=%d \n",
                mMachineState);
        }
    }
  }

  if (0 == strcmp (key, "sampfreqh")) {
    if (strlen(data) == 1) {
        if ((*data >= '0') && (*data <= '9')) {
            mSampFreqH = *data - '0';
            mSampFreq_Hz = (*data - '0') * 100;
            mSamplingStr[0] = 'S';
            mSamplingStr[1] = *data;
            mSampParmCount = 1;
        } else {
            mSampParmCount = 0;
        }
    } else {
        mSampParmCount = 0;
    }
  }

  if (0 == strcmp (key, "sampfreqm")) {
    if (strlen(data) == 1) {
        if ((*data >= '0') && (*data <= '9')) {
            mSampFreqM = *data - '0';
            mSampFreq_Hz += (*data - '0') * 10;
            mSamplingStr[2] = *data;
            if (mSampParmCount == 1) {
                mSampParmCount++;
            } else {
                mSampParmCount = 0;
            }
        } else {
            mSampParmCount = 0;
        }
    } else {
        mSampParmCount = 0;
    }
  }

  if (0 == strcmp (key, "sampfreql")) {
    if (strlen(data) == 1) {
        if ((*data >= '0') && (*data <= '9')) {
            mSampFreqL = *data - '0';
            mSampFreq_Hz += (*data - '0');
            mSamplingStr[3] = *data;
            if (mSampParmCount == 2) {
                mSampParmCount++;
            } else {
                mSampParmCount = 0;
            }
        } else {
            mSampParmCount = 0;
        }
    } else {
        mSampParmCount = 0;
    }
  }

  if (0 == strcmp (key, "sampfrequ")) {
    if (0 == strcmp (data, "MHz")) {
        mSampFreqU = *data;
        mSampFreq_Hz *= 1000000;

        mSamplingStr[4] = *data;
        if (mSampParmCount == 3) {
            mSampParmCount++;
        } else {
            mSampParmCount = 0;
        }
    } else if (0 == strcmp (data, "kHz")) {
        mSampFreqU = *data;
        mSampFreq_Hz *= 1000;
        mSamplingStr[4] = *data;
        if (mSampParmCount == 3) {
            mSampParmCount++;
        } else {
            mSampParmCount = 0;
        }
    } else {
        mSampParmCount = 0;
    }
  }

  pthread_mutex_unlock(&ttyMutex);

  return ret;
}

static void
request_completed (void *cls, struct MHD_Connection *connection,
                   void **con_cls, enum MHD_RequestTerminationCode toe)
{
  struct connection_info_struct *con_info = *con_cls;

  if (NULL == con_info)
    return;

  if (con_info->connectiontype == POST)
    {
      MHD_destroy_post_processor (con_info->postprocessor);
      if (con_info->answerstring)
        free (con_info->answerstring);
    }

  free (con_info);
  *con_cls = NULL;
}

static int HtmlLaPage(struct MHD_Connection *connection) {
  pthread_mutex_lock(&ttyMutex);
  int n = 0, i;
  char * psel;
  if (mMachineState == STATE_READY) {
      sprintf(mHtmlRefreshStr, "");
      sprintf(mHtmlButtValueStr, "Start");
  } else {
      sprintf(mHtmlRefreshStr, "<meta http-equiv=\"refresh\" content=\"1\">");
      sprintf(mHtmlButtValueStr, "Stop");
  }
  // setup parameter current value, ex: <OPTION selected>0<OPTION>1<OPTION>2<OPTION>3<OPTION>4<OPTION>5<OPTION>6<OPTION>7<OPTION>8<OPTION>9
  n = 0;
  for (i=0; i<10; i++) {
      if (mSampFreqH == i) {
          psel = SELECTED;
      } else {
          psel = NOT_SELECTED;
      }
      n += sprintf(mHtmlFHSelectStr+n, "<OPTION%s>%d", psel, i);
  }
  n = 0;
  for (i=0; i<10; i++) {
      if (mSampFreqM == i) {
          psel = SELECTED;
      } else {
          psel = NOT_SELECTED;
      }
      n += sprintf(mHtmlFMSelectStr+n, "<OPTION%s>%d", psel, i);
  }
  n = 0;
  for (i=0; i<10; i++) {
      if (mSampFreqL == i) {
          psel = SELECTED;
      } else {
          psel = NOT_SELECTED;
      }
      n += sprintf(mHtmlFLSelectStr+n, "<OPTION%s>%d", psel, i);
  }

  n = 0;
  for (i=0; i<4; i++) {
      if (mSampFreqU == FREQU[i]) {
          psel = SELECTED;
      } else {
          psel = NOT_SELECTED;
      }
      n += sprintf(mHtmlFUSelectStr+n, "<OPTION%s>%s", psel, freq_unit_str[i]);
  }

  memset(html_answer_buff, 0, sizeof html_answer_buff);
  sprintf(html_answer_buff,
    "<!DOCTYPE html>"
    "<html>"
    " <head>"
    "  <meta charset=\"utf-8\" />"
    "  %s"
    "  <style type='text/css'>"
    "   section { margin-bottom: 15px; margin-top: 15px; font-size: 20px; }"
    "   table { margin-left:10px; border: 1px solid black; border-collapse: collapse; }"
    "   caption { border: 1px solid black; font-size: 24px; font-weight: bold; background-color: #87ceeb; }"
    "   td { border: none; font-size: 20px; }"
    "   input { padding:3px; font-size: 20px; }"
    "   img { margin-left:350px; }"
    "   .imgmargin { width: 40%; border: 1px solid black; }"
    "   .imgcenter { width: 100%; }"
    "   .head { width: 33%; color: black;} "
    "   .onoff {text-align: center; color: white; background-color: #1020f4;}"
    "   .val { color: blue; text-align: left;}"
    "   .capt {margin-top: 15px; margin-bottom: 15px; font-size: 44px; font-weight: italic; text-align: right; background-color: #cccccc; }"
    "  </style>"
    " </head>"
    " <body bgcolor='#FFFFFF'>"
    "  <header>"
    "   <table width='50%'><caption class='capt'>STHowToM4ToA7BigDataExchange<img src=\"ST_icon2.jpg\" /></caption></table>"
    "  </header>"
    "  <section ><form method='post' name='analysform'>"
    "   <table width='50%'><caption>Control</caption><tbody>"
    "    <tr><td class='head'> Sampling frequency : </td>"
    "     <td><SELECT name='sampfreqh' size='1' >%s</SELECT</td>"
    "     <td><SELECT name='sampfreqm' size='1' >%s</SELECT</td>"
    "     <td><SELECT name='sampfreql' size='1' >%s</SELECT</td>"
    "     <td><SELECT name='sampfrequ' size='1' >%s</SELECT</td></tr>"
    "   </tbody></table>"
    "   <table width='50%'><tbody>"
    "    <tr ><td class='head' align='center'> </td>"
    "     <td class='head'><input style=\"width:100%\" type='submit' name='onoff' class='onoff' value='%s' /></td>"
    "     <td class='head' align='center'> </td></tr></tbody></table>   "
    "  </form></section>"
    "   <table width='50%'><caption>Measurements</caption><tbody>"
    "    <tr ><td>Machine state:</td><td class='val'> %s</td><td></td><td class='val'></td></tr>"
    "    <tr ><td>Nb of compressed data</td><td class='val'> %u</td><td></td><td class='val'></td></tr>"
    "    <tr ><td>Nb of uncompressed data</td><td class='val'> %u</td><td></td><td class='val'></td></tr>"
    "    <tr ><td>Nb of written in file data</td><td class='val'> %u</td><td></td><td class='val'></td></tr>"
    "    <tr ><td>File name</td><td class='val'> %s</td><td></td><td class='val'></td></tr>"
    "   </tbody></table>"
    " </body>"
    "</html>",
    mHtmlRefreshStr,
    mHtmlFHSelectStr, mHtmlFMSelectStr, mHtmlFLSelectStr, mHtmlFUSelectStr, mHtmlButtValueStr,
    machine_state_str[mMachineState], mNbCompData, mNbUncompData, mNbWrittenInFileData, mFileNameStr
  );
  pthread_mutex_unlock(&ttyMutex);

  struct MHD_Response *response;
  int ret;

  response =
    MHD_create_response_from_buffer (strlen (html_answer_buff), (void *) html_answer_buff,
                     MHD_RESPMEM_PERSISTENT);
  ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
  MHD_destroy_response (response);
  return ret;
}

static int
answer_to_connection (void *cls, struct MHD_Connection *connection,
                      const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls)
{
  struct MHD_Response *response;
  int ret;
  FILE *file;
  struct stat buf;
  if (NULL == *con_cls) {
    struct connection_info_struct *con_info;
    con_info = malloc (sizeof (struct connection_info_struct));
    if (NULL == con_info) {
        return MHD_NO;
    }
    con_info->answerstring = NULL;
    if (0 == strcmp (method, "POST")) {
        con_info->postprocessor = MHD_create_post_processor (connection, POSTBUFFERSIZE,
            iterate_post, (void *) con_info);
        if (NULL == con_info->postprocessor) {
              free (con_info);
              return MHD_NO;
        }
        con_info->connectiontype = POST;
    } else {
        con_info->connectiontype = GET;
    }
    *con_cls = (void *) con_info;
    return MHD_YES;
  }
  if (0 == strcmp (method, "POST")) {
    struct connection_info_struct *con_info = *con_cls;
    if (*upload_data_size != 0) {
        MHD_post_process (con_info->postprocessor, upload_data,
                *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }
  }
  if (0 == strcmp (method, "GET")) {
    if ( (0 == stat (&url[1], &buf)) && (S_ISREG (buf.st_mode)) ) {
        file = fopen (&url[1], "rb");
        if (file != NULL) {
            response = MHD_create_response_from_callback (buf.st_size, 32 * 1024,     /* 32k PAGE_NOT_FOUND size */
                    &file_reader, file, &file_free_callback);
            if (response == NULL) {
                fclose (file);
                return MHD_NO;
            }
            ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
            MHD_destroy_response (response);
            return ret;
        }
    }
  }
  ret = HtmlLaPage(connection);
  return ret;
}
/*************************************************************************************
End of Remote UI functions
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
    mThreadCancel = 1;
    sleep_ms(100);
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
                printf("[%ld.%06ld] vitural_tty_thread treat: %s\n", (long int)tval_result.tv_sec, (long int)tval_result.tv_usec, mByteBuffCpy);
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
                                    pthread_mutex_lock(&ttyMutex);
                                    mDdrBuffAwaited++;
                                    if (mDdrBuffAwaited > 2) {
                                        mDdrBuffAwaited = 0;
                                    }
                                    mNbCompData += length;
                                    //mNbUncompData += (uint64_t)(length*8);

                                    unsigned char* pCompData = (unsigned char*)mmappedData[buffIdx];
                                    for (int i=0; i<length; i++) {
                                        mNbUncompData += (1 + (*(pCompData+i) >> 5));
                                    }

                                    wsize= write_raw_file(mmappedData[buffIdx], length);
                                    if (wsize == (int32_t)length) {
                                        mNbWrittenInFileData += wsize;
                                    } else {
                                        mErrorDetected = 2;
                                    }
                                    pthread_mutex_unlock(&ttyMutex);
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
    strcpy(FIRM_NAME, "rprochdrlawc01100.elf");
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
        printf("\nDBG errno:%d - mmappedData[%d]:%p\n",errno,i,mmappedData[i]);
        assert(mmappedData[i] != MAP_FAILED);
    }
    pthread_mutex_init(&ttyMutex, NULL);
    mHttpDaemon = MHD_start_daemon (MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
                            &answer_to_connection, NULL,
                            MHD_OPTION_NOTIFY_COMPLETED, request_completed,
                            NULL, MHD_OPTION_END);
    if (NULL == mHttpDaemon) {
        printf("MHD_start_daemon fails!!!\n");
    }
    mMachineState = STATE_READY;
    mSampFreq_Hz = 0;
    mSampParmCount = 0;
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
