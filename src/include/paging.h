#include <stddef.h>
#include <stdint.h>

#define KERNEL_PFLAG_PRESENT 0b1
#define KERNEL_PFLAG_WRITE 0b10
#define KERNEL_PFLAG_USER 0b100
#define KERNEL_PFLAG_PXD                                                       \
  0b10000000000000000000000000000000000000000000000000000000000000 // a bit long
                                                                   // lmao
#define PTE_PRESENT (1 << 0)
#define PTE_WRITABLE (1 << 1)
#define PTE_USER (1 << 2)
#define PTE_HUGE (1 << 7)
#define PTE_GLOBAL (1 << 8)
#define PTE_NX (1ULL << 63)
#define PAGE_SIZE 4096
#define KERNEL_STACK_SIZE (64 * 1024) // 64KB stack
typedef struct PageEntry {
  uint8_t present : 1;
  uint8_t writable : 1;
  uint8_t user_accessible : 1;
  uint8_t write_through_caching : 1;
  uint8_t disable_cache : 1;
  uint8_t accessed : 1;
  uint8_t dirty : 1;
  uint8_t null : 1;
  uint8_t global : 1;
  uint8_t avl1 : 3;
  uintptr_t physical_address : 40;
  uint16_t avl2 : 11;
  uint8_t no_execute : 1;
} PageEntry;

typedef struct PageTable {
  PageEntry entries[512];
} PageTable;

PageTable *initPML4();
void map_page(uint64_t virt, uint64_t phys, uint64_t flags, size_t size);
uint64_t readCR3(void);
void *getPhysicalAddress(void *virtual_address);
