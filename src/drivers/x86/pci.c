#include "include/pci.h"
#include "kernel.h"
#include "klog.h"
#include "printf.h"
#include "stdbool.h"
#include "stdint.h"

void pci_drivers_print(void) {
  kprintf("PCI Driver Registry:\n");

  for (int i = 0; i < 256; i++) {
    pci_driver_t *drv = &kernel.pci_drivers[i];

    if (!drv->used)
      continue;

    kprintf("  [idx=%d] class=%x subclass=%x pcidrv=%x\n", i, drv->_class,
            drv->subclass, (uintptr_t)drv->pcidrv);
  }
}

pci_t __pci_load(uint8_t bus, uint8_t num, uint8_t function) {

  pci_t pciData;
  uint16_t *p = (uint16_t *)&pciData;

  for (int i = 0; i < 32; i++) {
    p[i] = pci_read_config16(bus, num, function, i * 2);
  }

  return pciData;
}

void reg(void (*pcidrv)(pci_t, uint8_t, uint8_t, uint8_t), uint8_t _class,
         uint8_t subclass) {
  for (uint16_t i = 0; i < 256; i++) {
    if (!kernel.pci_drivers[i].used) {
      kernel.pci_drivers[i].used = true;
      kernel.pci_drivers[i]._class = _class;
      kernel.pci_drivers[i].subclass = subclass;
      kernel.pci_drivers[i].pcidrv = pcidrv;
      return;
    }
  }
}

void __pci_launch(pci_t pci, uint8_t bus, uint8_t device, uint8_t function) {
  if (kernel.pci_device_count < MAX_PCI_DEVICES) {
    pci_device_entry_t *entry = &kernel.pci_devices[kernel.pci_device_count++];
    entry->used = 1;
    entry->header = pci;
    entry->bus = bus;
    entry->device = device;
    entry->function = function;
    entry->driver_data = NULL;
  }

  for (uint16_t i = 0; i < MAX_PCI_DRIVERS; i++) {
    if (kernel.pci_drivers[i].used &&
        kernel.pci_drivers[i]._class == pci._class &&
        kernel.pci_drivers[i].subclass == pci.subclass) {
      kernel.pci_drivers[i].pcidrv(pci, bus, device, function);
    }
  }
}

void initworkspace() {
  pci_t c_pci;
  for (uint16_t bus = 0; bus < 256; bus++) {

    for (uint8_t device = 0; device < 32; device++) {
      c_pci = __pci_load(bus, device, 0);
      if (c_pci.vendorID != 0xFFFF) {
        klog(KLOG_INFO, "PCI");
        kprintf("%d:%d.%d vendor=%x device=%x class=%x sub=%x\n", bus, device,
                0, c_pci.vendorID, c_pci.deviceID, c_pci._class,
                c_pci.subclass);
        __pci_launch(c_pci, bus, device, 0);
        for (uint8_t function = 1; function < 8; function++) {
          pci_t pci = __pci_load(bus, device, function);
          if (pci.vendorID != 0xFFFF) {
            klog(KLOG_INFO, "PCI");
            kprintf("%d:%d.%d vendor=%x device=%x class=%x sub=%x\n", bus,
                    device, function, pci.vendorID, pci.deviceID, pci._class,
                    pci.subclass);
            __pci_launch(pci, bus, device, function);
          }
        }
      }
    }
  }
}
