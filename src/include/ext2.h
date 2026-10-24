#pragma once
#include "disk.h"
#include "spinlock.h"
#include "stdint.h"

#define EXT4_FEATURE_INCOMPAT_64BIT 0x0080
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE 0x0002
#define EXT4_FEATURE_INCOMPAT_EXTENTS 0x0040
#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT2_MAGIC 0xEF53

// Features we can handle; anything else in incompat refuses the mount,
// anything else in ro_compat mounts read-only
#define EXT2_SUPPORTED_INCOMPAT (EXT2_FEATURE_INCOMPAT_FILETYPE)
#define EXT2_SUPPORTED_RO_COMPAT                                               \
  (EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER | EXT2_FEATURE_RO_COMPAT_LARGE_FILE)

#define EXT2_GOOD_OLD_REV 0
#define EXT2_DYNAMIC_REV 1
#define EXT2_GOOD_OLD_INODE_SIZE 128
#define EXT2_GOOD_OLD_FIRST_INO 11
#define EXT2_VALID_FS 1
#define EXT2_ROOT_INO 2
#define EXT2_ERROR_FS 2

// Block 1
struct ext2_superblock {
  uint32_t s_inodes_count;
  uint32_t s_blocks_count;
  uint32_t s_r_blocks_count;
  uint32_t s_free_blocks_count;
  uint32_t s_free_inodes_count;
  uint32_t s_first_data_block;
  uint32_t s_log_block_size;
  uint32_t s_log_frag_size;
  uint32_t s_blocks_per_group;
  uint32_t s_frags_per_group;
  uint32_t s_inodes_per_group;
  uint32_t s_mtime;
  uint32_t s_wtime;
  uint16_t s_mnt_count;
  uint16_t s_max_mnt_count;
  uint16_t s_magic;
  uint16_t s_state;
  uint16_t s_errors;
  uint16_t s_minor_rev_level;
  uint32_t s_lastcheck;
  uint32_t s_checkinterval;
  uint32_t s_creator_os;
  uint32_t revision;
  uint16_t s_def_resuid;
  uint16_t s_def_resgid;

  uint32_t s_first_ino;
  uint16_t inode_size;
  uint16_t s_block_group_nr;
  uint32_t s_feature_compat;
  uint32_t s_feature_incompat;
  uint32_t s_feature_ro_compat;
  uint8_t s_uuid[16];
  char s_volume_name[16];
  char s_last_mounted[64];
  uint32_t s_algo_bitmap;

  uint8_t s_prealloc_blocks;
  uint8_t s_prealloc_dir_blocks;
  uint16_t s_reserved_gdt_blocks;

  uint8_t s_journal_uuid[16];
  uint32_t s_journal_inum;
  uint32_t s_journal_dev;
  uint32_t s_last_orphan;

  uint32_t s_hash_seed[4];
  uint8_t s_def_hash_version;
  uint8_t s_jnl_backup_type;
  uint16_t s_desc_size;
  uint32_t s_default_mount_opts;
  uint32_t s_first_meta_bg;
  uint32_t s_mkfs_time;
  uint32_t s_jnl_blocks[17];

  uint32_t s_blocks_count_hi;
  uint32_t s_r_blocks_count_hi;
  uint32_t s_free_blocks_count_hi;
} __attribute__((packed));

// Block 2
struct ext2_group_desc {
  uint32_t bg_block_bitmap; // Block 8
  uint32_t bg_inode_bitmap; // Block 9
  uint32_t bg_inode_table;  // Block 10
  uint16_t bg_free_blocks_count;
  uint16_t bg_free_inodes_count;
  uint16_t bg_used_dirs_count;
  uint16_t bg_pad;
  uint32_t bg_reserved[3];
};

// Block 10
struct ext2_inode {
  uint16_t i_mode;
  uint16_t i_uid;
  uint32_t i_size;
  uint32_t i_atime;
  uint32_t i_ctime;
  uint32_t i_mtime;
  uint32_t i_dtime;
  uint16_t i_gid;
  uint16_t i_links_count;
  uint32_t i_blocks;
  uint32_t i_flags;
  uint32_t i_osd1;
  uint32_t i_block[15];
  uint32_t i_generation;
  uint32_t i_file_acl;
  uint32_t i_dir_acl;
  uint32_t i_faddr;
  uint32_t i_osd2[3];
};

#define EXT2_S_IFMT 0xF000
#define EXT2_S_IFLNK 0xA000
#define EXT2_S_IFDIR 0040000

struct ext2_dir_entry {
  uint32_t inode;
  uint16_t rec_len;
  uint8_t name_len;
  uint8_t file_type;
  char name[];
};

struct ext2_partition {
  struct disk *disk;
  struct ext2_superblock sb;

  uint64_t lba_start;
  uint32_t block_size;
  uint32_t sectors_per_block;
  uint32_t group_count;
  uint32_t inode_size;
  uint32_t first_ino;
  uint32_t inodes_per_block; /* block_size / inode_size */
  uint32_t itb_per_group;    /* inode table blocks per group */
  uint32_t desc_per_block;   /* block_size / sizeof(struct ext2_group_desc) */
  uint32_t gdb_count;        /* blocks the group descriptor table occupies */

  struct ext2_group_desc *groups; // group_count entries, loaded at mount

  bool read_only;
  bool sb_dirty;
  Spinlock lock;

  char *io_buf;
};

struct ext4_extent_header {
  uint16_t eh_magic;
  uint16_t eh_entries;
  uint16_t eh_max;
  uint16_t eh_depth;
  uint32_t eh_generation;
};

struct ext4_extent {
  uint32_t ee_block;
  uint16_t ee_len;
  uint16_t ee_start_hi;
  uint32_t ee_start_lo;
};

struct ext4_extent_idx {
  uint32_t ei_block;
  uint32_t ei_leaf_lo;
  uint16_t ei_leaf_hi;
  uint16_t ei_unused;
};

bool ext2_init(struct ext2_partition *fs, struct disk *target_disk,
               uint64_t lba_start);

// Superblock and group desc
bool ext2_read_superblock(struct ext2_partition *fs);
bool ext2_write_superblock(struct ext2_partition *fs);
void ext2_compute_layout(struct ext2_partition *fs);
bool ext2_fill_super(struct ext2_partition *fs);
struct ext2_group_desc *ext2_get_group_desc(struct ext2_partition *fs,
                                            uint32_t group);
bool ext2_read_group_descs(struct ext2_partition *fs);

// Io ops
bool ext2_read_blocks(struct ext2_partition *fs, uint32_t block, char *buf);
bool ext2_write_blocks(struct ext2_partition *fs, uint32_t block, char *buf);

// Inode
bool ext2_read_inode(struct ext2_partition *fs, uint32_t ino,
                     struct ext2_inode *out);
bool ext2_bmap(struct ext2_partition *fs, const struct ext2_inode *in,
               uint64_t fb, uint32_t *disk_block);
