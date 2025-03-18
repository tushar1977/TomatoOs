#ifndef __VFS_H__
#define __VFS_H__

#include <stdint.h>

#include "stdio.h"
#define INITIAL_CAPACITY 10
#define MAX_FILENAME 256
#define MAX_FILES 5
#define MYFS_MAGIC 0x13131313
#define MAX_DATABLOCKS 5
#define BLOCK_SIZE 1024
#define MAX_BLOCK_NUMBER 5

enum vtype { VNON, VREG, VDIR, VBLK, VCHR };

typedef struct {
  char name[MAX_FILENAME];
  uint32_t inode_number;
} File;

typedef struct {
  char name[MAX_FILENAME];
  File *files;
  int file_count;
  int capacity;
} Directory;
// 1-> true
// 0 -> false
typedef struct {
  uint32_t magic;
  uint32_t block_size;
  uint32_t inode_count;
  uint32_t free_blocks;
  uint32_t free_inodes;
  uint32_t root_inode;
} Superblock;

typedef struct {
  int free;
  int mode;
  int size;
  int blocks[MAX_BLOCK_NUMBER];
} Inode;

typedef struct {
  char data[BLOCK_SIZE];
  int free;
} DataBlock;

typedef struct {
  Directory *root;
  Superblock *superblock;
  Inode *inode_table[MAX_FILES];
  DataBlock data_blocks[MAX_DATABLOCKS];
  int total_files;
  int total_size;
} VFS;

extern VFS *vfs;

VFS *init_vfs();

void cleanup_vfs();

void create_file(VFS *vfs, const char *name, const char *data,
                 enum vtype VTYPE);
int delete_file(VFS *vfs, const char *filename);
const char *read_file(VFS *vfs, const char *filename);
int create_directory(VFS *vfs, const char *name);
void display_all_files(VFS *vfs);

void display_content(VFS *vfs, const char *name);
#endif // !__VFS_H__
