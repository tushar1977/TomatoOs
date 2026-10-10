#pragma once
#include "stdbool.h"
#include "stdint.h"
#include <stddef.h>
enum partition_style {
  PARTITION_ERR = 0,
  PARTITION_MBR = 1,
  PARTITION_GPT = 2,
  PARTITION_RAW = 5
};

#ifndef ALIGNDOWN
#define ALIGNDOWN(x, align) ((x) - ((x) % (align)))
#endif

#ifndef ALIGNUP
#define ALIGNUP(x, align)                                                      \
  ((x) % (align) == 0 ? (x) : (x) + ((align) - ((x) % (align))))
#endif

struct disk {
  void *arg;
  size_t lba_size;
  size_t disk_size;

  bool (*read)(void *arg, char *buffer, uint64_t lba, size_t len_in_blocks);
  bool (*write)(void *arg, char *buffer, uint64_t lba, size_t len_in_blocks);
};

struct bytes_to_block_res {
  uint64_t lba;
  uint64_t size_in_blocks;
  uint64_t offset;
};

static inline struct bytes_to_block_res
bytes_to_blocks(uint64_t start, uint64_t size, uint64_t lba_size) {
  struct bytes_to_block_res result;

  result.lba = ALIGNDOWN(start, lba_size) / lba_size;

  result.size_in_blocks = (ALIGNUP(size, lba_size) / lba_size) == 0
                              ? 1
                              : ALIGNUP(size, lba_size) / lba_size;

  result.offset = start - ALIGNDOWN(start, lba_size);

  return result;
}

void init_disk(struct disk *new_disk);
