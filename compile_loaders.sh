#!/bin/bash

export IDA_SDK=~/ida-sdk/src

mkdir -p ./linux_loaders
g++ -D__LINUX__ -D__IDP__ -D__EA64__ -D__X64__ -O3 -shared -fPIC -o ./linux_loaders/sega_roms_ldr.so -I$IDA_SDK/include/ -L$IDA_SDK/lib/x64_linux_gcc_64/ ./sega_roms_loader/sega_roms_ldr.cpp -lida
g++ -D__LINUX__ -D__IDP__ -D__EA64__ -D__X64__ -O3 -shared -fPIC -o ./linux_loaders/z80_loader.so    -I$IDA_SDK/include/ -L$IDA_SDK/lib/x64_linux_gcc_64/ ./z80_loader/z80_loader.cpp          -lida
