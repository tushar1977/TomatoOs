#include <stddef.h>
#include <stdint.h>
#define BLOCK_SIZE 4096
#define BITMAP_SET(map, bit) ((map)[(bit) / 8] |= (1 << ((bit) % 8)))
#define BITMAP_CLEAR(map, bit) ((map)[(bit) / 8] &= ~(1 << ((bit) % 8)))
#define BITMAP_TEST(map, bit) ((map)[(bit) / 8] & (1 << ((bit) % 8)))
typedef struct {
  uint64_t base;
  uint64_t size;
} available_memory;

extern available_memory physical;
void init_PMM();
void test_pmm();
void printMemoryMaps();
uint64_t pmm_alloc_page(uint64_t size);
void pmm_free(uint64_t addr, size_t pages);
