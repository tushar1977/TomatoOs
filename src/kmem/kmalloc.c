#include "include/kmem.h"

void *kmalloc(size_t size) {
  size = (size + 15) & ~15;
  if (heap_offset + size > sizeof(heap))
    return NULL;
  void *ptr = &heap[heap_offset];
  heap_offset += size;
  return ptr;
}
