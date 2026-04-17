#include "../include/paging.h"
#include "../include/kernel.h"
#include "../include/limine.h"
#include "../include/pmm.h"
#include "../include/printf.h"
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
void *phys_to_virt(uint64_t phys) { return (void *)(phys + kernel.hhdm); }
uint64_t virt_to_phys(void *virt) {
  uint64_t addr = (uint64_t)virt;

  if (addr >= kernel.hhdm) {
    return addr - kernel.hhdm;
  }

  if (addr >= kernel.kernel_addr.virtual_base) {
    return addr -
           (kernel.kernel_addr.virtual_base - kernel.kernel_addr.physical_base);
  }

  return (uint64_t)getPhysicalAddress(virt);
}
uint64_t readCR3(void) {
  uint64_t val;
  __asm__ volatile("mov %%cr3, %0" : "=r"(val));
  return val;
}

void map_kernel() {
  uint64_t phys_base = kernel.kernel_addr.physical_base;
  uint64_t virt_base = kernel.kernel_addr.virtual_base;
  uint64_t offset = virt_base - phys_base;

  uint64_t virt_ro_start = (uint64_t)p_kernel_start;
  uint64_t virt_rw_start = (uint64_t)p_writeallowed_start;
  uint64_t virt_end = (uint64_t)p_kernel_end;

  uint64_t phys_ro_start = virt_ro_start - offset;
  uint64_t phys_rw_start = virt_rw_start - offset;

  size_t readonly_pages =
      (virt_rw_start - virt_ro_start + PAGE_SIZE - 1) / PAGE_SIZE;
  size_t writable_pages =
      (virt_end - virt_rw_start + PAGE_SIZE - 1) / PAGE_SIZE;

  map_page(virt_ro_start, phys_ro_start, KERNEL_PFLAG_PRESENT, readonly_pages);

  map_page(virt_rw_start, phys_rw_start,
           KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE, writable_pages);
}

void map_all_mem() {
  for (size_t i = 0; i < kernel.memmap.entry_count; i++) {

    struct limine_memmap_entry *entry = kernel.memmap.entries[i];
    if (entry->type == LIMINE_MEMMAP_RESERVED ||
        entry->type == LIMINE_MEMMAP_BAD_MEMORY)
      continue;

    uint64_t phys = entry->base;
    uint64_t virt = phys + kernel.hhdm;
    size_t pages = (entry->length + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t flags = KERNEL_PFLAG_PRESENT | KERNEL_PFLAG_WRITE;

    map_page(virt, phys, flags, pages);
  }
  map_kernel();
}
PageTable *initPML4() {
  k_debug("Initialising Page Tables...");

  uint64_t pml4_phys = pmm_alloc_page(1);
  PML4 = phys_to_virt(pml4_phys);
  memset(PML4, 0, PAGE_SIZE);

  map_all_mem();

  kernel.cr3 = pml4_phys;
  writeCR3(pml4_phys);

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

  if (!PML4->entries[pml4_idx].present) {
    return NULL;
  }

  PageTable *pdp = phys_to_virt(PML4->entries[pml4_idx].physical_address << 12);

  if (!pdp->entries[pdp_idx].present) {
    return NULL;
  }

  PageTable *page_directory =
      phys_to_virt(pdp->entries[pdp_idx].physical_address << 12);

  if (!page_directory->entries[pd_idx].present) {
    return NULL;
  }

  PageTable *page_table =
      phys_to_virt(page_directory->entries[pd_idx].physical_address << 12);

  if (!page_table->entries[pt_idx].present) {
    return NULL;
  }

  return (void *)((page_table->entries[pt_idx].physical_address << 12) +
                  offset);
}

static uint64_t allocateEntry(PageTable *table, size_t index, uint8_t flags) {

  uint64_t phys = pmm_alloc_page(1);
  if (phys == 0) {
    kprintf("VMM: failed to allocate page table at index %d\n", index);
    return 0;
  }
  PageTable *pt = phys_to_virt(phys);

  memset(pt, 0, 4096);
  setPageTableEntry(&table->entries[index], flags | PTE_PRESENT | PTE_WRITABLE,
                    phys, 0);
  return phys;
}
void map_page(uint64_t virt, uint64_t phys, uint64_t flags, size_t num_pages) {
  for (size_t i = 0; i < num_pages; i++) {
    uint64_t current_virt = virt + (i * 4096);
    uint64_t current_phys = phys + (i * 4096);

    uintptr_t vir = (uintptr_t)current_virt;
    uintptr_t phy = (uintptr_t)current_phys;

    uint64_t pml4_idx = (vir >> 39) & 0x1FF;
    uint64_t pdp_idx = (vir >> 30) & 0x1FF;
    uint64_t pd_idx = (vir >> 21) & 0x1FF;
    uint64_t pt_idx = (vir >> 12) & 0x1FF;

    if (!PML4->entries[pml4_idx].present) {
      allocateEntry(PML4, pml4_idx, PTE_WRITABLE);
    }
    PageTable *pdp =
        phys_to_virt(PML4->entries[pml4_idx].physical_address << 12);

    if (!pdp->entries[pdp_idx].present) {
      allocateEntry(pdp, pdp_idx, PTE_WRITABLE);
    }
    PageTable *pd = phys_to_virt(pdp->entries[pdp_idx].physical_address << 12);

    if (!pd->entries[pd_idx].present) {
      allocateEntry(pd, pd_idx, PTE_WRITABLE);
    }
    PageTable *pt = phys_to_virt(pd->entries[pd_idx].physical_address << 12);

    setPageTableEntry(&pt->entries[pt_idx], flags, phy, 0);

    flushTLB((void *)current_virt);
  }
}
