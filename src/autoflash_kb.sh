#!/bin/bash

udiskie | while read m
do
if [[ "$m" == *"/RPI-RP2"* ]]
then
    TARGET_PATH=$(echo "$m" | cut -d" " -f4)
    if cp build/usb-ps1-mouse/usb-keyboard-to-ps1-controller.uf2 $TARGET_PATH ; then
        echo -n "flashed "
        date
        zenity --info --text "Flashed" --timeout 3
    else
        echo -n "fail "
        date
        zenity --error --text "Flashing failed"
    fi
fi
done
