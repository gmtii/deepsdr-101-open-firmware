#!/bin/sh

# Please install dfu utils before: apt install dfu-util

dfu-util -d 28e9:0189 -a 0 -s 0x08000000:leave -D boot_nuestro.bin
