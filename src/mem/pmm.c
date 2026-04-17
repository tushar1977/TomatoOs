#include "../include/pmm.h"
#include "../include/fb.h"
#include "../include/flanterm.h"
#include "../include/kernel.h"
#include "../include/limine.h"
#include "../include/printf.h"
#include "../include/string.h"

available_memory physical;
static uint8_t *bitmap = NULL;
static uint64_t total_pages = 0;
static uint64_t bitmap_size = 0;
static uint64_t usable_base = 0;

void test_pmm() {
  uint64_t p1 = pmm_alloc_page(1);
  kprintf("P1 addr = %x\n", p1);

  uint64_t p2 = pmm_alloc_page(100);
  kprintf("P1 addr = %x\n", p2);

  pmm_free(p1, 1);

  uint64_t p3 = pmm_alloc_page(100);
  kprintf("P1 addr = %x\n", p3);
}

void init_PMM() {
  k_debug("Initialising Physical Memory Allocator...");
  struct limine_memmap_entry *largest = NULL;
  for (size_t i = 0; i < kernel.memmap.entry_count; i++) {
    struct limine_memmap_entry *entry = kernel.memmap.entries[i];
    if (entry->type == LIMINE_MEMMAP_USABLE) {
      if (!largest || entry->length > largest->length) {
        largest = entry;
      }
    }
  }
  physical.base = largest->base;
  physical.size = largest->length;
  kprintf("%x\n", physical.base);
  kprintf("%d\n", physical.size);

  total_pages = physical.size / BLOCK_SIZE;

  bitmap = (uint8_t *)(physical.base + kernel.hhdm);
  bitmap_size = (total_pages + 7) / 8;

  memset(bitmap, 0, sizeof(bitmap_size));

  uint64_t bitmap_pages = (bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
  for (uint64_t i = 0; i < bitmap_pages; i++)
    BITMAP_SET(bitmap, i);

  usable_base = physical.base + (bitmap_pages * BLOCK_SIZE);
  printMemoryMaps();

  kprintf("PMM: %d pages available (%d MB)\n", total_pages - bitmap_pages,
          (total_pages - bitmap_pages) * BLOCK_SIZE / (1024 * 1024));
}

void printMemoryMaps() {
  kprintf("Memory Map:\n");
  kprintf("--------------------------------------------------\n");

  for (size_t i = 0; i < kernel.memmap.entry_count; i++) {
    struct limine_memmap_entry *entry = kernel.memmap.entries[i];

    const char *type_str;
    switch (entry->type) {
    case LIMINE_MEMMAP_USABLE:
      type_str = "Available";
      break;
    case LIMINE_MEMMAP_RESERVED:
      type_str = "Reserved";
      break;
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
      type_str = "ACPI Recl";
      break;
    case LIMINE_MEMMAP_ACPI_NVS:
      type_str = "ACPI NVS";
      break;
    case LIMINE_MEMMAP_BAD_MEMORY:
      type_str = "Bad Mem";
      break;
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
      type_str = "Boot Recl";
      break;
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
      type_str = "Kernel";
      break;
    case LIMINE_MEMMAP_FRAMEBUFFER:
      type_str = "Framebuffer";
      break;
    default:
      type_str = "Unknown";
      break;
    }
    kprintf("| %x | %x | %dMB | %s |\n", i, (entry->base),
            (int)(entry->length / (1024 * 1024)), type_str);
  }

  kprintf("--------------------------------------------------\n");
  kprintf("Total entries: %d\n", kernel.memmap.entry_count);

  uint64_t total_available = 0;
  for (size_t i = 0; i < kernel.memmap.entry_count; i++) {
    if (kernel.memmap.entries[i]->type == 1) {
      total_available += kernel.memmap.entries[i]->length;
    }
  }

  kprintf("Total available memory: %d MB (%d KB)\n",
          total_available / (1024 * 1024), total_available / 1024);
}
uint64_t pmm_alloc_page(uint64_t size) {
  uint64_t page_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
  for (uint64_t i = 0; i < total_pages; i++) {
    if (BITMAP_TEST(bitmap, i))
      continue;
    uint64_t j;
    for (j = i; j < i + page_needed && j < total_pages; j++) {

      if (BITMAP_TEST(bitmap, j))
        break;
    }

    if (j == i + page_needed) {
      for (uint64_t k = i; k < j; k++) {
        BITMAP_SET(bitmap, k);
      }
      return physical.base + (i * BLOCK_SIZE);
    }
    i = j;
  }
  kprintf("PMM: out of memory!\n");
  return 0;
}

void pmm_free(uint64_t addr, size_t pages) {
  if (addr == 0)
    return;
  uint64_t idx = (addr - physical.base) / BLOCK_SIZE;
  for (uint64_t i = idx; i < idx + pages; i++)
    BITMAP_CLEAR(bitmap, i);
}
