#include "include/vfs.h"
#include "include/kmem.h"
#include "include/printf.h"
#include "include/string.h"
#include <stdint.h>

#define VFS_SUCCESS 0
#define VFS_ERROR -1
#define VFS_FILE_NOT_FOUND -2
#define VFS_FULL -3

VFS *vfs = NULL;
void init_vfs() {
  vfs = (VFS *)kmalloc(sizeof(VFS));
  if (!vfs) {
    kprintf("Failed to allocate memory for VFS.\n");
    return;
  }

  vfs->superblock =
      (struct myfs_superblock *)kmalloc(sizeof(struct myfs_superblock));
  if (!vfs->superblock) {
    kprintf("Failed to allocate memory for superblock.\n");
    kfree(vfs);
    return;
  }

  for (int i = 0; i < MAX_FILES; i++) {
    vfs->inode_table[i] =
        (struct myfs_inode *)kmalloc(sizeof(struct myfs_inode));
    if (!vfs->inode_table[i]) {
      kprintf("Failed to allocate memory for inode table.\n");
      for (int j = 0; j < i; j++) {
        kfree(vfs->inode_table[j]);
      }
      kfree(vfs->superblock);
      kfree(vfs);
      return;
    }
  }

  vfs->root = (Directory *)kmalloc(sizeof(Directory));
  if (!vfs->root) {
    kprintf("Failed to allocate memory for root directory.\n");
    for (int i = 0; i < MAX_FILES; i++) {
      kfree(vfs->inode_table[i]);
    }
    kfree(vfs->superblock);
    kfree(vfs);
    return;
  }

  vfs->root->files = (File *)kmalloc(INITIAL_CAPACITY * sizeof(File));
  if (!vfs->root->files) {
    kprintf("Failed to allocate memory for root files.\n");
    kfree(vfs->root);
    for (int i = 0; i < MAX_FILES; i++) {
      kfree(vfs->inode_table[i]);
    }
    kfree(vfs->superblock);
    kfree(vfs);
    return;
  }

  memcpy(vfs->root->name, "root", strlen("root"));
  vfs->root->file_count = 0;
  vfs->root->capacity = INITIAL_CAPACITY;
  vfs->total_files = 0;
  vfs->total_size = 0;

  vfs->superblock->magic = MYFS_MAGIC;
  vfs->superblock->inode_count = MAX_FILES;
  vfs->superblock->root_inode = 0;
  vfs->superblock->free_blocks = MAX_DATABLOCKS;
  vfs->superblock->block_size = BLOCK_SIZE;

  for (int i = 0; i < MAX_FILES; i++) {
    vfs->inode_table[i]->mode = 0;
    vfs->inode_table[i]->size = 0;
    memset(vfs->inode_table[i]->blocks, -1,
           sizeof(vfs->inode_table[i]->blocks));
  }

  for (int i = 0; i < MAX_DATABLOCKS; i++) {
    memset(vfs->data_blocks[i].data, 0, BLOCK_SIZE);
  }

  kprintf("VFS initialized successfully.\n");
}
int create_file(const char *name, const char *data, enum vtype VTYPE) {

  if (VTYPE == -1) {
    VTYPE = VREG;
  }

  if (vfs->root->file_count >= vfs->root->capacity) {
    vfs->root->capacity *= 2;
    vfs->root->files =
        (File *)krealloc(vfs->root->files, vfs->root->capacity * sizeof(File));
  }

  int inode_num = -1;
  for (int i = 0; i < MAX_FILES; i++) {
    if (vfs->inode_table[i]->mode == 0) {
      inode_num = i;
      break;
    }
  }

  if (inode_num == -1) {
    return -1;
  }

  int block_num = -1;

  for (int i = 0; i < MAX_DATABLOCKS; i++) {
    if (vfs->data_blocks[i].data[0] == 0) {
      block_num = i;
      break;
    }
  }

  vfs->inode_table[inode_num]->mode = 0644;
  vfs->inode_table[inode_num]->size = strlen(data);
  vfs->inode_table[inode_num]->blocks[0] = block_num;

  memcpy(vfs->data_blocks[block_num].data, data, BLOCK_SIZE);

  File *file = &vfs->root->files[vfs->root->file_count];
  memcpy(file->name, name, MAX_FILENAME - 1);
  file->name[MAX_FILENAME - 1] = '\0';
  file->inode_number = inode_num;

  vfs->root->file_count++;
  vfs->total_files++;
  vfs->total_size += strlen(data);

  vfs->superblock->free_blocks--;
  vfs->superblock->inode_count--;

  kprintf("File '%s' created successfully (inode: %d, block: %d).\n", name,
          inode_num, block_num);
  return 0;
}

