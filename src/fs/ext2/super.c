#include "disk.h"
#include "ext2.h"
#include "kernel.h"
#include "klog.h"
#include "kmem.h"
#include "pmm.h"
#include "printf.h"
#include "string.h"

static bool sb_reject(const char *why) {
  klog(KLOG_ERROR, "EXT2");
  kprintf("bad superblock: %s\n", why);
  return false;
}

bool ext2_validate_sb(struct ext2_partition *fs) {
  const struct ext2_superblock *sb = &fs->sb;

  if (sb->s_log_block_size > 2)
    return sb_reject("block size above 4096 not supported");
  uint32_t bs = 1024u << sb->s_log_block_size;

  uint32_t lba = fs->disk->lba_size;
  if (lba == 0 || bs < lba || bs % lba != 0)
    return sb_reject("block size not a multiple of disk sector size");

  /* Revision and features. */
  if (sb->revision > EXT2_DYNAMIC_REV)
    return sb_reject("unknown revision level");

  uint32_t bad = sb->s_feature_incompat & ~EXT2_SUPPORTED_INCOMPAT;
  if (bad) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("unsupported incompat features %x, refusing to mount\n",
            (unsigned int)bad);
    return false;
  }

  /* Inode geometry. */
  uint32_t inode_size = EXT2_GOOD_OLD_INODE_SIZE;
  uint32_t first_ino = EXT2_GOOD_OLD_FIRST_INO;
  if (sb->revision >= EXT2_DYNAMIC_REV) {
    inode_size = sb->inode_size;
    first_ino = sb->s_first_ino;
    if (inode_size < EXT2_GOOD_OLD_INODE_SIZE || inode_size > bs ||
        (inode_size & (inode_size - 1)) != 0)
      return sb_reject("invalid inode size");
    if (first_ino < EXT2_GOOD_OLD_FIRST_INO)
      return sb_reject("invalid first inode");
  }

  if (sb->s_blocks_count == 0)
    return sb_reject("zero block count");
  if (sb->s_first_data_block >= sb->s_blocks_count)
    return sb_reject("first data block beyond end of fs");
  if (sb->s_blocks_per_group == 0 || sb->s_blocks_per_group > 8 * bs)
    return sb_reject("invalid blocks per group");
  if (sb->s_inodes_per_group == 0 || sb->s_inodes_per_group > 8 * bs)
    return sb_reject("invalid inodes per group");

  uint64_t data_blocks = (uint64_t)sb->s_blocks_count - sb->s_first_data_block;
  uint64_t groups =
      (data_blocks + sb->s_blocks_per_group - 1) / sb->s_blocks_per_group;

  if (sb->s_inodes_count == 0 ||
      sb->s_inodes_count > groups * sb->s_inodes_per_group)
    return sb_reject("inode count inconsistent with group layout");
  if (sb->s_inodes_count < first_ino)
    return sb_reject("too few inodes");

  uint64_t itb = ((uint64_t)sb->s_inodes_per_group * inode_size + bs - 1) / bs;
  if (itb >= sb->s_blocks_per_group)
    return sb_reject("inode table larger than a block group");

  uint64_t gdt_blocks = (groups * sizeof(struct ext2_group_desc) + bs - 1) / bs;
  if ((uint64_t)sb->s_first_data_block + 1 + gdt_blocks > sb->s_blocks_count)
    return sb_reject("group descriptor table does not fit");

  return true;
}

bool ext2_read_superblock(struct ext2_partition *fs) {
  struct bytes_to_block_res r = bytes_to_blocks(1024, 1024, fs->disk->lba_size);
  uint64_t lba = fs->lba_start + r.lba;

  bool ok = fs->disk->read(fs->disk->arg, fs->io_buf, lba, r.size_in_blocks);
  if (ok)
    memcpy(&fs->sb, fs->io_buf + r.offset, sizeof(fs->sb));

  if (!ok) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("failed to read superblock (lba %d)\n", (int)lba);
    return false;
  }

  if (fs->sb.s_magic != EXT2_MAGIC) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("invalid superblock magic: 0x%x (expected 0x%x)\n", fs->sb.s_magic,
            EXT2_MAGIC);
    return false;
  }

  kprintf("inode count %d\n", fs->sb.s_inodes_count);

  return true;
}

