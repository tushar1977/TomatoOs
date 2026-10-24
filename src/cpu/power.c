#include "../include/power.h"
#include "../include/klog.h"
#include "../include/printf.h"
#include "../include/util.h"
#include "uacpi/sleep.h"

static bool running_on_qemu(void) {
  outPortD(0xCF8, 0x80000000u); /* bus 0, device 0, function 0, offset 0 */
  uint32_t id = inPortD(0xCFC); /* low 16 = vendor, high 16 = device */
  return id == 0x12378086u || id == 0x29C08086u;
}

// In newer and Old versions of QEMU, you can do shutdown with:
static void qemu_poweroff(void) {
  outPortW(0x604, 0x2000);
  outPortW(0xB004, 0x2000);
}

void shutdown(void) {
  klog(KLOG_INFO, "POWER");
  kprintf("shutting down...\n");

  if (running_on_qemu()) {
    qemu_poweroff();
    klog(KLOG_WARN, "POWER");
    kprintf("QEMU poweroff write ignored, trying ACPI\n");
  }

  uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
  if (uacpi_unlikely_error(ret)) {
    klog(KLOG_ERROR, "POWER");
    kprintf("prepare_for_sleep_state: %s\n", uacpi_status_to_string(ret));
    return;
  }

  ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
  if (uacpi_unlikely_error(ret)) {
    klog(KLOG_ERROR, "POWER");
    kprintf("enter_sleep_state: %s\n", uacpi_status_to_string(ret));
    return;
  }
}

void reboot(void) {
  klog(KLOG_INFO, "POWER");
  kprintf("rebooting...\n");

  uacpi_status ret = uacpi_reboot();
  if (uacpi_unlikely_error(ret)) {

    klog(KLOG_ERROR, "POWER");
    outPortB(0x64, 0xFE);

    for (volatile int i = 0; i < 1000000; i++)
      ;

    struct {
      uint16_t limit;
      uint64_t base;
    } __attribute__((packed)) null_idt = {0, 0};
    asm volatile("lidt %0" ::"m"(null_idt));
    asm volatile("int $0x03");
  }

  halt();
}
