#!/bin/sh

#===============================================================================
# start_up_la.sh
#
# This script gets the current user, and calls the "keyboard" application
# accordingly.
# Thanks to the "keyboard" application, when the "USER1" or "USER2" button is
# pressed, the "logic analyzer" application is either started (if stopped) or
# stopped (if already started)
#
# Author: Jean-Christophe Trotin <jean-christophe.trotin@st.com>
# for STMicroelectronics.
#
# Copyright (c) 2022 STMicroelectronics. All rights reserved.
#
# Usage: ./start_up_la.sh
#===============================================================================

#
# Main
#

# Get the current user 
current_user=$USER
echo "Current user: " $current_user

# Build the command
cmd="/usr/local/demo/la/bin/keyboard"
echo "Command: " $cmd

# Call the "keyboard" application 
if [ "$current_user" != "root" ]; then
    script -qc "su -c '$cmd'" /tmp/start_la_typescript &
else
    $cmd &
fi
