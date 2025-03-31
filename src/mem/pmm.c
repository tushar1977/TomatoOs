#include "../include/pmm.h"
#include "../include/kernel.h"
#include "../include/limine.h"
#include "../include/string.h"
physical_allocator physical;
void init_PMM() {
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