const char *read_file(const char *name) {
  if (!vfs || !vfs->root) {
    return NULL;
  }

  for (int i = 0; i < vfs->root->file_count; i++) {
    if (strcmp(vfs->root->files[i].name, name) == 0) {
      uint32_t inode_number = vfs->root->files[i].inode_number;
      uint32_t block_number = vfs->inode_table[inode_number]->blocks[0];
      return vfs->data_blocks[block_number].data;
    }
  }

  return NULL;
}

int delete_file(const char *name) {

  for (int i = 0; i < vfs->root->file_count; i++) {
    if (strcmp(vfs->root->files[i].name, name) == 0) {
      uint32_t inode_num = vfs->root->files[i].inode_number;

      for (int j = 0; j < MAX_BLOCK_NUMBER; j++) {
        uint32_t blocknum = vfs->inode_table[inode_num]->blocks[j];
        if (blocknum != (uint32_t)-1) {
          memset(vfs->data_blocks[blocknum].data, 0, BLOCK_SIZE);
          vfs->superblock->free_blocks++;
        }
      }
      vfs->inode_table[inode_num]->mode = 0;
      vfs->inode_table[inode_num]->size = 0;
      memset(vfs->inode_table[inode_num]->blocks, -1,
             sizeof(vfs->inode_table[inode_num]->blocks));

      vfs->superblock->inode_count++;
      for (int j = i; j < vfs->root->file_count - 1; j++) {
        vfs->root->files[j] = vfs->root->files[j + 1];
      }

      vfs->root->file_count--;
      vfs->total_files--;
      vfs->total_size -= vfs->inode_table[inode_num]->size;

      kprintf("File '%s' deleted successfully.\n", name);
      return 0;
    }
  }
  kprintf("File not Found");
  return -1;
}

int create_directory(const char *name) {
  if (strlen(name) > MAX_FILENAME) {
    kprintf("Name overflow");
    return -1;
  }

  for (int i = 0; i < vfs->root->file_count; i++) {
    if (memcmp(vfs->root->files[i].name, name, strlen(name)) == 0) {
      kprintf("Already Directory exist %s\n", name);
    }
  }

  int inode_number = -1;
  for (int i = 0; i < MAX_FILES; i++) {
    if (vfs->inode_table[i]->mode == 0) {
      inode_number = i;
      break;
    }
  }

  Directory *dir = (Directory *)kmalloc(sizeof(Directory));
  if (!dir) {
    return -1;
  }

  memcpy(dir->name, name, MAX_FILENAME - 1);
  dir->name[MAX_FILENAME - 1] = '\0';
  dir->file_count = 0;
  dir->capacity = INITIAL_CAPACITY;
  dir->files = (File *)kmalloc(dir->capacity * sizeof(File));
  if (!dir->files) {
    kprintf("Failed to allocate memory for directory files");
    kfree(dir);
    return -1;
  }

  vfs->inode_table[inode_number]->mode = 0755;
  vfs->inode_table[inode_number]->size = 0;
  memset(vfs->inode_table[inode_number]->blocks, -1,
         sizeof(vfs->inode_table[inode_number]->blocks));

  if (vfs->root->file_count >= vfs->root->capacity) {
    vfs->root->capacity *= 2;
    vfs->root->files =
        (File *)krealloc(vfs->root->files, vfs->root->capacity * sizeof(File));
    if (!vfs->root->files) {
      kfree(dir->files);
      kfree(dir);
      return -1;
    }
  }

  File *new_file = &vfs->root->files[vfs->root->file_count];
  memcpy(new_file->name, name, MAX_FILENAME - 1);
  new_file->name[MAX_FILENAME - 1] = '\0';
  new_file->inode_number = inode_number;

  vfs->root->file_count++;
  vfs->total_files++;

  vfs->superblock->inode_count--;

  kprintf("Directory '%s' created successfully (inode: %d).\n", name,
          inode_number);
  return 0;
}

void display_all_files() {

  kprintf("--ALL FILES--\n");
  for (int i = 0; i < vfs->root->file_count; i++) {
    kprintf("%s ", vfs->root->files[i].name);
  }

  kprintf("---------------\n");
}

void display_content(const char *name) {

  int inode_num = -1;
  for (int i = 0; i < vfs->root->file_count; i++) {
    if (strcmp(vfs->root->files[i].name, name) == 0) {
      inode_num = vfs->root->files[i].inode_number;
    }
  }

  struct myfs_inode *inode = vfs->inode_table[inode_num];
  kprintf("Content of file '%s':\n", name);
  for (int i = 0; i < MAX_BLOCK_NUMBER; i++) {
    uint32_t block_number = inode->blocks[i];
    if (block_number != (uint32_t)-1) {
      kprintf("%s", vfs->data_blocks[block_number].data);
    }
  }
  kprintf("\n");
}
