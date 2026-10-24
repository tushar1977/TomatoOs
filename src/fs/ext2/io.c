#include "disk.h"
#include "ext2.h"
#include "klog.h"
#include "pmm.h"
#include "printf.h"
#include "spinlock.h"
#include "string.h"

bool ext2_read_blocks(struct ext2_partition *fs, uint32_t block, char *buf) {
  if (block >= fs->sb.s_blocks_count) {
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