bool ext2_validate_super(struct ext2_partition *fs) {}

bool ext2_write_superblock(struct ext2_partition *fs) {
  if (fs->read_only) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("write_superblock: filesystem is read-only\n");
    return false;
  }

  struct bytes_to_block_res r = bytes_to_blocks(1024, 1024, fs->disk->lba_size);
  uint64_t lba = fs->lba_start + r.lba;

  spinlock_aquire(&fs->lock);
  bool read_ok =
      fs->disk->read(fs->disk->arg, fs->io_buf, lba, r.size_in_blocks);
  bool ok = false;
  if (read_ok) {
    memcpy(fs->io_buf + r.offset, &fs->sb, sizeof(fs->sb));
    ok = fs->disk->write(fs->disk->arg, fs->io_buf, lba, r.size_in_blocks);
  }
  spinlock_release(&fs->lock);

  if (!ok) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("write_superblock: disk %s failed (lba %d)\n",
            read_ok ? "write" : "read", (int)lba);
    return false;
  }

  fs->sb_dirty = false;
  return true;
}

struct ext2_group_desc *ext2_get_group_desc(struct ext2_partition *fs,
                                            uint32_t group) {
  return group < fs->group_count ? &fs->groups[group] : NULL;
}

void ext2_compute_layout(struct ext2_partition *fs) {
  struct ext2_superblock *sb = &fs->sb;

  fs->block_size = 1024 << sb->s_log_block_size;
  fs->sectors_per_block = fs->block_size / fs->disk->lba_size;

  uint64_t data_blocks = sb->s_blocks_count - sb->s_first_data_block;
  fs->group_count = (uint32_t)((data_blocks + sb->s_blocks_per_group - 1) /
                               sb->s_blocks_per_group);

  if (sb->revision == EXT2_GOOD_OLD_REV) {
    fs->inode_size = EXT2_GOOD_OLD_INODE_SIZE;
    fs->first_ino = EXT2_GOOD_OLD_FIRST_INO;
  } else {
    fs->inode_size = sb->inode_size;
    fs->first_ino = sb->s_first_ino;
  }

  fs->read_only = (sb->s_feature_ro_compat & ~EXT2_SUPPORTED_RO_COMPAT) != 0;
  if (fs->read_only) {
    klog(KLOG_WARN, "EXT2");
    kprintf(
        "unsupported ro_compat features %x, mounting read-only\n",
        (unsigned int)(sb->s_feature_ro_compat & ~EXT2_SUPPORTED_RO_COMPAT));
  }
  fs->inodes_per_block = fs->block_size / fs->inode_size;
  fs->itb_per_group = (sb->s_inodes_per_group + fs->inodes_per_block - 1) /
                      fs->inodes_per_block;
  fs->desc_per_block = fs->block_size / sizeof(struct ext2_group_desc);
  fs->gdb_count =
      (fs->group_count + fs->desc_per_block - 1) / fs->desc_per_block;

  klog(KLOG_INFO, "EXT2");
  kprintf("block size %d, %d groups, inode size %d, first inode %d\n",
          (int)fs->block_size, (int)fs->group_count, (int)fs->inode_size,
          (int)fs->first_ino);
}

