#!/bin/bash

udiskie | while read m
do
if [[ "$m" == *"/RPI-RP2"* ]]
then
    TARGET_PATH=$(echo "$m" | cut -d" " -f4)
    if cp build/usb-ps1-mouse/main.uf2 $TARGET_PATH ; then
        echo -n "flashed "
        date
        zenity --info --text "Flashed"
    else
        echo -n "fail "
        date
        zenity --error --text "Flashing failed"
    fi
fi
done
