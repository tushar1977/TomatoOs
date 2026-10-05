#pragma once
#include "util.h"
#include <stdint.h>

typedef struct {
  uint16_t vendorID;
  uint16_t deviceID;
  uint16_t command;
  uint16_t status;
  uint8_t revisionID;
  uint8_t progIF;
  uint8_t subclass;
  uint8_t _class;
  uint8_t cacheLineSize;
  uint8_t latencyTimer;
  uint8_t headerType;
  uint8_t bist;
  uint32_t bar0;
  uint32_t bar1;
  uint32_t bar2;
  uint32_t bar3;
  uint32_t bar4;
  uint32_t bar5;
  uint32_t cardbusCISPointer;
  uint16_t subsystemVendorID;
  uint16_t subsystemID;
  uint32_t expansionROMBaseAddress;
  uint8_t capabilitiesPointer;
  uint8_t reserved0;
  uint16_t reserved1;
  uint32_t reserved2;
  uint8_t irq;
  uint8_t interruptPIN;
  uint8_t minGrant;
  uint8_t maxLatency;
} __attribute__((packed)) pci_t;

typedef struct pci_cap {
  uint8_t id;
  uint8_t off;
  uint8_t bus;
  uint8_t num;
  uint8_t func;
  uint16_t venID;
  uint16_t devID;
  uint8_t data[32];
  struct pci_cap *next;
} __attribute__((packed)) pci_cap_t;

typedef struct {
  int used;
  uint8_t _class;
  uint8_t subclass;
  void (*pcidrv)(pci_t, uint8_t, uint8_t, uint8_t);
} __attribute__((packed)) pci_driver_t;

void reg(void (*pcidrv)(pci_t, uint8_t, uint8_t, uint8_t), uint8_t _class,
         uint8_t subclass);
void initworkspace();

inline uint32_t pci_read_config32(uint8_t bus, uint8_t num, uint8_t function,
                                  uint8_t offset) {
  uint32_t address = (1u << 31) | (bus << 16) | (num << 11) | (function << 8) |
                     (offset & 0xfc);
  outPortD(PCI_CONFIG_ADDRESS, address);
  return inPortD(PCI_CONFIG_DATA);
}

inline uint16_t pci_read_config16(uint8_t bus, uint8_t num, uint8_t function,
                                  uint8_t offset) {
  uint32_t address = (1u << 31) | (bus << 16) | (num << 11) | (function << 8) |
                     (offset & 0xfc);
  outPortD(PCI_CONFIG_ADDRESS, address);
  return (uint16_t)((inPortD(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xffff);
}

inline uint8_t pci_read_config8(uint8_t bus, uint8_t num, uint8_t function,
                                uint8_t offset) {
  uint32_t address = (1u << 31) | (bus << 16) | (num << 11) | (function << 8) |
                     (offset & 0xfc);
  outPortD(PCI_CONFIG_ADDRESS, address);
  return (uint8_t)((inPortD(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xff);
}

inline void pci_write_config32(uint8_t bus, uint8_t num, uint8_t function,
                               uint8_t offset, uint32_t value) {
  uint32_t address = (1u << 31) | (bus << 16) | (num << 11) | (function << 8) |
                     (offset & 0xfc);
  outPortD(PCI_CONFIG_ADDRESS, address);
  outPortD(PCI_CONFIG_DATA, value);
}

inline void pci_write_config16(uint8_t bus, uint8_t num, uint8_t function,
                               uint8_t offset, uint16_t value) {
  uint32_t address = (1u << 31) | (bus << 16) | (num << 11) | (function << 8) |
                     (offset & 0xfc);
  outPortD(PCI_CONFIG_ADDRESS, address);
  uint32_t current = inPortD(PCI_CONFIG_DATA);
  uint32_t shift = (offset & 2) * 8;
  uint32_t merged =
      (current & ~(0xffffu << shift)) | ((uint32_t)value << shift);
  outPortD(PCI_CONFIG_DATA, merged);
}

inline void pci_write_config8(uint8_t bus, uint8_t num, uint8_t function,
                              uint8_t offset, uint8_t value) {
  uint32_t address = (1u << 31) | (bus << 16) | (num << 11) | (function << 8) |
                     (offset & 0xfc);
  outPortD(PCI_CONFIG_ADDRESS, address);
  uint32_t current = inPortD(PCI_CONFIG_DATA);
  uint32_t shift = (offset & 3) * 8;
  uint32_t merged = (current & ~(0xffu << shift)) | ((uint32_t)value << shift);
  outPortD(PCI_CONFIG_DATA, merged);
}
