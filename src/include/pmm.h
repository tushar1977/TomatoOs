#include <stddef.h>
#include <stdint.h>
#define BLOCK_SIZE 4096
typedef struct {
  uint64_t base;
  uint64_t size;
} physical_allocator;

extern physical_allocator physical;
void init_PMM();

void *k_malloc(size_t size);
void k_free(void *base);
