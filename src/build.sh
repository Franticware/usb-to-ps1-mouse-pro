#!/bin/bash

#export PICO_SDK_FETCH_FROM_GIT=1
export PICO_SDK_PATH=../pico-sdk

mkdir build
cd build
cmake ..
make
