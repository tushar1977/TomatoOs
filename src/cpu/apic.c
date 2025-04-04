#include "../include/apic.h"
#include "../include/acpi.h"
#include "../include/flanterm.h"
#include "../include/flanterm_write.h"
#include "../include/kernel.h"
#include "../include/paging.h"
#include "../include/string.h"
#include "../include/util.h"
#include "stdbool.h"

bool verify_apic() {
  uint32_t eax, edx;
  cpuid(1, &eax, &edx);
  return edx & (1 << 9);
}

void write_lapic(uintptr_t lapic_addr, uint64_t reg_offset, uint32_t val) {
  uint32_t volatile *lapic_register_addr =
      (uint32_t volatile *)(((uint64_t)lapic_addr) + reg_offset);
  *lapic_register_addr = val;
}

void init_local_apic(uintptr_t lapic_addr) {

  write_lapic(lapic_addr, LAPIC_TASK_PRIORITY_REGISTER, 0);
  write_lapic(lapic_addr, LAPIC_DESTINATION_FORMAT_REGISTER, 0xF0000000);
  write_lapic(lapic_addr, LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER,
              0xFF | 0x100);
}

void init_apic() {
  if (!verify_apic()) {
    halt();
  }

  outPortB(0x21, 0xFF);
  outPortB(0xA1, 0xFF);
  outPortB(0x20, 0xFF);
  outPortB(0xA0, 0xFF);

  MADT *madt = (MADT *)find_MADT(kernel.rsdt);

  map_page((uint64_t)madt->local_apic_addr + kernel.hhdm,
           (uint64_t)madt->local_apic_addr,
           KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE,
           ((sizeof((uint64_t)madt) + 4095) / 4096));

  kernel.lapic_base = (uint64_t)madt->local_apic_addr + kernel.hhdm;
  init_local_apic(kernel.lapic_base);

  MADTEntryHeader *entry = (MADTEntryHeader *)(((uint64_t)madt) + sizeof(MADT));
  uint64_t incremented = sizeof(MADT);

  while (incremented < madt->header.length) {
    if (entry->entry_type == IOAPIC) {
      IOApic *ioapic = (IOApic *)entry;
      map_page((uint64_t)ioapic->ioapic_addr, ioapic->ioapic_addr - kernel.hhdm,
               KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE,
               (sizeof((uint64_t)ioapic->ioapic_addr) + 4095) / 4096);

      kernel.ioapic_addr = ioapic->ioapic_addr;
      kernel.ioapic_device = *ioapic;
    } else if (entry->entry_type == LOCAL_APIC) {

      ProcessorLocalAPIC *this_local_apic = (ProcessorLocalAPIC *)entry;
    }

    entry = (MADTEntryHeader *)(((uint64_t)entry) + entry->record_length);
    incremented += entry->record_length;
  }
}
