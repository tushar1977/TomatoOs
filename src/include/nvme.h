#pragma once
#include "pci.h"
#include "spinlock.h"
#include "stdbool.h"
#include "stdint.h"
#include <stddef.h>

#define NVME_REG_CAP 0x00         // Controller Capabilities
#define NVME_REG_VS 0x08          // Version
#define NVME_REG_CC 0x14          // Controller Configuration
#define NVME_REG_CSTS 0x1C        // Controller Status
#define NVME_REG_AQA 0x24         // Admin Queue Attributes
#define NVME_REG_ASQ 0x28         // Admin Submission Queue Base Address
#define NVME_REG_ACQ 0x30         // Admin Completion Queue Base Address
#define NVME_CAP_MQES_MASK 0xFFFF // Maximum Queue Entries Supported

#define NVME_CAP_CQR (1ULL << 16)           // Contiguous Queues Required
#define NVME_CAP_DSTRD_MASK (0xFULL << 32)  // Doorbell Stride
#define NVME_CAP_CSS_MASK (0xFFULL << 37)   // Command Sets Supported
#define NVME_CAP_MPSMIN_MASK (0xFULL << 48) // Memory Page Size Minimum
#define NVME_CAP_MPSMAX_MASK (0xFULL << 52) // Memory Page Size Maximum
#define NVME_CC_EN (1 << 0)                 // Enable
#define NVME_CC_CSS_NVM (0 << 4)            // NVM Command Set
#define NVME_CC_SHN_NORMAL (1 << 14)        // Normal shutdown

#define NVME_CC_AMS_RR (0 << 11)
#define NVME_CC_MPS(n) ((uint32_t)(n) << 7)
#define NVME_CC_IOSQES(n) ((uint32_t)(n) << 16)
#define NVME_CC_IOCQES(n) ((uint32_t)(n) << 20)

#define NVME_CSTS_RDY (1 << 0) // Ready
#define NVME_CSTS_CFS (1 << 1) // Controller Fatal Status
#define NVME_MAX_IO_QUEUES (65535)
#define NVME_MAX_QUEUE_SIZE 64
#define NVME_RESET_TIMEOUT_MS 30000
#define NVME_ENABLE_TIMEOUT_MS 30000

struct nvme_queue {
  uint64_t address;
  size_t size;
};

struct nvme_lbaf {
  uint16_t ms;
  uint8_t ds;
  uint8_t rp;
};

struct nvme_command {
  uint32_t cdw0;
  uint32_t nsid;
  uint32_t reserved[2];
  uint64_t metadata;
  uint64_t prp1;
  uint64_t prp2;
  uint32_t cdw10;
  uint32_t cdw11_15[5];
};

struct nvme_completion {
  uint32_t result;
  uint32_t reserved;
  uint16_t sq_head;
  uint16_t sq_id;
  uint16_t command_id;
  uint16_t status;
};

struct nvme_id_ns {
  uint64_t nsze;  // Namespace Size
  uint64_t ncap;  // Namespace Capacity
  uint64_t nuse;  // Namespace Utilization
  uint8_t nsfeat; // Namespace Features
  uint8_t nlbaf;  // Number of LBA Formats
  uint8_t flbas;  // Formatted LBA Size
  uint8_t mc;     // Metadata Capabilities
  uint8_t dpc;    // End-to-end Data Protection Capabilities
  uint8_t dps;    // End-to-end Data Protection Type Settings
  uint8_t nmic;   // Namespace Multi-path I/O and Namespace Sharing Capabilities
  uint8_t rescap; // Reservation Capabilities
  uint8_t fpi;    // Format Progress Indicator
  uint8_t dlfeat; // Deallocate Logical Block Features
  uint16_t nawun; // Namespace Atomic Write Unit Normal
  uint16_t nawupf;    // Namespace Atomic Write Unit Power Fail
  uint16_t nacwu;     // Namespace Atomic Compare & Write Unit
  uint16_t nabsn;     // Namespace Atomic Boundary Size Normal
  uint16_t nabo;      // Namespace Atomic Boundary Offset
  uint16_t nabspf;    // Namespace Atomic Boundary Size Power Fail
  uint16_t noiob;     // Namespace Optimal I/O Boundary
  uint8_t nvmcap[16]; // NVM Capacity
  uint16_t npwg;      // Namespace Preferred Write Granularity
  uint16_t npwa;      // Namespace Preferred Write Alignment
  uint16_t npdg;      // Namespace Preferred Deallocate Granularity
  uint16_t npda;      // Namespace Preferred Deallocate Alignment
  uint16_t nows;      // Namespace Optimal Write Size
  uint16_t mssrl;     // Maximum Single Source Range Length
  uint32_t mcl;       // Maximum Copy Length
  uint8_t msrc;       // Maximum Source Range Count
  uint8_t rsvd81[11];
  uint32_t anagrpid; // ANA Group Identifier
  uint8_t rsvd96[3];
  uint8_t nsattr;            // Namespace Attributes
  uint16_t nvmsetid;         // NVM Set Identifier
  uint16_t endgid;           // Endurance Group Identifier
  uint8_t nguid[16];         // Namespace Globally Unique Identifier
  uint8_t eui64[8];          // IEEE Extended Unique Identifier
  struct nvme_lbaf lbaf[16]; // LBA Format Support
  uint8_t rsvd192[192];
  uint8_t vs[3712]; // Vendor Specific
};

struct nvme_namespace {
  uint32_t nsid;
  uint64_t size;
  uint32_t lba_size;
  uint16_t lba_shift;
  bool valid;
  struct nvme_id_ns *ns_data;
};

struct nvme_pair_queue {
  struct nvme_queue sq;
  struct nvme_queue cq;
  uint64_t sq_tail;
  uint64_t cq_head;
  uint8_t phase;
  uint8_t qid;
  volatile uint32_t *sq_doorbell;
  volatile uint32_t *cq_doorbell;
  Spinlock lock;
};

struct nvme_controller {
  volatile void *bar0;

  uint32_t stride;
  uint32_t page_size;
  uint16_t max_queue_entries;

  struct nvme_queue admin_sq;
  struct nvme_queue admin_cq;
  uint64_t admin_sq_tail;
  uint64_t admin_cq_head;
  uint8_t admin_phase;

  struct nvme_pair_queue *main_io_queue;
  struct nvme_namespace namespaces[256];
  uint32_t num_namespaces;
  uint8_t mpsmin;
  void *identify;
};

struct nvme_arg_disk {
  struct nvme_controller *ctrl;
  uint32_t nsid;
};

static inline uint32_t nvme_read32(struct nvme_controller *ctrl,
                                   uint32_t offset) {
  return *(volatile uint32_t *)((uint8_t *)ctrl->bar0 + offset);
}

static inline void nvme_write32(struct nvme_controller *ctrl, uint32_t offset,
                                uint32_t value) {
  *(volatile uint32_t *)((uint8_t *)ctrl->bar0 + offset) = value;
}

static inline uint64_t nvme_read64(struct nvme_controller *ctrl,
                                   uint32_t offset) {
  return *(volatile uint64_t *)((uint8_t *)ctrl->bar0 + offset);
}

static inline void nvme_write64(struct nvme_controller *ctrl, uint32_t offset,
                                uint64_t value) {
  *(volatile uint64_t *)((uint8_t *)ctrl->bar0 + offset) = value;
}

void nvme_init(pci_t pci, uint8_t bus, uint8_t device, uint8_t function);
