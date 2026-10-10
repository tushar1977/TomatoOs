#pragma once

// mbr headers

#include <stdint.h>
struct __attribute__((packed)) mbr_partition {
  uint8_t drive_status;
  uint8_t chs_start[3];
  uint8_t partition_type;
  uint8_t chs_end[3];
  uint32_t lba_start;
  uint32_t sector_count;
};

struct __attribute__((packed)) mbr_sector {
  uint8_t bootstrap[446];
  struct mbr_partition partitions[4];
  uint16_t signature;
};
