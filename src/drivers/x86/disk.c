#include "disk.h"
#include "ext2.h"
#include "gpt.h"
#include "include/pmm.h"
#include "kernel.h"
#include "klog.h"
#include "kmem.h"
#include "mbr.h"
#include "printf.h"
#include <stdarg.h>

uint8_t linux_fs_signature[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84,
                                  0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69,
                                  0xD8, 0x47, 0x7D, 0xE4};

const char *disk_type_to_str(enum partition_style disk_type) {
  switch (disk_type) {
  case PARTITION_ERR:
    return "Unknown";
  case PARTITION_MBR:
    return "MBR";
  case PARTITION_GPT:
    return "GPT";
  case PARTITION_RAW:
    return "Raw";
  }
}

enum partition_style determine_disk_type(struct disk *target_disk) {
  void *phys = (void *)pmm_alloc_page(4096);
  char *buffer = (char *)(kernel.hhdm + (uint64_t)phys);

  bool ok = target_disk->read(target_disk->arg, buffer, 1, 1);
  if (!ok) {
    klog(KLOG_ERROR, "DISK");
    kprintf("failed to read LBA 1 while probing partition style\n");
    pmm_free((uint64_t)phys, 1);
    return PARTITION_ERR;
  }

  if (memcmp((const char *)buffer, GPT_SIGNATURE, sizeof(GPT_SIGNATURE) - 1) ==
      0) {
    pmm_free((uint64_t)phys, 1);
    return PARTITION_GPT;
  }

  bool mbr_ok = target_disk->read(target_disk->arg, buffer, 0, 1);
  if (mbr_ok && (uint8_t)buffer[510] == 0x55 && (uint8_t)buffer[511] == 0xAA) {
    pmm_free((uint64_t)phys, 1);
    return PARTITION_MBR;
  }

  if (!mbr_ok) {
    klog(KLOG_ERROR, "DISK");
    kprintf("failed to read LBA 0 while probing partition style\n");
  }

  pmm_free((uint64_t)phys, 1);
  return PARTITION_ERR;
}

bool disk_selftest(struct disk *target_disk) {
  void *phys = (void *)pmm_alloc_page(4096);
  char *buffer = (char *)(kernel.hhdm + (uint64_t)phys);

  bool ok = target_disk->read(target_disk->arg, buffer, 0, 1);

  pmm_free((uint64_t)phys, 1);

  if (!ok) {
    klog(KLOG_ERROR, "DISK");
    kprintf("selftest failed: could not read LBA 0\n");
    return false;
  }

  return true;
}

const char *mbr_to_str(uint8_t type) {
  switch (type) {
  case 0x83:
    return "linux";
  case 0xb:
    return "fat32";
  default:
    return "unknown";
  }
  return "unknown";
}

bool disk_try_linux(struct disk *disk) {
  struct bytes_to_block_res b = bytes_to_blocks(1024, 1024, disk->lba_size);

  void *phys = (void *)pmm_alloc_page(4096);
  char *buffer = (char *)(kernel.hhdm + (uint64_t)phys);

  bool ok = disk->read(disk->arg, buffer, b.lba, b.size_in_blocks);

  if (!ok) {
    klog(KLOG_ERROR, "DISK");
    kprintf("failed to read superblock on raw disk\n");
    pmm_free((uint64_t)phys, 1);
    return false;
  }

  struct ext2_superblock *sb =
      (struct ext2_superblock *)((uint64_t)buffer + b.offset);

  bool is_ext2 = (sb->s_magic == EXT2_MAGIC);
  pmm_free((uint64_t)phys, 1);

  if (is_ext2) {
    struct ext2_partition *fs = kmalloc(sizeof(*fs));
    if (!fs) {
      klog(KLOG_ERROR, "DISK");
      kprintf("out of memory for ext2 partition\n");
      return false;
    }
    if (!ext2_init(fs, disk, 0))
      kfree(fs);
  }

  return is_ext2;
}

