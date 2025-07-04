#!/bin/bash

git clone --branch 0.7.1 --single-branch https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
git clone --branch 2.1.1 --recursive --single-branch https://github.com/raspberrypi/pico-sdk.git

# pico-sdk hotfix
sed -i '1i#include <cstdint>' pico-sdk/tools/pioasm/pio_assembler.h
sed -i '1i#include <cstdint>' pico-sdk/tools/pioasm/output_format.h
sed -i '1i#include <cstdint>' pico-sdk/tools/pioasm/pio_types.h
