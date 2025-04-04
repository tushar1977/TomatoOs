#include "../include/vfs.h"
#include "../include/memory.h"
#include "../include/string.h"

VFS *init_vfs() {
  VFS *vfs = (VFS *)malloc(sizeof(VFS));
  if (vfs == NULL) {
    printf("Error: Failed to allocate VFS structure\n");
    return NULL;
  }

  vfs->superblock = (Superblock *)malloc(sizeof(Superblock));
  if (vfs->superblock == NULL) {
    free(vfs);
    return NULL;
  }

  for (int i = 0; i < MAX_FILES; i++) {
    vfs->inode_table[i] = (Inode *)malloc(sizeof(Inode));
    if (vfs->inode_table[i] == NULL) {
      printf("Error: Failed to allocate inode\n");
      for (int j = 0; j < i; j++) {
        free(vfs->inode_table[j]);
      }
      free(vfs->superblock);
      free(vfs);
      return NULL;
    }
    memset(vfs->inode_table[i], 0, sizeof(Inode));
    vfs->inode_table[i]->free = 1;
    vfs->inode_table[i]->mode = 0;
    vfs->inode_table[i]->size = 0;

    printf("%d: free=%d\n", i, vfs->inode_table[i]->free);

    for (int j = 0; j < MAX_BLOCK_NUMBER; j++) {
      vfs->inode_table[i]->blocks[j] = -1;
    }
  }

  vfs->root = (Directory *)malloc(sizeof(Directory));
  if (vfs->root == NULL) {
    printf("Error: Failed to allocate root directory\n");
    for (int i = 0; i < MAX_FILES; i++) {
      free(vfs->inode_table[i]);
    }
    free(vfs->superblock);
    free(vfs);
    return NULL;
  }

  vfs->root->files = (File *)malloc(INITIAL_CAPACITY * sizeof(File));
  if (vfs->root->files == NULL) {
    printf("Error: Failed to allocate file array for root directory\n");
    free(vfs->root);
    for (int i = 0; i < MAX_FILES; i++) {
      free(vfs->inode_table[i]);
    }
    free(vfs->superblock);
    free(vfs);
    return NULL;
  }
  strncpy(vfs->root->name, "root", sizeof(vfs->root->name) - 1);
  vfs->root->name[sizeof(vfs->root->name) - 1] = '\0';
  vfs->root->file_count = 0;
  vfs->root->capacity = INITIAL_CAPACITY;

  vfs->total_files = 0;
  vfs->total_size = 0;

  vfs->superblock->magic = MYFS_MAGIC;
  vfs->superblock->root_inode = 0;
  vfs->superblock->inode_count = MAX_FILES;
  vfs->superblock->free_blocks = MAX_DATABLOCKS;
  vfs->superblock->free_inodes = MAX_FILES;
  vfs->superblock->block_size = BLOCK_SIZE;

  for (int i = 0; i < MAX_DATABLOCKS; i++) {
    vfs->data_blocks[i].free = 1;
  }

  printf("Total inodes%d\n", vfs->superblock->inode_count);
  printf("VFS name %s\n", vfs->root->name);
  return vfs;
}
void create_file(VFS *vfs, const char *name, const char *data,
                 enum vtype VTYPE) {
  // Check if we need to expand the files array
  printf("DEBUG: Current inode status before allocation:\n");
  for (int i = 0; i < 5; i++) { // Just show first 5 for brevity
    printf("Inode %d: free=%d\n", i, vfs->inode_table[i]->free);
  }
  if (vfs->root->file_count >= vfs->root->capacity) {
    vfs->root->capacity *= 2;
    File *nf =
        (File *)realloc(vfs->root->files, vfs->root->capacity * sizeof(File));
    if (nf == NULL) {
      printf("Error: Memory allocation failed\n");
      return;
    }
    vfs->root->files = nf;
  }

  // Find a free inode
  int inode_num = -1;
  for (int i = 0; i < MAX_FILES; i++) {
    if (vfs->inode_table[i]->free == 1) {

      inode_num = i;
      printf("DEBUG: Selected inode %d as free\n", inode_num);
      vfs->inode_table[i]->free = 0;
      vfs->inode_table[i]->mode = 0644;
      vfs->inode_table[i]->size = strlen(data);
      memset(vfs->inode_table[i]->blocks, -1,
             sizeof(vfs->inode_table[i]->blocks));
      break;
    }
  }

  if (inode_num == -1) {
    printf("Error: No free inodes available\n");
    return;
  }

  int remaining_size = strlen(data);
  int block_index = 0; // Track which block entry we're using in the inode

  // Allocate blocks for the file data
  while (remaining_size > 0 && block_index < MAX_BLOCK_NUMBER) {
    // Find a free data block
    int free_block = -1;
    for (int i = 0; i < MAX_DATABLOCKS; i++) {
      if (vfs->data_blocks[i].free) {
        free_block = i;
        vfs->data_blocks[i].free = 0;
        break;
      }
    }

    if (free_block == -1) {
      printf("Error: No free data blocks available\n");
      return;
    }

    int copy_size = (remaining_size < BLOCK_SIZE) ? remaining_size : BLOCK_SIZE;
    memcpy(vfs->data_blocks[free_block].data, data + (block_index * BLOCK_SIZE),
           copy_size);

    vfs->inode_table[inode_num]->blocks[block_index] = free_block;

    block_index++;
    remaining_size -= copy_size;
  }

  File *file = &vfs->root->files[vfs->root->file_count];
  strncpy(file->name, name, MAX_FILENAME - 1);
  file->name[MAX_FILENAME - 1] = '\0';
  file->inode_number = inode_num;

  // Update filesystem stats
  vfs->root->file_count++;
  vfs->total_files++;
  vfs->total_size += strlen(data);
  vfs->superblock->free_blocks -= block_index;

  // Update free inodes count
  if (vfs->superblock->free_inodes > 0) {
    vfs->superblock->free_inodes--;
  }
  printf("DEBUG: Inode status after allocation:\n");
  for (int i = 0; i < 5; i++) {
    printf("Inode %d: free=%d\n", i, vfs->inode_table[i]->free);
  }

  printf("File '%s' created successfully (inode: %d, blocks used: %d).\n", name,
         inode_num, block_index);
}
int delete_file(VFS *vfs, const char *name) {

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

      printf("File '%s' deleted successfully.\n", name);
      return 0;
    }
  }
  printf("File not Found");
  return -1;
}

