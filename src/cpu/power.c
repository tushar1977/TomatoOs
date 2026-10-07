#include "../include/power.h"
#include "../include/klog.h"
#include "../include/printf.h"
#include "../include/util.h"
#include "uacpi/sleep.h"

void shutdown(void) {
  klog(KLOG_INFO, "POWER");
  kprintf("shutting down...\n");

  uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
  if (uacpi_unlikely_error(ret)) {
    klog(KLOG_ERROR, "POWER");
    kprintf("prepare_for_sleep_state: %s\n", uacpi_status_to_string(ret));
    halt();
  }

  ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);

  klog(KLOG_ERROR, "POWER");
  kprintf("enter_sleep_state: %s\n", uacpi_status_to_string(ret));
  halt();
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
