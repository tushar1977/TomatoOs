#include "ext2.h"
#include "klog.h"
#include "printf.h"
#include "stdio.h"
#include "string.h"

static bool ext2_read_ptr(struct ext2_partition *fs, uint32_t blk, uint32_t idx,
                          uint32_t *out) {
  if (blk < fs->sb.s_first_data_block || blk >= fs->sb.s_blocks_count) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("bmap: pointer block %d out of range\n", (int)blk);
    return false;
  }
  spinlock_aquire(&fs->lock);
  bool ok = ext2_read_blocks(fs, blk, fs->io_buf);
  if (ok)
    *out = ((uint32_t *)fs->io_buf)[idx];
  spinlock_release(&fs->lock);
  return ok;
}

bool ext2_bmap(struct ext2_partition *fs, const struct ext2_inode *in,
               uint64_t fb, uint32_t *disk_block) {
  const uint64_t ppb = fs->block_size / 4;
  uint32_t p;

  *disk_block = 0;

  if (fb < 12) { /* direct */
    *disk_block = in->i_block[fb];
    return true;
  }
  fb -= 12;

  if (fb < ppb) { /* single indirect */
    p = in->i_block[12];
    if (!p)
      return true;
    return ext2_read_ptr(fs, p, (uint32_t)fb, disk_block);
  }
  fb -= ppb;

  if (fb < ppb * ppb) { /* double indirect */
    p = in->i_block[13];
    if (!p)
      return true;
    if (!ext2_read_ptr(fs, p, (uint32_t)(fb / ppb), &p))
      return false;
    if (!p)
      return true;
    return ext2_read_ptr(fs, p, (uint32_t)(fb % ppb), disk_block);
  }
  fb -= ppb * ppb;

  if (fb < ppb * ppb * ppb) {
    p = in->i_block[14];
    if (!p)
      return true;
    if (!ext2_read_ptr(fs, p, (uint32_t)(fb / (ppb * ppb)), &p))
      return false;
    if (!p)
      return true;
    uint64_t rem = fb % (ppb * ppb);
    if (!ext2_read_ptr(fs, p, (uint32_t)(rem / ppb), &p))
      return false;
    if (!p)
      return true;
    return ext2_read_ptr(fs, p, (uint32_t)(rem % ppb), disk_block);
  }

  return false;
}
bool ext2_read_inode(struct ext2_partition *fs, uint32_t ino,
                     struct ext2_inode *out) {

  const struct ext2_superblock *sb = &fs->sb;

  if (ino == 0 || ino > sb->s_inodes_count) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("read_inode: inode %d out of range (max %d)\n", (int)ino,
            (int)sb->s_inodes_count);
    return false;
  }

  uint64_t group = (ino - 1) / sb->s_inodes_per_group;
  uint64_t index = (ino - 1) % sb->s_inodes_per_group;

  struct ext2_group_desc *gd = ext2_get_group_desc(fs, group);
  if (!gd) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("read_inode: inode %d maps to bad group %d (have %d)\n", (int)ino,
            (int)group, (int)fs->group_count);
    return false;
  }

  uint64_t byte = index * fs->inode_size;
  uint64_t block = (uint64_t)gd->bg_inode_table + byte / fs->block_size;
  uint32_t off = (uint32_t)(byte % fs->block_size);

  klog(KLOG_INFO, "EXT2");
  kprintf("read_inode: ino=%d group=%d index=%d inode_size=%d\n", (int)ino,
          (int)group, (int)index, (int)fs->inode_size);
  klog(KLOG_INFO, "EXT2");
  kprintf("read_inode: bg_inode_table=%d byte=%d -> block=%d off=%d\n",
          (int)gd->bg_inode_table, (int)byte, (int)block, (int)off);

  if (block >= sb->s_blocks_count) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("read_inode: inode %d table block %d out of range (count %d)\n",
            (int)ino, (int)block, (int)sb->s_blocks_count);
    return false;
  }

  size_t n = fs->inode_size < sizeof(*out) ? fs->inode_size : sizeof(*out);

  spinlock_aquire(&fs->lock);
  bool ok = ext2_read_blocks(fs, (uint32_t)block, fs->io_buf);
  if (ok) {
    memset(out, 0, sizeof(*out));
    memcpy(out, fs->io_buf + off, n);
  }
  spinlock_release(&fs->lock);

  if (!ok) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("read_inode: failed to read block %d for inode %d\n", (int)block,
            (int)ino);
    return false;
  }

  klog(KLOG_INFO, "EXT2");
  kprintf("read_inode: copied %d bytes, mode=0x%x size=%d links=%d\n", (int)n,
          (unsigned int)out->i_mode, (int)out->i_size, (int)out->i_links_count);
  klog(KLOG_INFO, "EXT2");
  kprintf("read_inode: i_blocks=%d i_block[0]=%d i_block[12]=%d\n",
          (int)out->i_blocks, (int)out->i_block[0], (int)out->i_block[12]);

  return true;
}
