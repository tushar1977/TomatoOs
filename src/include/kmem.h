#pragma once
#include <stddef.h>
#define HEAP_SIZE (16 * 1024 * 1024)
extern char heap[HEAP_SIZE];
#define align4(x) (((((x) - 1) >> 2) << 2) + 4)
#define META_BLOCK_SIZE offsetof(struct meta_block, data)

typedef struct meta_block *meta_ptr;
struct meta_block {
  int free;
  size_t size;
  meta_ptr next;
  meta_ptr prev;
  void *ptr;
  char data[1];
};
extern char *brk;
extern char *endp;
extern void *base;

void *krealloc(void *ptr, size_t size);
void split(meta_ptr block, size_t size);
void kmem_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *sbrk(size_t size);
int cbrk(void *addr);
void *calloc(size_t n, size_t size);
void *realloc(void *ptr, size_t size);
void test();
