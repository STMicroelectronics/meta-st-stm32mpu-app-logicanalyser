#!/bin/sh

#===============================================================================
# run_la.sh
#
# This script gets the user associated with the weston application, and calls
# the "logic analyzer" application accordingly.
#
# Author: Jean-Christophe Trotin <jean-christophe.trotin@st.com>
# for STMicroelectronics.
#
# Copyright (c) 2022 STMicroelectronics. All rights reserved.
#
# Usage: ./run_la.sh
#===============================================================================

#
# Function: get the weston user ("root" or "weston")
#
get_weston_user() {
    ps aux | grep '/usr/bin/weston ' | grep -v 'grep' | awk '{print $1}'
}

#
# Main
#

# Get the user associated with the weston application: 
weston_user=$(get_weston_user)
echo "Weston user: " $weston_user

# Build the command
cmd="/usr/local/demo/la/bin/backend"
echo "Command: " $cmd

# Manage the "logic analyzer" application
pidof backend >/dev/null
if [[ $? -ne 0 ]] ; then
    if [ "$weston_user" != "root" ]; then
        script -qc "su -l $weston_user -c '$cmd'" &
    else
        $cmd &
    fi
else
    killall backend
    # To get traces in a file (/tmp/start_la_typescript), comment the above line
    rm /tmp/start_la_typescript
fi
