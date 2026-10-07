#ifndef KERNEL_H
#define KERNEL_H
#include "acpi.h"
#include "apic.h"
#include "fb.h"
#include "flanterm.h"
#include "gdt.h"
#include "idt.h"
#include "limine.h"
#include "pci.h"
#include "stdint.h"

#define MAX_PCI_DEVICES 32
#define MAX_PCI_DRIVERS 256

typedef struct {
  int used;
  pci_t header;
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  void *driver_data;
} pci_device_entry_t;

typedef struct {
  struct limine_framebuffer **framebuffer;
  struct limine_memmap_response memmap;
  struct limine_executable_address_response kernel_addr;
  struct limine_executable_file_response kernel_file;
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
  volatile uint64_t apic_ticks;
  uint32_t lapic_ticks_per_10ms;

  pci_device_entry_t pci_devices[MAX_PCI_DEVICES];
  int pci_device_count;

  pci_driver_t pci_drivers[MAX_PCI_DRIVERS];
  int last_pci_drv;

  struct nvme_controller *nvme_controllers[256];
  int nvme_controller_ptr;
} Kernel;

extern Kernel kernel;
#endif // KERNEL_H
