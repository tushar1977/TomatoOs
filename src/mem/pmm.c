#include "../include/pmm.h"
#include "../include/fb.h"
#include "../include/flanterm.h"
#include "../include/kernel.h"
#include "../include/limine.h"
#include "../include/printf.h"
#include "../include/string.h"
physical_allocator physical;
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
}

void printMemoryMaps() {
  kprintf("Memory Map:\n");
  kprintf("--------------------------------------------------\n");

  for (size_t i = 0; i < kernel.memmap.entry_count; i++) {
    struct limine_memmap_entry *entry = kernel.memmap.entries[i];

    const char *type_str;
    switch (entry->type) {
    case 1:
      type_str = "Available";
      break;
    case 2:
      type_str = "Reserved";
      break;
    case 3:
      type_str = "ACPI Recl";
      break;
    case 4:
      type_str = "ACPI NVS";
      break;
    case 5:
      type_str = "Bad Mem";
      break;
    default:
      type_str = "Unknown";
      break;
    }

    kprintf("| %x | 0x%x | %dMB | %s |\n", i, (unsigned int)(entry->base),
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
static void *b_malloc(uint64_t *base, size_t length, size_t size) {
  if (length <= BLOCK_SIZE) {
    if (size + 1 <= length && *((uint64_t *)base) == 0) {
      *base = size;
      memset(base + 1, 0, sizeof(void *) * size);
      return (void *)(base + 1);
    } else {
      return NULL;
    }
  }

  size_t half = length / 2;
  if (half <= size + 1 && *((uint64_t *)base) == 0) {
    *base = size;
    memset(base + 1, 0, sizeof(void *) * size);
    return (void *)(base + 1);
  } else if (half > size) {
    void *b = b_malloc(base, half, size);
    if (b == NULL) {
      b = b_malloc(base + half, half, size);
    }

    return b;
  }
  return NULL;
}

void *k_malloc(size_t size) {
  return b_malloc((uint64_t *)physical.base, physical.size, size);
}

void k_free(void *base) {
  uint64_t size = *(((uint64_t *)base) - 1);
  memset(base, 0, size);
}
