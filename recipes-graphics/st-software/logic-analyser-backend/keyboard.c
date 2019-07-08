/*
 * <Description>
 *
 * Copyright (C) 2018, STMicroelectronics - All Rights Reserved
 * Author: YOUR NAME <> for STMicroelectronics.
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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <pthread.h>

#define USER_GPIO_OFFSET 13

int running = 0;
int gpio13_pressed = 0;
int gpio14_pressed = 0;
static pthread_t thread_ioctl1;
static pthread_t thread_ioctl2;
pthread_mutex_t keybMutex;
struct gpioevent_request ereq1, ereq2;
struct gpioevent_data event1, event2;

static void sleep_ms(int milliseconds)
{
	usleep(milliseconds * 1000);
}

/***************** configure_userbutton ***************************/
int configure_userbutton(struct gpioevent_request *ereq, int offset)
{
	char chrdev_name[20];
	int fd,ret;

	strcpy(chrdev_name, "/dev/gpiochip0");

	/*  Open device: gpiochip0 for GPIO bank A */
	fd = open(chrdev_name, 0);

	if (fd == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to open %s\n", chrdev_name);
		return ret;
	}

	/* request GPIO line for Button activation*/
	memset(ereq,0,sizeof(struct gpioevent_request));
	ereq->lineoffset = offset;
	ereq->handleflags = GPIOHANDLE_REQUEST_INPUT;
	ereq->eventflags = GPIOEVENT_EVENT_FALLING_EDGE;
    sprintf(ereq->consumer_label, "User PA%02d", offset);

	ret = ioctl(fd, GPIO_GET_LINEEVENT_IOCTL, ereq);
	if (ret == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to issue GET event1 IOCTL (%d)\n",ret);
		return ret;
	}

	close(fd);
	return 0;
}

void *ioctl1_thread(void *arg)
{
    int ret;
    while (1) {
        /* read User button input event1 */
		ret = read(ereq1.fd, &event1, sizeof(event1));
		if (ret == -1) {
			if (errno == -EAGAIN) {
				fprintf(stderr, "nothing available\n");
				continue;
			} else {
				ret = -errno;
				fprintf(stderr, "Failed to read event1 (%d)\n",ret);
				break;
			}
		}
		if (ret != sizeof(event1)) {
			fprintf(stderr, "Reading event1 failed\n");
			ret = -EIO;
			break;
		}

		/* process the event1 received */
		switch (event1.id) {
			case GPIOEVENT_EVENT_FALLING_EDGE:
                printf("GPIO13 GPIOEVENT_EVENT_FALLING_EDGE\n");
                pthread_mutex_lock(&keybMutex);
                gpio13_pressed = 1;
                pthread_mutex_unlock(&keybMutex);
				break;
			default:
				fprintf(stdout, "unknown event1\n");
		}
    }
}

void *ioctl2_thread(void *arg)
{
    int ret;
    while (1) {
		/* read User button input event2 */
		ret = read(ereq2.fd, &event2, sizeof(event2));
		if (ret == -1) {
			if (errno == -EAGAIN) {
				fprintf(stderr, "nothing available\n");
				continue;
			} else {
				ret = -errno;
				fprintf(stderr, "Failed to read event2 (%d)\n",ret);
				break;
			}
		}
		if (ret != sizeof(event2)) {
			fprintf(stderr, "Reading event2 failed\n");
			ret = -EIO;
			break;
		}

		/* process the event2 received */
		switch (event2.id) {
			case GPIOEVENT_EVENT_FALLING_EDGE:
                printf("GPIO14 GPIOEVENT_EVENT_FALLING_EDGE\n");
                pthread_mutex_lock(&keybMutex);
                gpio14_pressed = 1;
                pthread_mutex_unlock(&keybMutex);
				break;
			default:
				fprintf(stdout, "unknown event2\n");
		}
    }
}


int main(int argc, char **argv)
{
	int ret=0;

	printf("read keyb event thread\n");

	if ((getuid ()) != 0) {
		fprintf(stderr, "You are not root! This may not work...\n");
		return 0;
	}

	/* configure USER button */
	if (configure_userbutton(&ereq1, 14) < 0){
		perror("GPIO_A14 export issue");
		goto quit;
	}
	if (configure_userbutton(&ereq2, 13) < 0){
		perror("GPIO_A13 export issue");
		goto quit;
	}
  	if (pthread_create( &thread_ioctl1, NULL, ioctl1_thread, NULL) != 0) {
		printf("greio_receiver_thread creation fails\n");
		goto quit;
	}
  	if (pthread_create( &thread_ioctl2, NULL, ioctl2_thread, NULL) != 0) {
		printf("greio_receiver_thread creation fails\n");
		goto quit;
	}
	pthread_mutex_init(&keybMutex, NULL);

	while(1) {
		sleep_ms(1);
        pthread_mutex_lock(&keybMutex);
        if (gpio13_pressed) {
            gpio13_pressed = 0;
            system("/usr/local/demo/la/run_la.sh");
        } else if (gpio14_pressed) {
            gpio14_pressed = 0;
            system("/usr/local/demo/la/run_la.sh");
        }
        pthread_mutex_unlock(&keybMutex);
	}
    return EXIT_SUCCESS; 

quit:
	return 0;
}
