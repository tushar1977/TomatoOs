#include "../include/gdt.h"
#include "../include/kernel.h"
#include "../include/printf.h"
#include "../include/string.h"
#include <stdint.h>

extern void gdt_flush(uint64_t);
extern void tss_flush();

struct gdt_entry_struct gdt_entries[7];
struct gdt_tss_entry_struct gdt_tss_entry;
struct gdt_ptr_struct gdt_ptr;
struct tss_entry_struct tss;

void initGdt() {
  setGdtGate(0, 0, 0, 0, 0);            // Null segment
  setGdtGate(1, 0, 0xFFFFF, 0x9A, 0xA); // Kernel code segment
  setGdtGate(2, 0, 0xFFFFF, 0x92, 0xC); // Kernel data segment
  setGdtGate(3, 0, 0xFFFFF, 0xFA, 0xA); // User code segment
  setGdtGate(4, 0, 0xFFFFF, 0xF2, 0xC); // User data segment
  write_tss(5, (uint64_t)&tss, sizeof(struct tss_entry_struct) - 1, 0x89, 0x0);

  kernel.gdtr.base = (uint64_t)&gdt_entries;
  kernel.gdtr.limit = (sizeof(struct gdt_entry_struct) * 7) - 1;

  gdt_flush((uint64_t)&kernel.gdtr);
  tss_flush();
  k_debug("Initialising GDT...");
}

void write_tss(int num, uint64_t base, uint64_t limit, uint8_t access,
               uint8_t gran) {
  memset(&tss, 0, sizeof(struct tss_entry_struct));

  tss.rsp0 = 0xFFFFFFFF80000000;
  tss.iopb_offset = sizeof(struct tss_entry_struct);

  gdt_tss_entry.limit_low = (limit & 0xFFFF);
  gdt_tss_entry.base_low = (base & 0xFFFF);
  gdt_tss_entry.base_middle = (base >> 16) & 0xFF;
  gdt_tss_entry.base_high = (base >> 24) & 0xFF;
  gdt_tss_entry.base_upper32 = (base >> 32);
  gdt_tss_entry.access = access;
  gdt_tss_entry.limit_high = (limit >> 16) & 0xF;
  gdt_tss_entry.flags = gran & 0xF;
  gdt_tss_entry.reserved = 0;

  *((struct gdt_tss_entry_struct *)&gdt_entries[num]) = gdt_tss_entry;
}
void setGdtGate(int num, uint64_t base, uint64_t limit, uint8_t access,
                uint8_t gran) {
  gdt_entries[num].limit_low = (limit & 0xFFFF);

  gdt_entries[num].base_low = (base & 0xFFFF);
  gdt_entries[num].base_middle = (base >> 16) & 0xFF;
  gdt_entries[num].base_high = (base >> 24) & 0xFF;

  gdt_entries[num].flags = gran;

  gdt_entries[num].access = access;
}
