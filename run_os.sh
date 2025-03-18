#!/bin/sh
set -e
make clean
make all
qemu-system-i386 -s -cdrom image.iso
