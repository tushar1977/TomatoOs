#include "nvme.h"
#include "apic_timer.h"
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

void nvme_submit_admin(struct nvme_controller *ctrl, struct nvme_command *cmd) {
  volatile struct nvme_command *sq =
      (volatile struct nvme_command *)(kernel.hhdm + ctrl->admin_sq.address);

  memcpy((void *)&sq[ctrl->admin_sq_tail], cmd, sizeof(struct nvme_command));

  ctrl->admin_sq_tail++;

  if (ctrl->admin_sq_tail > 63)
    ctrl->admin_sq_tail = 0;

  memory_barrier();

  volatile uint32_t *sq_db =
      (volatile uint32_t *)((uint8_t *)ctrl->bar0 + 0x1000);
  *sq_db = ctrl->admin_sq_tail;

  memory_barrier();
}

bool nvme_wait_admin(struct nvme_controller *ctrl, uint64_t timeout_ms) {

  volatile struct nvme_completion *cq =
      (volatile struct nvme_completion *)(ctrl->admin_cq.address + kernel.hhdm);
  uint64_t elapsed = 0;

  while (elapsed < timeout_ms * 1000) {
    memory_barrier();
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

  struct nvme_pair_queue *queue = kmalloc(sizeof(struct nvme_pair_queue *));
  nvme_alloc_queue(&queue->cq);
  nvme_alloc_queue(&queue->sq);
  queue->phase = 1;
  queue->qid = nvme_qid_ptr;
  spinlock_release(&queue->lock);

  struct nvme_command cmd;
  memset(&cmd, 0, sizeof(struct nvme_command));

  // Allocates this queue wrt nvme controller
  cmd.cdw0 = 0x05;
  cmd.cdw10 = ((uint32_t)(1)) | (((uint32_t)queue->qid) << 16);
  cmd.cdw11_15[0] = 1;
  cmd.prp1 = queue->cq.address;

  nvme_submit_admin(ctrl, &cmd);
  bool status = nvme_wait_admin(ctrl, 1000);

  if (status == false)
    return NULL;

  memset(&cmd, 0, sizeof(struct nvme_command));

  cmd.cdw0 = 0x01;
  cmd.cdw10 = ((uint32_t)(1)) | (((uint32_t)queue->qid) << 16);
  cmd.cdw11_15[0] = (((uint32_t)queue->qid) << 16) | 0x1;
  cmd.prp1 = queue->sq.address;

  nvme_submit_admin(ctrl, &cmd);
  status = nvme_wait_admin(ctrl, 1000);

  if (status == false)
    return NULL;

  uint8_t *doorbell_base = (uint8_t *)ctrl->bar0 + 0x1000;
  uint32_t stride_bytes = ctrl->stride;
  queue->sq_doorbell =
      (uint32_t *)(doorbell_base + ((2 * queue->qid) * stride_bytes));
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
  cmd.cdw10 = 1;
  cmd.nsid = 0;

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
  struct nvme_namespace *ns = &ctrl->namespaces[nsid - 1];

  if (!ns->valid) {
    klog(KLOG_WARN, "NVME");
    kprintf("init_namespace called on invalid namespace %d\n", nsid);
    return;
  }

  bool read_only = (ns->ns_data->nsattr & (1 << 0)) != 0;

  uint16_t shift = 0;
  uint32_t sz = ns->lba_size;
  while (sz > 1) {
    sz >>= 1;
    shift++;
  }
  ns->lba_shift = shift;

  klog(KLOG_INFO, "NVME");
  kprintf("namespace %d ready: %d-byte sectors, %s\n", nsid, ns->lba_size,
          read_only ? "read-only" : "read-write");
}

bool nvme_init_namespaces(struct nvme_controller *ctrl) {
  void *ns_list_phys = (void *)pmm_alloc_page(ctrl->page_size);
  void *ns_list = (void *)(kernel.hhdm + (uint64_t)ns_list_phys);

  struct nvme_command cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cdw0 = 0x06; // Identify opcode
  cmd.cdw10 = 0x2; // CNS=2: Identify Active Namespace ID List
  cmd.prp1 =
      (uint64_t)ns_list_phys; // physical address, not the virtual pointer

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

  uint16_t cmd = pci_read_config16(bus, device, function, 0x04);
  pci_write_config16(bus, device, function, 0x04, cmd | 0x0006);

  uint64_t bar_phys = ((uint64_t)pci.bar1 << 32) | (pci.bar0 & 0xFFFFFFF0);
  map_page(kernel.hhdm + bar_phys, bar_phys,
           PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE, 4);

  struct nvme_controller *ctrl = kmalloc(sizeof(struct nvme_controller));
  memset(ctrl, 0, sizeof(*ctrl));
  ctrl->bar0 = (volatile void *)(kernel.hhdm + bar_phys);

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

  nvme_write32(ctrl, NVME_REG_CC,
               nvme_read32(ctrl, 0x14) & ~(uint32_t)NVME_CC_EN);
  while (nvme_read32(ctrl, NVME_REG_CSTS) & 1) {
    pause();
  }

  void *sq_phys = (void *)pmm_alloc_page(4096);
  void *cq_phys = (void *)pmm_alloc_page(4096);

  ctrl->admin_sq.address = (uint64_t)sq_phys;
  ctrl->admin_sq.size = 63;
  ctrl->admin_cq.address = (uint64_t)cq_phys;
  ctrl->admin_cq.size = 63;

  nvme_write32(ctrl, NVME_REG_AQA, 63 | (63 << 16));
  nvme_write64(ctrl, NVME_REG_ASQ, ctrl->admin_sq.address);
  nvme_write64(ctrl, NVME_REG_ACQ, ctrl->admin_cq.address);

  uint32_t cc_val = NVME_CC_CSS_NVM | NVME_CC_MPS(host_mps_encoding) |
                    NVME_CC_AMS_RR | NVME_CC_IOSQES(6) | NVME_CC_IOCQES(4);
  nvme_write32(ctrl, NVME_REG_CC, cc_val);
  nvme_write32(ctrl, NVME_REG_CC, cc_val | NVME_CC_EN);

  while (!(nvme_read32(ctrl, NVME_REG_CSTS) & 1)) {
    pause();
  }

  if (!nvme_identify(ctrl)) {
    klog(KLOG_ERROR, "NVME");
    kprintf("failed to identify controller\n");
    return;
  }

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
