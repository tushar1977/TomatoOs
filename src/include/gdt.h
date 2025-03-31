#ifndef GDT_H
#define GDT_H

#include <stdint.h>

struct gdt_entry_struct {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_middle;
  uint8_t access;
  uint8_t limit_high : 4;
  uint8_t flags : 4;
  uint8_t base_high;

} __attribute__((packed));

struct gdt_tss_entry_struct {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_middle;
  uint8_t access;
  uint8_t limit_high : 4;
  uint8_t flags : 4;
  uint8_t base_high;
  uint32_t base_upper32;
  uint32_t reserved;

} __attribute__((packed));

struct gdt_ptr_struct {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed));

struct tss_entry_struct {
  uint64_t rsp0;
  uint16_t iopb_offset;
} __attribute__((packed));

void initGdt();
void setGdtGate(int num, uint64_t base, uint64_t limit, uint8_t access,
                uint8_t gran);

void write_tss(int num, uint64_t base, uint64_t limit, uint8_t access,
               uint8_t gran);
#endif
