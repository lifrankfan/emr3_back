#!/bin/bash

for mem in /sys/devices/system/memory/memory*/state; do
    state=$(cat "$mem")
    if [ "$state" = "offline" ]; then
        echo "${mem%/state} is offline"
    fi
done