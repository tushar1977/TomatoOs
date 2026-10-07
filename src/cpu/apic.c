#include "../include/apic.h"
#include "../include/acpi.h"
#include "../include/flanterm.h"
#include "../include/kernel.h"
#include "../include/klog.h"
#include "../include/paging.h"
#include "../include/printf.h"
#include "../include/string.h"
#include "../include/util.h"
#include "stdbool.h"

bool verify_apic() {
  uint32_t eax, edx;
  cpuid(1, &eax, &edx);
  return edx & (1 << 9);
}

uint32_t read_ioapic(void *ioapic_addr, uint32_t reg) {
  uint64_t virt = (uint64_t)(ioapic_addr + kernel.hhdm);
  *(volatile uint32_t *)virt = reg;
  return *(volatile uint32_t *)(virt + 0x10);
}

void write_ioapic(void *ioapic_addr, uint32_t reg, uint32_t value) {
  uint64_t virt = (uint64_t)(ioapic_addr) + kernel.hhdm;
  *(volatile uint32_t *)virt = reg;
  *(volatile uint32_t *)(virt + 0x10) = value;
}

uint32_t read_lapic(uintptr_t lapic_base, uint32_t reg) {
  return *(volatile uint32_t *)(lapic_base + reg);
}
void write_lapic(uintptr_t lapic_addr, uint64_t reg_offset, uint32_t val) {
  uint32_t volatile *lapic_register_addr =
      (uint32_t volatile *)(((uint64_t)lapic_addr) + reg_offset);
  *lapic_register_addr = val;
}

void set_ioapic_entry(uint8_t vector, uint8_t irq, uint64_t flags,
                      uint64_t lapic) {
  uint64_t ioapic_info = ((uint64_t)lapic << 56) | (vector & 0xFF);
  ioapic_info &= ~(1ULL << 16);
  ioapic_info &= ~(1ULL << 15);
  ioapic_info &= ~(1ULL << 13);

  uint32_t irq_register =
      ((irq - kernel.ioapic_device.global_system_interrupt_base) * 2) + 0x10;

  write_ioapic((void *)kernel.ioapic_addr, irq_register, (uint32_t)ioapic_info);
  write_ioapic((void *)kernel.ioapic_addr, irq_register + 1,
               (uint32_t)(ioapic_info >> 32));
}

void unmask_ioapic(uint32_t gsi, uint32_t lapic_id) {
  uintptr_t ioapic_addr = (uintptr_t)kernel.ioapic_device.ioapic_addr;
  uint32_t entry = gsi * 2;
  uint32_t reg_low = 0x10 + entry;

  uint32_t low = read_ioapic((void *)ioapic_addr, reg_low);
  low &= ~(1 << 16);
  write_ioapic((void *)ioapic_addr, reg_low, low);
}

void mask_ioapic(uint32_t gsi, uint32_t lapic_id) {
  uintptr_t ioapic_addr = (uintptr_t)kernel.ioapic_device.ioapic_addr;
  uint32_t entry = gsi * 2;
  uint32_t reg_low = 0x10 + entry;

  uint32_t low = read_ioapic((void *)ioapic_addr, reg_low);
  low |= (1 << 16); // Set mask bit
  write_ioapic((void *)ioapic_addr, reg_low, low);
}
void end_of_interrupt() {
  write_lapic(kernel.lapic_base, LAPIC_EOI_REGISTER, 0);
}

void init_local_apic(uintptr_t lapic_addr) {

  write_lapic(lapic_addr, LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER, 0x1FF);
  write_lapic(lapic_addr, LAPIC_TASK_PRIORITY_REGISTER, 0x00);
  write_lapic(lapic_addr, LAPIC_DESTINATION_FORMAT_REGISTER, 0xFFFFFFFF);

  klog(KLOG_INFO, "APIC");
  kprintf("Local APIC configured at %x\n", lapic_addr);
}

void *find_MADT(RSDT *root_rsdt) {
  uint64_t num_entries =
      (root_rsdt->header.length - sizeof(root_rsdt->header)) / 4;
  for (size_t i = 0; i < num_entries; i++) {
    ISDTHeader *this_header =
        (ISDTHeader *)(root_rsdt->entries[i] + kernel.hhdm);

    if (memcmp(this_header->signature, "APIC", 4) == 0)
      return (void *)this_header;
  }

  return NULL;
}

void init_apic() {
  if (!kernel.rsdp_table) {
    klog(KLOG_ERROR, "APIC");
    kprintf("RSDP table is NULL\n");
    halt();
  }

  kernel.rsdt = (RSDT *)(kernel.rsdp_table->rsdt_address + kernel.hhdm);

  if (!kernel.rsdt) {
    klog(KLOG_ERROR, "APIC");
    kprintf("RSDT is NULL\n");
    halt();
  }

  if (!verify_apic()) {
    klog(KLOG_ERROR, "APIC");
    kprintf("APIC not supported by this CPU\n");
    halt();
  }

  MADT *madt = (MADT *)find_MADT(kernel.rsdt);
  if (!madt) {
    klog(KLOG_ERROR, "APIC");
    kprintf("MADT not found\n");
    halt();
  }

  uint64_t lapic_phys = madt->local_apic_addr;
  kernel.lapic_base = lapic_phys + kernel.hhdm;

  map_page(kernel.lapic_base, lapic_phys,
           KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE, 1);

  init_local_apic(kernel.lapic_base);

  uint64_t offset = sizeof(MADT);
  uint64_t madt_end = madt->header.length;

  for (int i = 0; i < 16; i++) {
    kernel.irq_overrides[i] = i;
  }

  int entry_index = 0;
  int ioapic_count = 0;

  while (offset < madt_end) {
    MADTEntryHeader *entry = (MADTEntryHeader *)(((uint64_t)madt) + offset);

    /*
     * Extremely important sanity checks.
     */
    if (entry->record_length < sizeof(MADTEntryHeader) ||
        offset + entry->record_length > madt_end) {
      klog(KLOG_ERROR, "APIC");
      kprintf("malformed MADT entry at offset %d\n", offset);
      halt();
    }

    switch (entry->entry_type) {

    case APIC_TYPE_IO: {
      IOApic *ioapic = (IOApic *)entry;

      kernel.ioapic_device = *ioapic;
      kernel.ioapic_addr = ioapic->ioapic_addr;

      uint64_t ioapic_phys = ioapic->ioapic_addr;
      uint64_t ioapic_virt = ioapic_phys + kernel.hhdm;

      map_page(ioapic_virt, ioapic_phys,
               KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE, 1);

      ioapic_count++;
      break;
    }

    case APIC_TYPE_IO_OVERRIDE: {
      IOApicInterruptSourceOverride *iso =
          (IOApicInterruptSourceOverride *)entry;

      if (iso->irq_source < 16) {
        kernel.irq_overrides[iso->irq_source] = iso->global_system_interrupt;
      } else {
        klog(KLOG_WARN, "APIC");
        kprintf("interrupt source override irq %d >= 16, ignoring\n",
                iso->irq_source);
      }

      kernel.iso = iso;

      break;
    }

    default:
      break;
    }

    offset += entry->record_length;
    entry_index++;

    /*
     * Prevent an accidental infinite loop on real hardware.
     */
    if (entry_index > 256) {
      klog(KLOG_ERROR, "APIC");
      kprintf("too many MADT entries, aborting\n");
      halt();
    }
  }

  klog(KLOG_INFO, "APIC");
  kprintf("initialized, %d I/O APIC(s) found\n", ioapic_count);
}
