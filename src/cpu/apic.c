#include "../include/apic.h"
#include "../include/acpi.h"
#include "../include/flanterm.h"
#include "../include/flanterm_write.h"
#include "../include/kernel.h"
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
  *(volatile uint32_t *)ioapic_addr = reg;
  *(volatile uint32_t *)(ioapic_addr + 0x10) = value;
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

  uint64_t ioapic_info = (lapic << 56) | flags | (vector & 0xFF);

  if (kernel.iso) {
    char polarity = (kernel.iso->flags & 0b11) == 0b11 ? 1 : 0;
    char mode = ((kernel.iso->flags >> 2) & 0b11) == 0b11 ? 1 : 0;
    ioapic_info = ((uint64_t)lapic << 56) | (mode << 15) | (polarity << 13) |
                  (vector & 0xFF) | flags;
  }

  uint32_t apic_ver = read_ioapic((void *)kernel.ioapic_addr, 1);
  uint32_t max_redirectional_entry = apic_ver >> 16;

  uint32_t irq_register =
      ((irq - kernel.ioapic_device.global_system_interrupt_base) * 2) + 0x10;

  write_ioapic((void *)kernel.ioapic_addr, irq_register, (uint32_t)ioapic_info);
  write_ioapic((void *)kernel.ioapic_addr, irq_register + 1,
               (uint32_t)((uint64_t)ioapic_info >> 32));
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

  uint32_t tpr = read_lapic(lapic_addr, LAPIC_TASK_PRIORITY_REGISTER);
  kprintf("LAPIC Task Priority Register: %x\n", tpr);

  uint32_t dfr = read_lapic(lapic_addr, LAPIC_DESTINATION_FORMAT_REGISTER);
  kprintf("LAPIC Destination Format Register: %x\n", dfr);

  uint32_t svr =
      read_lapic(lapic_addr, LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER);
  kprintf("LAPIC Spurious Interrupt Vector Register: %x\n", svr);
  k_debug("This LAPIC was successfully set up!");
}

void init_apic() {
  k_debug("Initiating APIC...\n");
  disableLegacyPIC();
  if (!verify_apic()) {

    k_debug("This device does not support APIC, but rather only legacy PIC. "
            "Halting.\n");
    halt();
  }

  MADT *madt = (MADT *)find_MADT(kernel.rsdt);
  kprintf("Local APIC paddr: %x\n", madt->local_apic_addr);

  map_page((uint64_t)madt->local_apic_addr + kernel.hhdm,
           (uint64_t)madt->local_apic_addr,
           KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE, 1);

  kernel.lapic_base = (uint64_t)madt->local_apic_addr + kernel.hhdm;
  init_local_apic(kernel.lapic_base);

  uint64_t offset = sizeof(MADT);
  uint64_t madt_end = madt->header.length;

  while (offset < madt_end) {
    MADTEntryHeader *entry = (MADTEntryHeader *)(((uint64_t)madt) + offset);

    switch (entry->entry_type) {
    case APIC_TYPE_IO: {
      IOApic *ioapic = (IOApic *)entry;

      k_debug("I/O APIC device found. Information:");
      kprintf(" - I/O APIC ID: %d\n", ioapic->ioapic_id);
      kprintf(" - I/O APIC address: 0x%x\n", ioapic->ioapic_addr);
      kprintf(" - Global system interrupt base: %d\n",
              ioapic->global_system_interrupt_base);

      kernel.ioapic_device = *ioapic;
      kernel.ioapic_addr = ioapic->ioapic_addr;

      break;
    }

    case APIC_TYPE_LOCAL: {
      ProcessorLocalAPIC *lapic = (ProcessorLocalAPIC *)entry;
      k_debug("Processor local APIC device found. Information");
      kprintf(" - Processor ID: %d\n", lapic->processor_id);
      kprintf(" - APIC ID: %d\n", lapic->apic_id);
      break;
    }

    case APIC_TYPE_IO_OVERRIDE: {
      IOApicInterruptSourceOverride *iso =
          (IOApicInterruptSourceOverride *)entry;

      kprintf("IRQ Override: IRQ %d → GSI %d, flags: %x\n", iso->irq_source,
              iso->global_system_interrupt, iso->flags);
      kernel.irq_overrides[iso->irq_source] = iso->global_system_interrupt;
      kernel.iso = iso;
    }
    }

    offset += entry->record_length;
  }
}