int create_directory(VFS *vfs, const char *name) {
  if (strlen(name) > MAX_FILENAME) {
    printf("Name overflow");
    return -1;
  }

  for (int i = 0; i < vfs->root->file_count; i++) {
    if (memcmp(vfs->root->files[i].name, name, strlen(name)) == 0) {
      printf("Already Directory exist %s\n", name);
    }
  }

  int inode_number = -1;
  for (int i = 0; i < MAX_FILES; i++) {
    if (vfs->inode_table[i]->mode == 0) {
      inode_number = i;
      break;
    }
  }

  Directory *dir = (Directory *)malloc(sizeof(Directory));
  if (!dir) {
    return -1;
  }

  memcpy(dir->name, name, MAX_FILENAME - 1);
  dir->name[MAX_FILENAME - 1] = '\0';
  dir->file_count = 0;
  dir->capacity = INITIAL_CAPACITY;
  dir->files = (File *)malloc(dir->capacity * sizeof(File));
  if (!dir->files) {
    printf("Failed to allocate memory for directory files");
    free(dir);
    return -1;
  }

  vfs->inode_table[inode_number]->mode = 0755;
  vfs->inode_table[inode_number]->size = 0;
  memset(vfs->inode_table[inode_number]->blocks, -1,
         sizeof(vfs->inode_table[inode_number]->blocks));

  if (vfs->root->file_count >= vfs->root->capacity) {
    vfs->root->capacity *= 2;
    vfs->root->files =
        (File *)realloc(vfs->root->files, vfs->root->capacity * sizeof(File));
    if (!vfs->root->files) {
      free(dir->files);
      free(dir);
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

  printf("Directory '%s' created successfully (inode: %d).\n", name,
         inode_number);
  return 0;
}

void display_all_files(VFS *vfs) {
  printf("--ALL FILES--\n");
  printf("Total files: %d\n", vfs->root->file_count);
  for (int i = 0; i < vfs->root->file_count; i++) {
    vfs->root->files[i].name[MAX_FILENAME - 1] = '\0';
    printf("%s\n", vfs->root->files[i].name);
  }
  printf("---------------\n");
}
void display_content(VFS *vfs, const char *name) {
  int inode_num = -1;
  for (int i = 0; i < vfs->root->file_count; i++) {
    if (strcmp(vfs->root->files[i].name, name) == 0) {
      inode_num = vfs->root->files[i].inode_number;
      break; // Exit loop once the file is found
    }
  }

  if (inode_num == -1) {
    printf("Error: File '%s' not found.\n", name);
    return;
  }

  if (inode_num < 0 || inode_num >= MAX_FILES || !vfs->inode_table[inode_num]) {
    printf("Error: Invalid inode number %d for file '%s'.\n", inode_num, name);
    return;
  }

  Inode *inode = vfs->inode_table[inode_num];
  printf("Content of file '%s':\n", name);

  for (int i = 0; i < MAX_BLOCK_NUMBER; i++) {
    uint32_t block_number = inode->blocks[i];
    if (block_number != (uint32_t)-1 && block_number < MAX_DATABLOCKS) {
      printf("%s", vfs->data_blocks[block_number].data);
    }
  }
  printf("\n");
}
