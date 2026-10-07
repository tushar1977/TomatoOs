#include "../include/flanterm.h"

#include "../include/kernel.h"
#include "../include/klog.h"
#include "../include/paging.h"
#include "../include/printf.h"
#include "../include/string.h"
#include "uacpi/sleep.h"
#include "uacpi/uacpi.h"
#include <include/acpi.h>
#include <uacpi/event.h>

void acpi_init(void) {
  uacpi_status ret;

  ret = uacpi_initialize(0);
  if (uacpi_unlikely_error(ret)) {
    klog(KLOG_ERROR, "ACPI");
    kprintf("uacpi_initialize: %s\n", uacpi_status_to_string(ret));
    return;
  }

  ret = uacpi_namespace_load();
  if (uacpi_unlikely_error(ret)) {
    klog(KLOG_ERROR, "ACPI");
    kprintf("uacpi_namespace_load: %s\n", uacpi_status_to_string(ret));
    return;
  }

  ret = uacpi_namespace_initialize();
  if (uacpi_unlikely_error(ret)) {
    klog(KLOG_ERROR, "ACPI");
    kprintf("uacpi_namespace_initialize: %s\n", uacpi_status_to_string(ret));
    return;
  }

  ret = uacpi_finalize_gpe_initialization();
  if (uacpi_unlikely_error(ret)) {
    klog(KLOG_ERROR, "ACPI");
    kprintf("GPE initialization: %s\n", uacpi_status_to_string(ret));
    return;
  }

  klog(KLOG_INFO, "ACPI");
  kprintf("uACPI initialized successfully\n");
}
