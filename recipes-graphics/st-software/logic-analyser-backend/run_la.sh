#!/bin/sh

# Assume that this script is being run from the root of the archive
here=`pwd`

cd /usr/local/demo/la/bin
BACK_END=/usr/local/demo/la/bin/backend

gui_start() {
    #Rotation
    rot=0

    insmod /lib/modules/$(uname -r)/kernel/drivers/misc/stm32_rpmsg_sdb.ko
    ${BACK_END} &
}

gui_stop() {
    killall backend
    echo stop >/sys/class/remoteproc/remoteproc0/state
    rmmod stm32_rpmsg_sdb.ko
}

pidof backend >/dev/null
if [[ $? -ne 0 ]] ; then
    gui_start
else
    gui_stop
fi

exit 0
