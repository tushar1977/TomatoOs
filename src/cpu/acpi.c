#include "../include/flanterm.h"

#include "../include/kernel.h"
#include "../include/paging.h"
#include "../include/printf.h"
#include "../include/string.h"
#include "uacpi/sleep.h"
#include "uacpi/uacpi.h"
#include <include/acpi.h>
#include <uacpi/event.h>

void acpi_init(void) {
  /*
   * Start with this as the first step of the initialization. This loads all
   * tables, brings the event subsystem online, and enters ACPI mode. We pass
   * in 0 as the flags as we don't want to override any default behavior for
   * now.
   */
  uacpi_status ret = uacpi_initialize(0);
  if (uacpi_unlikely_error(ret)) {
    kprintf("uacpi_initialize error: %s", uacpi_status_to_string(ret));
  }

  /*
   * Load the AML namespace. This feeds DSDT and all SSDTs to the interpreter
   * for execution.
   */
  ret = uacpi_namespace_load();
  if (uacpi_unlikely_error(ret)) {
    kprintf("uacpi_namespace_load error: %s", uacpi_status_to_string(ret));
  }

  /*
   * Initialize the namespace. This calls all necessary _STA/_INI AML methods,
   * as well as _REG for registered operation region handlers.
   */
  ret = uacpi_namespace_initialize();
  if (uacpi_unlikely_error(ret)) {
    kprintf("uacpi_namespace_initialize error: %s",
            uacpi_status_to_string(ret));
  }

  /*
   * Tell uACPI that we have marked all GPEs we wanted for wake (even though we
   * haven't actually marked any, as we have no power management support right
   * now). This is needed to let uACPI enable all unmarked GPEs that have a
   * corresponding AML handler. These handlers are used by the firmware to
   * dynamically execute AML code at runtime to e.g. react to thermal events or
   * device hotplug.
   */
  ret = uacpi_finalize_gpe_initialization();
  if (uacpi_unlikely_error(ret)) {
    kprintf("uACPI GPE initialization error: %s", uacpi_status_to_string(ret));
  }

  /*
   * That's it, uACPI is now fully initialized and working! You can proceed to
   * using any public API at your discretion. The next recommended step is
   * namespace enumeration and device discovery so you can bind drivers to ACPI
   * objects.
   */
  kprintf("Done!!");
}
