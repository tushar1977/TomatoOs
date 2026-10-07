#!/bin/bash
set -e

if [ ! -d "limine" ]; then
  git clone https://github.com/limine-bootloader/limine.git --branch=v11.x-binary --depth=1
fi

if [ ! -d "src/uacpi" ]; then
  ./uACPI.sh
fi
make clean
make all

mkdir -p sysroot
mkdir -p sysroot/boot
cp -v bin/myos sysroot/boot/
mkdir -p sysroot/boot/limine
cp -v limine.conf limine/limine-bios.sys limine/limine-bios-cd.bin \
  limine/limine-uefi-cd.bin sysroot/boot/limine/

mkdir -p sysroot/EFI/BOOT
cp -v limine/BOOTX64.EFI sysroot/EFI/BOOT/
cp -v limine/BOOTIA32.EFI sysroot/EFI/BOOT/

xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
  -no-emul-boot -boot-load-size 4 -boot-info-table \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  -efi-boot-part --efi-boot-image --protective-msdos-label \
  sysroot -o myos.iso

./limine/limine bios-install myos.iso

if [ ! -f disk.img ]; then
  qemu-img create -f raw disk.img 6G
fi

if [ "$1" == "debug" ]; then
  qemu-system-x86_64 myos.iso \
    -s -S \
    -d int,cpu_reset \
    -no-reboot \
    -m 1G \
    -smp 4,sockets=1,cores=4,threads=1 \
    -mem-prealloc \
    -drive file=disk.img,format=raw,if=none,id=nvme0 \
    -device nvme,drive=nvme0,serial=MYOSNVME
else
  qemu-system-x86_64 myos.iso \
    -s \
    -d int,cpu_reset \
    -m 1G \
    -smp 4,sockets=1,cores=4,threads=1 \
    -mem-prealloc \
    -drive file=disk.img,format=raw,if=none,id=nvme0 \
    -device nvme,drive=nvme0,serial=MYOSNVME
fi
