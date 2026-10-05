#include "include/pci.h"
#include "printf.h"
#include "stdbool.h"
#include "stdint.h"

pci_driver_t pci_drivers[256];
int last_pci_drv = 0;

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
    if (!pci_drivers[i].used) {
      pci_drivers[i].used = true;
      pci_drivers[i]._class = _class;
      pci_drivers[i].subclass = subclass;
      pci_drivers[i].pcidrv = pcidrv;
      return;
    }
  }
}

void __pci_launch(pci_t pci, uint8_t bus, uint8_t device, uint8_t function) {
  for (uint16_t i = 0; i < 256; i++) {
    if (pci_drivers[i].used && pci_drivers[i]._class == pci._class &&
        pci_drivers[i].subclass == pci.subclass) {
      pci_drivers[i].pcidrv(pci, bus, device, function);
    }
  }
}

void initworkspace() {
  pci_t c_pci;
  for (uint16_t bus = 0; bus < 256; bus++) {

    c_pci = __pci_load(bus, 0, 0);
    if (c_pci.vendorID != 0xFFFF) {
      for (uint8_t device = 0; device < 32; device++) {
        c_pci = __pci_load(bus, device, 0);
        if (c_pci.vendorID != 0xFFFF) {
          kprintf(
              "[pci] found %d:%d.%d  vendor=%x device=%x  class=%x sub=%x\n",
              bus, device, 0, c_pci.vendorID, c_pci.deviceID, c_pci._class,
              c_pci.subclass);
          __pci_launch(c_pci, bus, device, 0);
          for (uint8_t function = 1; function < 8; function++) {
            pci_t pci = __pci_load(bus, device, function);
            if (pci.vendorID != 0xFFFF) {
              kprintf("[pci] found %d:%d.%d  vendor=%x device=%x  class=%x "
                      "sub=%x\n",
                      bus, device, function, pci.vendorID, pci.deviceID,
                      pci._class, pci.subclass);
              __pci_launch(pci, bus, device, function);
            }
          }
        }
      }
    }
  }
}
