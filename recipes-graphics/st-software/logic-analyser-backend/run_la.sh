#!/bin/sh

# Assume that this script is being run from the root of the archive
here=`pwd`

cd /usr/local/demo/la/bin
BACK_END=/usr/local/demo/la/bin/backend

gui_start() {
    #Rotation
    rot=0

    ${BACK_END} &
# To get traces in a file, replace the previous line by the following one:
#    ${BACK_END} &> la_trace.txt &
}

gui_stop() {
    killall backend
}

pidof backend >/dev/null
if [[ $? -ne 0 ]] ; then
    gui_start
else
    gui_stop
fi

exit 0
