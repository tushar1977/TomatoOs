
#pragma once

#include <stdint.h>
struct __attribute__((packed)) gpt_lba1 {
  uint8_t signature[8];
  uint32_t revision;
  uint32_t size;
  uint32_t crc32;
  uint32_t reserved;
  uint64_t current_lba;
  uint64_t other_lba;
  uint64_t first_usable;
  uint64_t last_usable;
  uint8_t guid[16];
  uint64_t partition_lba;
  uint32_t count_partitions;
  uint32_t size_of_partition;
};

struct __attribute__((packed)) gpt_partition_entry {
  uint8_t guid[16];
  uint8_t unique_guid[16];
  uint64_t start_lba;
  uint64_t end_lba;
  uint64_t attributes;
  uint8_t name[72];
};

#define GPT_SIGNATURE "EFI PART"
