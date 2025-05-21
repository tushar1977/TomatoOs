#include "../../include/acpi.h"
#include "../../include/flanterm.h"
#include "../../include/kernel.h"
#include "../../include/paging.h"
#include "../../include/printf.h"
#include "../../include/string.h"
void init_acpi() {
  if (kernel.rsdp_table->revision != 0) {
    halt();
  }

  map_page((uint64_t)kernel.rsdp_table->rsdt_address + kernel.hhdm,
           (uint64_t)kernel.rsdp_table->rsdt_address, KERNEL_PFLAG_PRESENT, 1);

  RSDT *rsdt = (RSDT *)(kernel.rsdp_table->rsdt_address + kernel.hhdm);
  uint64_t rsdt_num_entries = (rsdt->header.length - sizeof(rsdt->header)) / 4;
  kernel.rsdt = rsdt;

  k_debug("Initialising ACPI...");
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
