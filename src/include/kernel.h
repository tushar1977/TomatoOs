// kernel.h
#ifndef KERNEL_H
#define KERNEL_H

#include "acpi.h"
#include "apic.h"
#include "fb.h"
#include "flanterm.h"
#include "gdt.h"
#include "idt.h"
#include "limine.h"
#include "stdint.h"
typedef struct {
  struct limine_framebuffer **framebuffer;
  struct limine_memmap_response memmap;
  struct limine_kernel_address_response kernel_addr;
  struct limine_kernel_file_response kernel_file;
  RSDP *rsdp_table;
  RSDT *rsdt;
  struct flanterm_context *ft_ctx;
  struct gdt_ptr_struct gdtr;
  struct Idt_ptr idtr;
  uint64_t lapic_base;
  uint64_t rsdp_address;
  uint64_t ioapic_addr;
  IOApic ioapic_device;
  IOApicInterruptSourceOverride *iso;
  uint64_t hhdm;
  uint64_t kernel_size;
  uint32_t *back_buffer;
  uint32_t front_buffer;
  uint32_t fg_colour;
  uint32_t bg_colour;
  uint64_t cr3;
  uint32_t irq_overrides[16];
  volatile uint64_t framebuffer_size;
} Kernel;

extern Kernel kernel;
#endif // KERNEL_H