bool ext2_read_group_descs(struct ext2_partition *fs) {
  uint32_t table_bytes = fs->group_count * sizeof(struct ext2_group_desc);
  uint32_t table_blocks = (table_bytes + fs->block_size - 1) / fs->block_size;
  uint32_t first_block = fs->sb.s_first_data_block + 1;

  fs->groups = kmalloc(table_bytes);
  if (fs->groups == NULL) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("out of memory for %d group descriptors\n", (int)fs->group_count);
    return false;
  }

  uint64_t phys = pmm_alloc_page(4096);
  if (phys == 0) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("out of memory for group descriptor buffer\n");
    kfree(fs->groups);
    fs->groups = NULL;
    return false;
  }
  char *buf = (char *)(kernel.hhdm + phys);

  for (uint32_t i = 0; i < table_blocks; i++) {
    if (!ext2_read_blocks(fs, first_block + i, buf)) {
      klog(KLOG_ERROR, "EXT2");
      kprintf("failed to read group descriptor block %d\n",
              (int)(first_block + i));
      pmm_free(phys, 1);
      kfree(fs->groups);
      fs->groups = NULL;
      return false;
    }

    uint32_t done = i * fs->block_size;
    uint32_t chunk = table_bytes - done;
    if (chunk > fs->block_size)
      chunk = fs->block_size;
    memcpy((char *)fs->groups + done, buf, chunk);
  }

  pmm_free(phys, 1);

  uint32_t itable_blocks =
      (fs->sb.s_inodes_per_group * fs->inode_size + fs->block_size - 1) /
      fs->block_size;

  for (uint32_t g = 0; g < fs->group_count; g++) {
    struct ext2_group_desc *gd = &fs->groups[g];
    uint32_t first = fs->sb.s_first_data_block + g * fs->sb.s_blocks_per_group;
    uint32_t last = first + fs->sb.s_blocks_per_group - 1;
    if (last >= fs->sb.s_blocks_count)
      last = fs->sb.s_blocks_count - 1;

    if (gd->bg_block_bitmap < first || gd->bg_block_bitmap > last ||
        gd->bg_inode_bitmap < first || gd->bg_inode_bitmap > last ||
        gd->bg_inode_table < first ||
        gd->bg_inode_table + itable_blocks - 1 > last) {
      klog(KLOG_ERROR, "EXT2");
      kprintf("group %d descriptor out of range (block bitmap %d, inode "
              "bitmap %d, inode table %d)\n",
              (int)g, (int)gd->bg_block_bitmap, (int)gd->bg_inode_bitmap,
              (int)gd->bg_inode_table);
      kfree(fs->groups);
      fs->groups = NULL;
      return false;
    }
  }

  klog(KLOG_INFO, "EXT2");
  kprintf("loaded %d group descriptors from %d block(s)\n",
          (int)fs->group_count, (int)table_blocks);
  return true;
}

void ext2_free_group_descs(struct ext2_partition *fs) {
  if (fs == NULL || fs->groups == NULL)
    return;

  kfree(fs->groups);
  fs->groups = NULL;
}

static void ext2_setup_super(struct ext2_partition *fs) {
  uint16_t state = fs->sb.s_state;
  if (!(state & 1)) { /* EXT2_VALID_FS */
    klog(KLOG_WARN, "EXT2");
    kprintf("filesystem was not cleanly unmounted\n");
  } else if (state & 2) { /* EXT2_ERROR_FS */
    klog(KLOG_WARN, "EXT2");
    kprintf("filesystem has recorded errors\n");
  }
}

static bool ext2_check_root(struct ext2_partition *fs) {
  struct ext2_inode root;
  if (!ext2_read_inode(fs, EXT2_ROOT_INO, &root))
    return false;
  if ((root.i_mode & 0xF000) != 0x4000 || root.i_size == 0 ||
      root.i_links_count == 0) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("root inode is not a valid directory\n");
    return false;
  }
  for (uint64_t b = 0; b < 3; b++) {
    uint32_t d;
    if (ext2_bmap(fs, &root, b, &d))
      kprintf("bmap: file block %d -> disk block %d\n", (int)b, (int)d);
    else
      kprintf("bmap: file block %d failed\n", (int)b);
  }
  return true;
}

bool ext2_fill_super(struct ext2_partition *fs) {
  if (!ext2_read_superblock(fs))
    return false;
  if (!ext2_validate_sb(fs))
    return false;

  ext2_compute_layout(fs);

  if (!ext2_read_group_descs(fs))
    return false;

  if (!ext2_check_root(fs)) {
    ext2_free_group_descs(fs);
    return false;
  }

  ext2_setup_super(fs);
  return true;
}
