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
  uint32_t volatile *ioapic = (uint32_t volatile *)ioapic_addr;
  ioapic[0] = (reg & 0xff);
  return ioapic[4];
}

void write_ioapic(void *ioapic_addr, uint32_t reg, uint32_t value) {
  uint32_t volatile *ioapic = (uint32_t volatile *)ioapic_addr;
  ioapic[0] = (reg & 0xff);
  ioapic[4] = value;
}

uint32_t read_lapic(uintptr_t lapic_addr, uint64_t reg_offset) {
  uint32_t volatile *lapic_register_addr =
      (uint32_t volatile *)(((uint64_t)lapic_addr) + reg_offset);
  return (uint32_t)*lapic_register_addr;
}

void write_lapic(uintptr_t lapic_addr, uint64_t reg_offset, uint32_t val) {
  uint32_t volatile *lapic_register_addr =
      (uint32_t volatile *)(((uint64_t)lapic_addr) + reg_offset);
  *lapic_register_addr = val;
}

void map_ioapic(uint8_t vec, uint32_t irq, uint32_t lapic_id, bool polarity,
                bool trigger) {
  kprintf("Global system interrupt base: %d\n",
          kernel.ioapic_device.global_system_interrupt_base);
  uintptr_t ioapic_addr =
      (uintptr_t)(((uint64_t)kernel.ioapic_device.ioapic_addr));
  uint32_t gsi_base = kernel.ioapic_device.global_system_interrupt_base;

  kprintf("GSI for keyboard: %d\n", gsi_base + irq);
  uint32_t entry_num = gsi_base + (irq * 2);
  kprintf("Entry number: %d\n", entry_num);
  uint32_t reg_nums[2] = {0x10 + entry_num, 0x11 + entry_num};
  kprintf("Register numbers: %d and %d\n", reg_nums[0], reg_nums[1]);
  uint32_t redirection_entries[2] = {
      read_ioapic((void *)ioapic_addr, reg_nums[0]),
      read_ioapic((void *)ioapic_addr, reg_nums[1])};

  redirection_entries[0] =
      (redirection_entries[0] & ~0xFF) | vec; // set vector number
  redirection_entries[0] &= ~0x700;           // set delivery mode to normal
  redirection_entries[0] &=
      ~0x800; // set destination mode to physical. Probably worse but for now
              // it's just easier.
  if (polarity)
    redirection_entries[0] |= 0x2000; // set polarity to low
  else
    redirection_entries[0] &= ~0x2000; // set polarity to high
  if (trigger)
    redirection_entries[0] |= 0x8000; // set trigger to level
  else
    redirection_entries[0] &= ~0x8000; // set trigger to edge
  redirection_entries[0] &= ~0x10000;  // makes sure that it's unmasked

  kprintf("Done! New value: 0x%x\n", redirection_entries[0]);

  kprintf("Trying to set entry two...\n");
  redirection_entries[1] = (lapic_id & 0xF) << 28;
  kprintf("Done, new value: 0x%x\n", redirection_entries[1]);
  kprintf("Trying to set new entries...\n");
  write_ioapic((void *)ioapic_addr, reg_nums[0], redirection_entries[0]);
  write_ioapic((void *)ioapic_addr, reg_nums[1], redirection_entries[1]);
  k_debug("Done, this IOAPIC IRQ has been mapped and unmasked.");
}

void mask_ioapic(uint8_t irq, uint32_t lapic_id) {
  uintptr_t ioapic_addr =
      (uintptr_t)(((uint64_t)kernel.ioapic_device.ioapic_addr) + kernel.hhdm);
  uint32_t gsi_base = kernel.ioapic_device.global_system_interrupt_base;
  uint32_t entry_num = gsi_base + (irq * 2);
  uint32_t reg_num = 0x10 + entry_num;
  uint32_t redirection_entry = read_ioapic((void *)ioapic_addr, reg_num);
  redirection_entry |= 0x10000;
  write_ioapic((void *)ioapic_addr, reg_num, redirection_entry);
}

void unmask_ioapic(uint8_t irq, uint32_t lapic_id) {
  uintptr_t ioapic_addr =
      (uintptr_t)(((uint64_t)kernel.ioapic_device.ioapic_addr) + kernel.hhdm);
  uint32_t gsi_base = kernel.ioapic_device.global_system_interrupt_base;
  uint32_t entry_num = gsi_base + (irq * 2);
  uint32_t reg_num = 0x10 + entry_num;
  uint32_t redirection_entry = read_ioapic((void *)ioapic_addr, reg_num);
  redirection_entry &= ~0x10000;
  write_ioapic((void *)ioapic_addr, reg_num, redirection_entry);
}

void end_of_interrupt() {
  write_lapic(kernel.lapic_base, LAPIC_END_OF_INTERRUPT_REGISTER, 0);
  enable_interrupts();
}

void init_local_apic(uintptr_t lapic_addr) {

  k_debug("Setting task priority of LAPIC...");
  write_lapic(lapic_addr, LAPIC_TASK_PRIORITY_REGISTER, 0);

  k_debug("Setting LAPIC destination format to flat mode...");
  write_lapic(lapic_addr, LAPIC_DESTINATION_FORMAT_REGISTER, 0xF0000000);

  k_debug("Setting spurious interrupt vector (and enabling this LAPIC)...");
  write_lapic(lapic_addr, LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER,
              0xFF | 0x100);
  k_debug("This LAPIC was successfully set up!");
}

void init_apic() {
  k_debug("Initiating APIC...\n");
  outPortB(0x21, 0xff);
  outPortB(0xA1, 0xff);

  if (!verify_apic()) {

    k_debug("This device does not support APIC, but rather only legacy PIC. "
            "Halting.\n");
    halt();
  }

  MADT *madt = (MADT *)find_MADT(kernel.rsdt);
  kprintf("Local APIC paddr: 0x%x\n", madt->local_apic_addr);

  map_page((uint64_t)madt->local_apic_addr + kernel.hhdm,
           (uint64_t)madt->local_apic_addr,
           KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE,
           ((sizeof((uint64_t)madt) + 4095) / 4096));

  kernel.lapic_base = (uint64_t)madt->local_apic_addr + kernel.hhdm;
  init_local_apic(kernel.lapic_base);

  uint64_t offset = sizeof(MADT);
  uint64_t madt_end = madt->header.length;

  while (offset < madt_end) {
    MADTEntryHeader *entry = (MADTEntryHeader *)(((uint64_t)madt) + offset);

    switch (entry->entry_type) {
    case IOAPIC: {
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

    case LOCAL_APIC: {
      ProcessorLocalAPIC *lapic = (ProcessorLocalAPIC *)entry;
      k_debug("Processor local APIC device found. Information");
      kprintf(" - Processor ID: %d\n", lapic->processor_id);
      kprintf(" - APIC ID: %d\n", lapic->apic_id);
      break;
    }
    }

    offset += entry->record_length;
  }
}
