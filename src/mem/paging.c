#include "../include/paging.h"
#include "../include/kernel.h"
#include "../include/limine.h"
#include "../include/pmm.h"
#include "../include/stddef.h"
#include "../include/string.h"
#include <stdint.h>

static PageTable *PML4;

extern uint64_t p_kernel_start[];
extern uint64_t p_writeallowed_start[];
extern uint64_t p_kernel_end[];

static inline void flushTLB(void *page) {
  __asm__ volatile("invlpg (%0)" ::"r"(page) : "memory");
}

uint64_t readCR3(void) {
  uint64_t val;
  __asm__ volatile("mov %%cr3, %0" : "=r"(val));
  return val;
}

void map_kernel() {

  uint64_t p_kernel = kernel.kernel_addr.physical_base;
  uint64_t v_kernel = kernel.kernel_addr.virtual_base;

  uint64_t offset = v_kernel - p_kernel;

  uint64_t phys_kernel_start = (uint64_t)p_kernel_start - offset;
  uint64_t phys_writeallowed_start = (uint64_t)p_writeallowed_start - offset;
  uint64_t phys_kernel_end = (uint64_t)p_kernel_end - offset;

  uint64_t readonly_size =
      (uint64_t)p_writeallowed_start - (uint64_t)p_kernel_start;
  uint64_t writable_size =
      (uint64_t)p_kernel_end - (uint64_t)p_writeallowed_start;

  size_t readonly_pages = (readonly_size + 4095) / 4096;
  size_t writable_pages = (writable_size + 4095) / 4096;
  map_page((uint64_t)p_kernel_start, phys_kernel_start, KERNEL_PFLAG_PRESENT,
           readonly_pages);

  map_page((uint64_t)p_writeallowed_start, phys_writeallowed_start,
           KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE, writable_pages);
}

void map_sections() {
  uint64_t num_memmap = kernel.memmap.entry_count;
  struct limine_memmap_entry **memmap_entries = kernel.memmap.entries;

  for (size_t i = 0; i < num_memmap; i++) {
    struct limine_memmap_entry *entry = memmap_entries[i];
    uint64_t entry_type = entry->type;

    if (entry_type == LIMINE_MEMMAP_USABLE ||
        entry_type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
        entry_type == LIMINE_MEMMAP_FRAMEBUFFER ||
        entry_type == LIMINE_MEMMAP_KERNEL_AND_MODULES ||
        entry_type == LIMINE_MEMMAP_ACPI_NVS ||
        entry_type == LIMINE_MEMMAP_ACPI_RECLAIMABLE) {

      uint64_t page_count = (entry->length + 4095) / 4096;

      if (page_count == 0)
        continue;

      uint64_t phys_addr = entry->base & ~0xFFFULL; // Align to page boundary
      uint64_t virt_addr = phys_addr + kernel.hhdm;

      map_page(virt_addr, phys_addr, KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE,
               page_count);
    }
  }
}
PageTable *initPML4() {
  uintptr_t cr3 = (uintptr_t)readCR3();
  PML4 = (PageTable *)((cr3 >> 12) << 12);
  map_kernel();

  kernel.cr3 = (uint64_t)(PML4 + kernel.hhdm) - kernel.hhdm;
  return PML4;
}
void setPageTableEntry(PageEntry *entry, uint8_t flags,
                       uintptr_t physical_address, uint16_t available) {
  entry->present = (flags & 1) ? 1 : 0;
  entry->writable = (flags & 2) ? 1 : 0;
  entry->user_accessible = (flags & 4) ? 1 : 0;
  entry->write_through_caching = (flags & 8) ? 1 : 0;
  entry->disable_cache = (flags & 16) ? 1 : 0;
  entry->accessed = 0;
  entry->dirty = 0;
  entry->global = (flags & 256) ? 1 : 0;
  entry->avl1 = available & 0x3;
  entry->physical_address = physical_address >> 12;
  entry->avl2 = available >> 3;
  entry->no_execute = (flags & 512) ? 1 : 0;
}
void *getPhysicalAddress(void *virtual_address) {

  uintptr_t virt_add = (uintptr_t)virtual_address;

  uint64_t offset = virt_add & 0xFFF;
  uint64_t pt_idx = (virt_add >> 12) & 0x1FF;
  uint64_t pd_idx = (virt_add >> 21) & 0x1FF;
  uint64_t pdp_idx = (virt_add >> 30) & 0x1FF;
  uint64_t pml4_idx = (virt_add >> 39) & 0x1FF;

  PageTable *page_directory_pointer =
      (PageTable *)((uint64_t)(PML4->entries[pml4_idx].physical_address) << 12);
  PageTable *page_directory =
      (PageTable *)((uint64_t)(page_directory_pointer->entries[pdp_idx]
                                   .physical_address)
                    << 12);
  PageTable *page_table =
      (PageTable *)((uint64_t)(page_directory->entries[pd_idx].physical_address)
                    << 12);

  return (void *)((page_table->entries[pt_idx].physical_address << 12) +
                  offset);
}

static void allocateEntry(PageTable *table, size_t index, uint8_t flags,
                          size_t size) {
  void *physical_page = k_malloc(size);
  setPageTableEntry(&table->entries[index], flags | PTE_PRESENT | PTE_WRITABLE,
                    (uintptr_t)physical_page, 0);
}
void map_page(uint64_t virt, uint64_t phys, uint64_t flags, size_t size) {
  uintptr_t vir = (uintptr_t)virt;
  uintptr_t phy = (uintptr_t)phys;

  uint64_t pml4_idx = (vir >> 39) & 0x1FF;
  uint64_t pdp_idx = (vir >> 30) & 0x1FF;
  uint64_t pd_idx = (vir >> 21) & 0x1FF;
  uint64_t pt_idx = (vir >> 12) & 0x1FF;

  if (!PML4->entries[pml4_idx].present) {
    allocateEntry(PML4, pml4_idx, 0, size);
  }
  PageTable *pdp =
      (PageTable *)((PML4->entries[pml4_idx].physical_address << 12));
  if (!pdp->entries[pdp_idx].present) {
    allocateEntry(pdp, pdp_idx, 0, size);
  }
  PageTable *pd = (PageTable *)((pdp->entries[pdp_idx].physical_address << 12));
  if (!pd->entries[pd_idx].present) {
    allocateEntry(pd, pd_idx, 0, size);
  }
  PageTable *pt = (PageTable *)((pd->entries[pd_idx].physical_address << 12));
  setPageTableEntry(&pt->entries[pt_idx], flags, phy, 0);

  flushTLB((void *)virt);
}