bool disk_do_mbr_linux(struct disk *disk, uint64_t start) {
  struct bytes_to_block_res b = bytes_to_blocks(1024, 1024, disk->lba_size);

  void *phys = (void *)pmm_alloc_page(4096);
  char *buffer = (char *)(kernel.hhdm + (uint64_t)phys);

  bool ok = disk->read(disk->arg, buffer, start + b.lba, b.size_in_blocks);

  if (!ok) {
    klog(KLOG_ERROR, "DISK");
    kprintf("failed to read superblock of partition at lba %d\n", (int)start);
    pmm_free((uint64_t)phys, 1);
    return false;
  }

  struct ext2_superblock *sb =
      (struct ext2_superblock *)((uint64_t)buffer + b.offset);

  bool is_ext2 = (sb->s_magic == EXT2_MAGIC);
  if (is_ext2) {
    klog(KLOG_INFO, "DISK");
    kprintf("detected ext2 on partition at lba %d\n", (int)start);
  } else {
    klog(KLOG_WARN, "DISK");
    kprintf("partition at lba %d is not ext2 (magic=%x)\n", (int)start,
            (unsigned int)sb->s_magic);
  }

  pmm_free((uint64_t)phys, 1);

  if (is_ext2) {
    struct ext2_partition *fs = kmalloc(sizeof(*fs));
    if (!ext2_init(fs, disk, start))
      kfree(fs);
  }

  return is_ext2;
}

void disk_gpt(struct disk *new_disk) {

  void *phys = (void *)pmm_alloc_page(4096);
  char *buffer = (char *)(kernel.hhdm + (uint64_t)phys);

  struct gpt_lba1 lba1 = {};
  new_disk->read(new_disk->arg, (char *)buffer, 1, 1);

  struct gpt_partition_entry *part_table = (struct gpt_partition_entry *)buffer;
  for (size_t i = 0; i < lba1.count_partitions; i++) {
    struct gpt_partition_entry entry = part_table[i];
    if (memcmp(entry.guid, (uint8_t[16]){0}, 16) == 0)
      continue;

    klog(KLOG_INFO, "GPT");
    kprintf("partition %d: start_lba %d\n", (int)i, (int)entry.start_lba);

    if (memcmp(entry.guid, linux_fs_signature, 16) == 0) {
      klog(KLOG_INFO, "GPT");
      kprintf("partition %d: linux filesystem\n", (int)i);
      disk_do_mbr_linux(new_disk, entry.start_lba);
    }
  }

  pmm_free((uint64_t)phys, 1);
}

void disk_mbr(struct disk *new_disk) {

  void *phys = (void *)pmm_alloc_page(4096);
  char *buffer = (char *)(kernel.hhdm + (uint64_t)phys);

  new_disk->read(new_disk->arg, (char *)buffer, 0, 1);

  struct mbr_sector *mbr = (struct mbr_sector *)buffer;
  for (int i = 0; i < 4; i++) {
    if (mbr->partitions[i].partition_type != 0) {
      klog(KLOG_INFO, "MBR");
      kprintf("partition %d: type %s (%x), lba_start %d\n", i,
              mbr_to_str(mbr->partitions[i].partition_type),
              (unsigned int)mbr->partitions[i].partition_type,
              (int)mbr->partitions[i].lba_start);
      if (mbr->partitions[i].partition_type == 0x83) {
        disk_do_mbr_linux(new_disk, mbr->partitions[i].lba_start);
      }
    }
  }

  pmm_free((uint64_t)phys, 1);
}

void init_disk(struct disk *new_disk) {
  if (disk_selftest(new_disk) == false)
    return;

  enum partition_style style = determine_disk_type(new_disk);
  klog(KLOG_INFO, "DISK");
  kprintf("partition style: %s\n", disk_type_to_str(style));

  switch (style) {
  case PARTITION_MBR:
    disk_mbr(new_disk);
    break;
  case PARTITION_GPT:
    disk_gpt(new_disk);
    break;
  default:
    bool status = disk_try_linux(new_disk);
    (void)status;
    break;
  }
}
