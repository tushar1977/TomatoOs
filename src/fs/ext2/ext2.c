#include "ext2.h"
#include "disk.h"
#include "kernel.h"
#include "klog.h"
#include "pmm.h"
#include "printf.h"
#include "spinlock.h"
#include <string.h>

bool ext2_init(struct ext2_partition *fs, struct disk *target_disk,
               uint64_t lba_start) {
  memset(fs, 0, sizeof(*fs));
  fs->disk = target_disk;
  fs->lba_start = lba_start;

  if (kernel.ext2_partition_count >= (int)(sizeof(kernel.ext2_partitions) /
                                           sizeof(kernel.ext2_partitions[0]))) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("too many ext2 partitions\n");
    return false;
  }

  uint64_t phys = pmm_alloc_page(4096);
  if (phys == 0) {
    klog(KLOG_ERROR, "EXT2");
    kprintf("out of memory for io buffer\n");
    return false;
  }
  fs->io_buf = (char *)(kernel.hhdm + phys);

  if (!ext2_fill_super(fs)) {
    klog(KLOG_WARN, "EXT2");
    kprintf("mount failed\n");
    pmm_free(phys, 1);
    return false;
  }

  kernel.ext2_partitions[kernel.ext2_partition_count++] = fs;
  if (kernel.root_fs == NULL)
    kernel.root_fs = fs;
  return true;
}
