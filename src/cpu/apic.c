#include "../include/apic.h"
#include "../include/acpi.h"
#include "../include/flanterm.h"
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
  k_debug("This LAPIC was successfully set up!");
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
  k_debug("[APIC] === BEGIN init_apic ===");

  k_debug("[APIC] Checking RSDP...");
  kprintf("[APIC] RSDP address: %x\n", kernel.rsdp_table);

  if (!kernel.rsdp_table) {
    k_debug("[APIC] ERROR: kernel.rsdp_table is NULL");
    halt();
  }

  k_debug("[APIC] RSDP looks non-null");

  k_debug("[APIC] Calculating RSDT address...");
  kprintf("[APIC] RSDT physical: %x\n", kernel.rsdp_table->rsdt_address);
  kprintf("[APIC] HHDM: %x\n", kernel.hhdm);

  kernel.rsdt = (RSDT *)(kernel.rsdp_table->rsdt_address + kernel.hhdm);

  kprintf("[APIC] RSDT virtual: %x\n", kernel.rsdt);

  if (!kernel.rsdt) {
    k_debug("[APIC] ERROR: RSDT is NULL");
    halt();
  }

  k_debug("[APIC] Calling verify_apic()...");

  bool apic_supported = verify_apic();

  kprintf("[APIC] verify_apic() returned: %d\n", apic_supported);

  if (!apic_supported) {
    k_debug("[APIC] ERROR: APIC not supported");
    halt();
  }

  k_debug("[APIC] Calling find_MADT()...");

  MADT *madt = (MADT *)find_MADT(kernel.rsdt);

  kprintf("[APIC] find_MADT returned: %x\n", madt);

  if (!madt) {
    k_debug("[APIC] ERROR: MADT not found");
    halt();
  }

  k_debug("[APIC] MADT found");

  /* signature is a 4-byte, non-null-terminated field; copy it into a
   * null-terminated buffer so we can print it with %s */
  char madt_sig[5];
  madt_sig[0] = madt->header.signature[0];
  madt_sig[1] = madt->header.signature[1];
  madt_sig[2] = madt->header.signature[2];
  madt_sig[3] = madt->header.signature[3];
  madt_sig[4] = '\0';

  kprintf("[APIC] MADT signature: %s\n", madt_sig);
  kprintf("[APIC] MADT length: %d\n", madt->header.length);
  kprintf("[APIC] MADT revision: %d\n", madt->header.revision);

  k_debug("[APIC] Reading MADT local APIC address...");

  uint64_t lapic_phys = madt->local_apic_addr;

  kprintf("[APIC] Local APIC physical: %x\n", lapic_phys);
  kprintf("[APIC] Local APIC virtual: %x\n", lapic_phys + kernel.hhdm);

  kernel.lapic_base = lapic_phys + kernel.hhdm;

  kprintf("[APIC] kernel.lapic_base = %x\n", kernel.lapic_base);

  k_debug("[APIC] Mapping Local APIC...");

  map_page(kernel.lapic_base, lapic_phys,
           KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE, 1);

  k_debug("[APIC] Local APIC mapping completed");

  k_debug("[APIC] Calling init_local_apic()...");

  init_local_apic(kernel.lapic_base);

  k_debug("[APIC] init_local_apic() completed");

  k_debug("[APIC] Initializing IRQ override table...");

  uint64_t offset = sizeof(MADT);
  uint64_t madt_end = madt->header.length;

  kprintf("[APIC] MADT start: %x\n", madt);
  kprintf("[APIC] MADT length: %d\n", madt_end);
  kprintf("[APIC] Initial entry offset: %d\n", offset);

  for (int i = 0; i < 16; i++) {
    kernel.irq_overrides[i] = i;
  }

  k_debug("[APIC] IRQ override table initialized");

  int entry_index = 0;

  while (offset < madt_end) {

    kprintf("[APIC] Processing MADT entry #%d\n", entry_index);
    kprintf("[APIC] Entry offset: %d / %d\n", offset, madt_end);

    MADTEntryHeader *entry = (MADTEntryHeader *)(((uint64_t)madt) + offset);

    kprintf("[APIC] Entry address: %x\n", entry);

    kprintf("[APIC] Entry type: %d\n", entry->entry_type);

    kprintf("[APIC] Entry length: %d\n", entry->record_length);

    /*
     * Extremely important sanity checks.
     */
    if (entry->record_length < sizeof(MADTEntryHeader)) {
      k_debug("[APIC] ERROR: Invalid MADT entry length!");
      kprintf("[APIC] record_length = %d\n", entry->record_length);
      halt();
    }

    if (offset + entry->record_length > madt_end) {
      k_debug("[APIC] ERROR: MADT entry extends past MADT!");
      kprintf("[APIC] offset=%d length=%d end=%d\n", offset,
              entry->record_length, madt_end);
      halt();
    }

    switch (entry->entry_type) {

    case APIC_TYPE_IO: {
      k_debug("[APIC] Found I/O APIC entry");

      IOApic *ioapic = (IOApic *)entry;

      kprintf("[APIC] I/O APIC ID: %d\n", ioapic->ioapic_id);

      kprintf("[APIC] I/O APIC physical address: %x\n", ioapic->ioapic_addr);

      kprintf("[APIC] GSI base: %d\n", ioapic->global_system_interrupt_base);

      kernel.ioapic_device = *ioapic;
      kernel.ioapic_addr = ioapic->ioapic_addr;

      uint64_t ioapic_phys = ioapic->ioapic_addr;
      uint64_t ioapic_virt = ioapic_phys + kernel.hhdm;

      kprintf("[APIC] I/O APIC virtual address: %x\n", ioapic_virt);

      k_debug("[APIC] Mapping I/O APIC...");

      map_page(ioapic_virt, ioapic_phys,
               KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE, 1);

      k_debug("[APIC] I/O APIC mapping completed");

      break;
    }

    case APIC_TYPE_LOCAL: {
      k_debug("[APIC] Found processor Local APIC entry");

      ProcessorLocalAPIC *lapic = (ProcessorLocalAPIC *)entry;

      kprintf("[APIC] Processor ID: %d\n", lapic->processor_id);

      kprintf("[APIC] APIC ID: %d\n", lapic->apic_id);

      break;
    }

    case APIC_TYPE_IO_OVERRIDE: {
      k_debug("[APIC] Found Interrupt Source Override");

      IOApicInterruptSourceOverride *iso =
          (IOApicInterruptSourceOverride *)entry;

      kprintf("[APIC] IRQ source: %d\n", iso->irq_source);

      kprintf("[APIC] GSI: %d\n", iso->global_system_interrupt);

      kprintf("[APIC] Flags: %x\n", iso->flags);

      if (iso->irq_source < 16) {
        kernel.irq_overrides[iso->irq_source] = iso->global_system_interrupt;
      } else {
        k_debug("[APIC] WARNING: IRQ source >= 16");
      }

      kernel.iso = iso;

      break;
    }

    default:
      kprintf("[APIC] Unknown MADT entry type: %d\n", entry->entry_type);
      break;
    }

    offset += entry->record_length;

    kprintf("[APIC] Next MADT offset: %d\n", offset);

    entry_index++;

    /*
     * Prevent an accidental infinite loop on real hardware.
     */
    if (entry_index > 256) {
      k_debug("[APIC] ERROR: Too many MADT entries");
      halt();
    }
  }

  k_debug("[APIC] === APIC Done ===");
}
