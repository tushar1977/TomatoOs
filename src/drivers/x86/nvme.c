#include "nvme.h"
#include "apic_timer.h"
#include "disk.h"
#include "include/pmm.h"
#include "include/printf.h"
#include "include/util.h"
#include "kernel.h"
#include "klog.h"
#include "kmem.h"
#include "paging.h"
#include "pci.h"
#include "spinlock.h"
#include <stdint.h>

void nvme_send_io(struct nvme_pair_queue *queue, uint8_t opcode, uint32_t nsid,
                  void *buffer, uint64_t lba, uint16_t num_blocks,
                  size_t transfer_bytes) {
  struct nvme_command cmd;
  memset(&cmd, 0, sizeof(cmd));

  cmd.cdw0 = opcode;
  cmd.nsid = nsid;
  cmd.cdw10 = (uint32_t)lba;
  cmd.cdw11_15[0] = (uint32_t)(lba >> 32);
  cmd.cdw11_15[1] = (uint32_t)(num_blocks - 1);

  void *data_phys = getPhysicalAddress(buffer);
  if (data_phys == NULL) {
    kprintf("[nvme] send_io failed: buffer %x not mapped\n", (uint64_t)buffer);
    return;
  }
  cmd.prp1 = (uint64_t)data_phys;

  size_t num_pages = (transfer_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
  uint64_t *list = NULL;
  void *list_phys = NULL;

  if (num_pages == 1) {
    cmd.prp2 = 0;
  } else if (num_pages == 2) {
    void *second_page_phys = getPhysicalAddress((char *)buffer + PAGE_SIZE);
    if (second_page_phys == NULL) {
      kprintf("[nvme] send_io failed: second page of buffer not mapped\n");
      return;
    }
    cmd.prp2 = (uint64_t)second_page_phys;
  } else {

    list_phys = (void *)pmm_alloc_page(PAGE_SIZE);
    list = (uint64_t *)(kernel.hhdm + (uint64_t)list_phys);

    for (size_t i = 1; i < num_pages; i++) {
      void *page_phys = getPhysicalAddress((char *)buffer + (i * PAGE_SIZE));
      if (page_phys == NULL) {
        kprintf("[nvme] send_io failed: page %d of buffer not mapped\n",
                (int)i);
        pmm_free((uint64_t)list_phys, 1);
        return;
      }
      list[i - 1] = (uint64_t)page_phys;
    }
    cmd.prp2 = (uint64_t)list_phys;
  }

  volatile struct nvme_command *sq =
      (volatile struct nvme_command *)(kernel.hhdm + queue->sq.address);
  memcpy((void *)&sq[queue->sq_tail], &cmd, sizeof(struct nvme_command));

  memory_barrier();

  queue->sq_tail = (queue->sq_tail + 1) % (queue->sq.size + 1);
  *queue->sq_doorbell = queue->sq_tail;

  memory_barrier();
}

bool nvme_wait_io(struct nvme_pair_queue *queue,
                  struct nvme_completion *out_completion, uint64_t timeout_ms) {
  volatile struct nvme_completion *cq =
      (volatile struct nvme_completion *)(kernel.hhdm + queue->cq.address);
  uint64_t elapsed = 0;

  while (elapsed < timeout_ms) {
    memory_barrier();

    if ((cq[queue->cq_head].status & 0x1) == queue->phase) {
      uint16_t status_code = (cq[queue->cq_head].status >> 1) & 0x7FFF;

      if (out_completion) {
        *out_completion = cq[queue->cq_head];
      }

      queue->cq_head = (queue->cq_head + 1) % (queue->cq.size + 1);
      if (queue->cq_head == 0) {
        queue->phase ^= 1;
      }

      *queue->cq_doorbell = (uint32_t)queue->cq_head;
      memory_barrier();

      return status_code == 0;
    }

    sleep(10);
    elapsed += 10;
  }

  kprintf("[nvme] I/O wait timed out\n");
  return false;
}

bool nvme_read(void *arg, char *buffer, uint64_t lba, size_t len_in_blocks) {
  struct nvme_arg_disk *disk = (struct nvme_arg_disk *)arg;
  size_t lba_size = disk->ctrl->namespaces[disk->nsid - 1].lba_size;

  if (((uint64_t)buffer & (PAGE_SIZE - 1)) != 0) {
    kprintf("[nvme] read failed: unaligned buffer %x\n", (uint64_t)buffer);
    return false;
  }

  size_t transfer_bytes = len_in_blocks * lba_size;

  spinlock_aquire(&disk->ctrl->main_io_queue->lock);
  nvme_send_io(disk->ctrl->main_io_queue, 0x02, disk->nsid, buffer, lba,
               (uint16_t)len_in_blocks, transfer_bytes);
  struct nvme_completion completion;
  bool ok = nvme_wait_io(disk->ctrl->main_io_queue, &completion, 1000);

  spinlock_release(&disk->ctrl->main_io_queue->lock);
  if (!ok) {
    kprintf("[nvme] read timed out\n");
    return false;
  }

  uint16_t status_code = (completion.status >> 1) & 0x7FFF;
  if (status_code != 0) {
    kprintf("[nvme] read failed, status=%x\n", status_code);
    return false;
  }

  return true;
}

bool nvme_write(void *arg, char *buffer, uint64_t lba, size_t len_in_blocks) {
  struct nvme_arg_disk *disk = (struct nvme_arg_disk *)arg;
  size_t lba_size = disk->ctrl->namespaces[disk->nsid - 1].lba_size;

  if (((uint64_t)buffer & (PAGE_SIZE - 1)) != 0) {
    kprintf("[nvme] write failed: unaligned buffer %x\n", (uint64_t)buffer);
    return false;
  }

  size_t transfer_bytes = len_in_blocks * lba_size;

  nvme_send_io(disk->ctrl->main_io_queue, 0x01, disk->nsid, buffer, lba,
               (uint16_t)len_in_blocks, transfer_bytes);

  struct nvme_completion completion;
  bool ok = nvme_wait_io(disk->ctrl->main_io_queue, &completion, 5000);

  if (!ok) {
    kprintf("[nvme] write timed out\n");
    return false;
  }

  uint16_t status_code = (completion.status >> 1) & 0x7FFF;
  if (status_code != 0) {
    kprintf("[nvme] write failed, status=%x\n", status_code);
    return false;
  }

  return true;
}

void nvme_submit_admin(struct nvme_controller *ctrl, struct nvme_command *cmd) {
  // get the virtual address of admin queue
  volatile struct nvme_command *sq =
      (volatile struct nvme_command *)(kernel.hhdm + ctrl->admin_sq.address);

  // copy the command to admin queue
  memcpy((void *)&sq[ctrl->admin_sq_tail], cmd, sizeof(struct nvme_command));

  ctrl->admin_sq_tail++;

  if (ctrl->admin_sq_tail > 63)
    ctrl->admin_sq_tail = 0;

  memory_barrier();

  // locate the admin submission queue tail doorbell
  volatile uint32_t *sq_db =
      (volatile uint32_t *)((uint8_t *)ctrl->bar0 + 0x1000);
  // notify the nvme controller that new command is available
  *sq_db = ctrl->admin_sq_tail;

  memory_barrier();
}

bool nvme_wait_admin(struct nvme_controller *ctrl, uint64_t timeout_ms) {

  // obtain the virtual address for admin completion queue
  volatile struct nvme_completion *cq =
      (volatile struct nvme_completion *)(ctrl->admin_cq.address + kernel.hhdm);
  uint64_t elapsed = 0;

  // wait for reply
  while (elapsed < timeout_ms * 1000) {
    memory_barrier();
    // check the phase tag in completion entry
    if ((cq[ctrl->admin_cq_head].status & 0x01) == ctrl->admin_phase) {

      uint16_t status_code = (cq[ctrl->admin_cq_head].status >> 1) & 0xFF;

      ctrl->admin_cq_head++;
      if (ctrl->admin_cq_head > 63) {
        ctrl->admin_cq_head = 0;
        ctrl->admin_phase ^= 1;
      }

      volatile uint32_t *cq_db =
          (volatile uint32_t *)((uint8_t *)ctrl->bar0 + 0x1000 +
                                (1 * ctrl->stride));
      *cq_db = ctrl->admin_cq_head;
      memory_barrier();

      if (status_code != 0) {
        klog(KLOG_ERROR, "NVME");
        kprintf("admin command failed, status=%x\n", status_code);
        return false;
      }

      return true;
    }

    sleep(10);
    elapsed += 10;
  }

  klog(KLOG_ERROR, "NVME");
  kprintf("admin command timed out after %d ms\n", timeout_ms);
  return false;
}

static void nvme_alloc_queue(struct nvme_queue *q) {
  void *phys = (void *)pmm_alloc_page(4096);
  memset((void *)(kernel.hhdm + (uint64_t)phys), 0, 4096);
  q->address = (uint64_t)phys;
  q->size = 63;
}

uint8_t nvme_qid_ptr = 1;

struct nvme_pair_queue *nvme_create_io_queue(struct nvme_controller *ctrl) {

  struct nvme_pair_queue *queue = kmalloc(sizeof(struct nvme_pair_queue));
  nvme_alloc_queue(&queue->cq);
  nvme_alloc_queue(&queue->sq);
  queue->phase = 1;
  queue->qid = nvme_qid_ptr;
  spinlock_release(&queue->lock);

  struct nvme_command cmd;
  memset(&cmd, 0, sizeof(struct nvme_command));

  // Allocates cq queue wrt nvme controller
  cmd.cdw0 = 0x05;
  cmd.cdw10 = ((uint32_t)queue->cq.size << 16) | queue->qid;
  cmd.cdw11_15[0] = 0x1;
  cmd.prp1 = queue->cq.address;

  nvme_submit_admin(ctrl, &cmd);
  bool status = nvme_wait_admin(ctrl, 1000);
  kprintf("[nvme] create IOSQ status=%d\n", status);
  if (status == false)
    return NULL;

  memset(&cmd, 0, sizeof(struct nvme_command));

  // create sq queue
  cmd.cdw0 = 0x01;
  cmd.cdw10 = ((uint32_t)queue->sq.size << 16) | queue->qid;
  cmd.cdw11_15[0] = ((uint32_t)queue->qid << 16) | 0x1;
  cmd.prp1 = queue->sq.address;

  nvme_submit_admin(ctrl, &cmd);
  status = nvme_wait_admin(ctrl, 1000);

  if (status == false)
    return NULL;

  // sq[i] doorbell = BAR0 + 0x1000 + 2*qid*stride
  uint8_t *doorbell_base = (uint8_t *)ctrl->bar0 + 0x1000;
  // SQ doorbell at index (2 * QID).
  uint32_t stride_bytes = ctrl->stride;
  queue->sq_doorbell =
      (uint32_t *)(doorbell_base + ((2 * queue->qid) * stride_bytes));
  // CQ doorbell is at index (2 * QID + 1).
  queue->cq_doorbell =
      (uint32_t *)(doorbell_base + ((2 * queue->qid + 1) * stride_bytes));
  nvme_qid_ptr++;

  klog(KLOG_INFO, "NVME");
  kprintf("I/O queue %d created\n", queue->qid);

  return queue;
}

bool nvme_identify(struct nvme_controller *ctrl) {

  void *phys_buffer = (void *)pmm_alloc_page(ctrl->page_size);
  struct nvme_command cmd = {0};
  cmd.cdw0 = 0x06; // Identify opcode
  cmd.prp1 = (uint64_t)phys_buffer;
  cmd.cdw10 = 1; // request identify controller data
  cmd.nsid = 0;  // no namespace to target

  nvme_submit_admin(ctrl, &cmd);

  bool status = nvme_wait_admin(ctrl, 1000);
  if (status == false)
    return false;
  memory_barrier();

  char model[41];
  memcpy(model, (void *)(phys_buffer + kernel.hhdm + 24), 40);
  model[40] = '\0';
  klog(KLOG_INFO, "NVME");

  kprintf("controller model: %s\n", model);

  ctrl->identify = (void *)(phys_buffer + kernel.hhdm);

  return true;
}

bool nvme_identify_namespace(struct nvme_controller *ctrl, uint32_t nsid) {
  struct nvme_namespace *ns = &ctrl->namespaces[nsid - 1];

  void *ns_data_phys = (void *)pmm_alloc_page(ctrl->page_size);
  ns->ns_data = (struct nvme_id_ns *)(kernel.hhdm + (uint64_t)ns_data_phys);

  struct nvme_command cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cdw0 = 0x06; // Identify opcode
  cmd.nsid = nsid;
  cmd.cdw10 = 0x0; // CNS=0: Identify Namespace
  cmd.prp1 = (uint64_t)
      ns_data_phys; // physical address, not derived by subtracting hhdm

  nvme_submit_admin(ctrl, &cmd);
  bool result = nvme_wait_admin(ctrl, 1000);

  if (!result) {
    return false;
  }

  ns->nsid = nsid;
  ns->size = ns->ns_data->nsze;

  uint8_t lba_format = ns->ns_data->flbas & 0xF;
  if (lba_format < ns->ns_data->nlbaf) {
    ns->lba_size = 1u << ns->ns_data->lbaf[lba_format].ds;
  } else {
    ns->lba_size = 512;
  }
  ns->valid = true;

  klog(KLOG_INFO, "NVME");
  kprintf("namespace %d: lba_size=%d bytes, total=%d GiB\n", nsid, ns->lba_size,
          (ns->size * ns->lba_size) / (1024 * 1024 * 1024));

  return true;
}

void nvme_init_namespace(struct nvme_controller *ctrl, uint32_t nsid) {
  struct nvme_arg_disk *new_disk = kmalloc(sizeof(struct nvme_arg_disk));
  new_disk->ctrl = ctrl;
  new_disk->nsid = nsid;

  struct disk *generic_disk = (struct disk *)kmalloc(sizeof(struct disk));
  generic_disk->arg = new_disk;
  generic_disk->lba_size = ctrl->namespaces[nsid - 1].lba_size;
  generic_disk->read = nvme_read;
  generic_disk->write = nvme_write;

  init_disk(generic_disk);
}

bool nvme_init_namespaces(struct nvme_controller *ctrl) {
  void *ns_list_phys = (void *)pmm_alloc_page(ctrl->page_size);
  void *ns_list = (void *)(kernel.hhdm + (uint64_t)ns_list_phys);

  struct nvme_command cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cdw0 = 0x06; // Identify opcode
  cmd.cdw10 = 0x2; // CNS=2: Identify Active Namespace ID List
  cmd.prp1 = (uint64_t)ns_list_phys;

  nvme_submit_admin(ctrl, &cmd);
  bool result = nvme_wait_admin(ctrl, 1000);

  if (!result) {
    return false;
  }

  uint32_t *ns_ids = (uint32_t *)ns_list;
  for (int i = 0; i < 1024; i++) {
    uint32_t nsid = ns_ids[i];
    if (nsid == 0) {
      break; // end of list
    }

    result = nvme_identify_namespace(ctrl, nsid);
    if (result) {
      nvme_init_namespace(ctrl, nsid);
      ctrl->num_namespaces++;
    }
  }

  return true;
}

void nvme_init(pci_t pci, uint8_t bus, uint8_t device, uint8_t function) {
  static bool nvme_subsystem_initialized = false;
  if (!nvme_subsystem_initialized) {
    kernel.nvme_controller_ptr = 0;
    nvme_subsystem_initialized = true;
  }

  if (pci.progIF != 0x02) {
    return;
  }

  // Enabling bus mastering for DMA and memory space access
  uint16_t cmd = pci_read_config16(bus, device, function, 0x04);
  pci_write_config16(bus, device, function, 0x04, cmd | 0x0006);

  // Map BAR0 to access nvme controller's MMIO reg
  uint64_t bar_phys = ((uint64_t)pci.bar1 << 32) | (pci.bar0 & 0xFFFFFFF0);
  map_page(kernel.hhdm + bar_phys, bar_phys,
           PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE, 4);

  // Allocating controller memory region
  struct nvme_controller *ctrl = kmalloc(sizeof(struct nvme_controller));
  memset(ctrl, 0, sizeof(*ctrl));
  ctrl->bar0 = (volatile void *)(kernel.hhdm + bar_phys);

  // Reading capabilities
  uint64_t cap = nvme_read64(ctrl, NVME_REG_CAP);
  ctrl->admin_phase = 1;

  uint32_t css = (cap & NVME_CAP_CSS_MASK) >> 37;

  uint32_t vs = nvme_read32(ctrl, NVME_REG_VS);
  uint16_t vs_major = (uint16_t)(vs >> 16);
  uint8_t vs_minor = (uint8_t)(vs >> 8);
  klog(KLOG_INFO, "NVME");
  kprintf("controller version %d.%d\n", vs_major, vs_minor);
  if (vs_major < 1) {
    klog(KLOG_ERROR, "NVME");
    kprintf("unsupported controller version\n");
    return;
  }

  if (!(css & 0x1)) {
    klog(KLOG_ERROR, "NVME");
    kprintf("controller does not support the NVM command set (css=%x)\n", css);
    return;
  }

  uint32_t mpsmin = (cap & NVME_CAP_MPSMIN_MASK) >> 48;
  uint32_t mpsmax = (cap & NVME_CAP_MPSMAX_MASK) >> 52;
  uint32_t dstrd = (cap >> 32) & 0xf;
  uint32_t host_mps_encoding = 0;

  // Checking the page size for controller
  if (host_mps_encoding < mpsmin || host_mps_encoding > mpsmax) {
    klog(KLOG_ERROR, "NVME");
    kprintf("controller doesn't support 4KB pages (min=%d max=%d)\n", mpsmin,
            mpsmax);
    return;
  }

  ctrl->stride = 4u << dstrd;
  ctrl->mpsmin = (uint8_t)mpsmin;
  ctrl->page_size = 4096;
  ctrl->max_queue_entries = (cap & 0xffff) + 1;

  // Disable the controller and wait until it becomes not ready.
  nvme_write32(ctrl, NVME_REG_CC,
               nvme_read32(ctrl, 0x14) & ~(uint32_t)NVME_CC_EN);
  while (nvme_read32(ctrl, NVME_REG_CSTS) & 1) {
    pause();
  }

  // allocate memory for the admin submission and completion Queues.
  void *sq_phys = (void *)pmm_alloc_page(4096);
  void *cq_phys = (void *)pmm_alloc_page(4096);

  ctrl->admin_sq.address = (uint64_t)sq_phys;
  ctrl->admin_sq.size = 63;
  ctrl->admin_cq.address = (uint64_t)cq_phys;
  ctrl->admin_cq.size = 63;

  // configure the admin queue size and base addresses
  nvme_write32(ctrl, NVME_REG_AQA, 63 | (63 << 16));
  nvme_write64(ctrl, NVME_REG_ASQ, ctrl->admin_sq.address);
  nvme_write64(ctrl, NVME_REG_ACQ, ctrl->admin_cq.address);

  uint32_t cc_val = NVME_CC_CSS_NVM | NVME_CC_MPS(host_mps_encoding) |
                    NVME_CC_AMS_RR | NVME_CC_IOSQES(6) | NVME_CC_IOCQES(4);
  nvme_write32(ctrl, NVME_REG_CC, cc_val);
  nvme_write32(ctrl, NVME_REG_CC, cc_val | NVME_CC_EN);

  // wait for controller to be ready
  while (!(nvme_read32(ctrl, NVME_REG_CSTS) & 1)) {
    pause();
  }

  // Identify the controller and retrieve its capabilities
  if (!nvme_identify(ctrl)) {
    klog(KLOG_ERROR, "NVME");
    kprintf("failed to identify controller\n");
    return;
  }

  // create the io queues
  ctrl->main_io_queue = nvme_create_io_queue(ctrl);

  if (ctrl->main_io_queue == NULL) {
    klog(KLOG_ERROR, "NVME");
    kprintf("failed to create I/O queue\n");
    return;
  }

  if (!nvme_init_namespaces(ctrl)) {
    klog(KLOG_ERROR, "NVME");
    kprintf("failed to init namespaces\n");
  }

  klog(KLOG_INFO, "NVME");
  kprintf("NVME initialised!\n");
  kernel.nvme_controllers[kernel.nvme_controller_ptr++] = ctrl;
}
