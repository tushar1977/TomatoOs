#include "ext2.h"
#include "disk.h"
#include "kernel.h"
#include "klog.h"
#include "pmm.h"
#include "printf.h"
#include "string.h"

bool ext2_read_superblock(struct ext2_partition *fs) {
  struct bytes_to_block_res r = bytes_to_blocks(1024, 1024, fs->disk->lba_size);
  uint64_t lba = fs->lba_start + r.lba;

  spinlock_aquire(&fs->lock);
  bool ok = fs->disk->read(fs->disk->arg, fs->io_buf, lba, r.size_in_blocks);
  if (ok)
    memcpy(&fs->sb, fs->io_buf + r.offset, sizeof(fs->sb));
  spinlock_release(&fs->lock);

  if (!ok) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("failed to read superblock (lba %d)\n", (int)lba);
    return false;
  }

  return true;
}

bool ext2_write_superblock(struct ext2_partition *fs) {
  struct bytes_to_block_res r = bytes_to_blocks(1024, 1024, fs->disk->lba_size);
  uint64_t lba = fs->lba_start + r.lba;

  spinlock_aquire(&fs->lock);
  bool ok = fs->disk->read(fs->disk->arg, fs->io_buf, lba, r.size_in_blocks);
  if (ok)
    memcpy(&fs->sb, fs->io_buf + r.offset, sizeof(fs->sb));
  spinlock_release(&fs->lock);

  if (!ok) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("failed to read superblock (lba %d)\n", (int)lba);
    return false;
  }

  return true;
}

bool ext2_read_blocks(struct ext2_partition *fs, uint32_t block, char *buf) {
  if (block > fs->sb.s_blocks_count) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("read_block: block %d out of range (count %d)\n", (int)block,
            (int)fs->sb.s_blocks_count);
    return false;
  }
  struct bytes_to_block_res r = bytes_to_blocks(
      (uint64_t)block * fs->block_size, fs->block_size, fs->disk->lba_size);
  uint64_t lba = fs->lba_start + r.lba;

  if (fs->block_size >= fs->disk->lba_size) {
    if (fs->disk->read(fs->disk->arg, buf, lba, r.size_in_blocks)) {
      return true;
    }
  } else {
    spinlock_aquire(&fs->lock);
    bool ok = fs->disk->read(fs->disk->arg, fs->io_buf, lba, 1);
    if (ok)
      memcpy(buf, fs->io_buf + r.offset, fs->block_size);
    spinlock_release(&fs->lock);
    if (ok)
      return true;
  }
  klog(KLOG_ERROR, "EXT2");
  kprintf("read_block: disk read failed (block %d, lba %d)\n", (int)block,
          (int)lba);
  return false;
}

bool ext2_write_blocks(struct ext2_partition *fs, uint32_t block, char *buf) {
  if (fs->read_only) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("write_block: filesystem is read-only (block %d)\n", (int)block);
    return false;
  }

  if (block >= fs->sb.s_blocks_count) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("write_block: block %d out of range (count %d)\n", (int)block,
            (int)fs->sb.s_blocks_count);
    return false;
  }

  struct bytes_to_block_res r = bytes_to_blocks(
      (uint64_t)block * fs->block_size, fs->block_size, fs->disk->lba_size);
  uint64_t lba = fs->lba_start + r.lba;

  if (fs->block_size >= fs->disk->lba_size) {
    if (fs->disk->write(fs->disk->arg, buf, lba, r.size_in_blocks))
      return true;
  } else {
    spinlock_aquire(&fs->lock);
    bool ok = fs->disk->read(fs->disk->arg, fs->io_buf, lba, 1);
    if (ok) {
      memcpy(fs->io_buf + r.offset, buf, fs->block_size);
      ok = fs->disk->write(fs->disk->arg, fs->io_buf, lba, 1);
    }
    spinlock_release(&fs->lock);
    if (ok)
      return true;
  }

  klog(KLOG_ERROR, "EXT2");
  kprintf("write_block: disk write failed (block %d, lba %d)\n", (int)block,
          (int)lba);
  return false;
}

void ext2_init(struct disk *target_disk, uint64_t lba_start) {
  struct bytes_to_block_res r =
      bytes_to_blocks(1024, 1024, target_disk->lba_size);

  void *phys = (void *)pmm_alloc_page(4096);
  char *buffer = (char *)(kernel.hhdm + (uint64_t)phys);

  bool ok = target_disk->read(target_disk->arg, buffer, lba_start + r.lba,
                              r.size_in_blocks);
  if (!ok) {
    kprintf("[ext2] failed to read superblock\n");
    pmm_free((uint64_t)phys, 1);
    return;
  }

  struct ext2_superblock *sb =
      (struct ext2_superblock *)((uint64_t)buffer + r.offset);

  if (sb->s_magic != EXT2_MAGIC) {
    kprintf("[ext2] magic not valid!\n");
    pmm_free((uint64_t)phys, 1);
    return;
  }

  if ((1024 << sb->s_log_block_size) > 4096) {
    kprintf("[ext2] partition block size is bigger than page_size! (todo)\n");
    pmm_free((uint64_t)phys, 1);
    return;
  }

  if (sb->revision >= 1) {
    klog(KLOG_INFO, "EXT2");
    kprintf("detected features %s %s %s\n",
            (sb->s_feature_ro_compat & EXT2_FEATURE_RO_COMPAT_LARGE_FILE)
                ? "EXT2_FEATURE_RO_COMPAT_LARGE_FILE"
                : "",
            (sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT)
                ? "EXT4_FEATURE_INCOMPAT_64BIT"
                : "",
            (sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_EXTENTS)
                ? "EXT4_FEATURE_INCOMPAT_EXTENTS"
                : "");
  }

  pmm_free((uint64_t)phys, 1);
}
