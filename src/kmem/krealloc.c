#include "include/kmem.h"
#include <stddef.h>
#include <stdio.h>
void *krealloc(void *ptr, size_t size) {
  meta_ptr block, newblock;
  size_t s;
  void *newptr;

  if (!ptr)
    return kmalloc(size);

  if (size == 0) {
    kfree(ptr);
    return NULL;
  }

  s = align4(size);
  block = (meta_ptr)((char *)ptr - META_BLOCK_SIZE);

  if (block->size >= s) {
    if (block->size - s >= (META_BLOCK_SIZE + 4)) {
      split(block, s);
    }
    return ptr;
  }

  if (block->next && block->next->free &&
      (block->size + META_BLOCK_SIZE + block->next->size) >= s) {

    size_t combined = block->size + META_BLOCK_SIZE + block->next->size;
    meta_ptr next = block->next;

    block->size = combined;
    block->next = next->next;
    if (block->next) {
      block->next->prev = block;
    }

    if (block->size - s >= (META_BLOCK_SIZE + 4)) {
      split(block, s);
    }
    return ptr;
  }

  newptr = kmalloc(size);
  if (!newptr)
    return NULL;

  newblock = (meta_ptr)((char *)newptr - META_BLOCK_SIZE);
  size_t copy_size =
      block->size < newblock->size ? block->size : newblock->size;

  char *src = (char *)ptr;
  char *dst = (char *)newptr;
  for (size_t i = 0; i < copy_size; i++) {
    dst[i] = src[i];
  }

  kfree(ptr);
  return newptr;
}
