// kernel.h
#ifndef KERNEL_H
#define KERNEL_H
#include "stdint.h"
void kernel_main(uint32_t magic, struct multiboot_info *bootInfo);

#endif // KERNEL_H
