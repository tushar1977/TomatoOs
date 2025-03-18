#include "include/gdt.h"
#include "include/idt.h"
#include "include/keyboard.h"
#include "include/kprint.h"
#include "include/memory.h"
#include "include/multiboot.h"
#include "include/paging.h"
#include "include/serial.h"
#include "include/spinlock.h"
#include "include/stdio.h"
#include "include/string.h"
#include "include/syscall.h"
#include "include/tty.h"

void kernel_main(uint32_t magic, struct multiboot_info *bootInfo) {
  terminal_initialize();
  kstatusf("Terminal initialized..\n");

  kstatusf("Initializing GDT..\n");
  initGdt();

  kstatusf("Initializing IDT..\n");
  InitIdt();

  kstatusf("Initializing Syscalls..\n");
  syscall_init();

  kstatusf("Initializing Keyboard..\n");
  initKeyboard();

  uint32_t mod1 = *(uint32_t *)(bootInfo->mods_addr + 4);
  uint32_t physicsStart = (mod1 + 0xFFF) & ~0xFFF;
  kstatusf("Initializing memory with upper mem: %d KB, starting physical "
           "address: %x\n",
           bootInfo->mem_upper, physicsStart);

  for (int i = 0; i < bootInfo->mmap_length;
       i += sizeof(struct multiboot_mmap_entry)) {

    struct multiboot_mmap_entry *mmnt =
        (struct multiboot_mmap_entry *)(bootInfo->mmap_addr + i);

    printf("Low addr: %x | High add:%x | Low length: %x | High Length: %x | "
           "size: %x | Type: %d\n",
           mmnt->addr_low, mmnt->addr_high, mmnt->len_low, mmnt->len_high,
           mmnt->size, mmnt->type);
  }

  initMemory(bootInfo->mem_upper * 1024, physicsStart);

  kstatusf("Virtual File System Initialized..\n");
  printf("tusos--> ");

  for (;;) {
  }
}
