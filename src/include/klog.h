#pragma once

typedef enum {
  KLOG_INFO,
  KLOG_WARN,
  KLOG_ERROR,
  KLOG_DEBUG,
} klog_level_t;

/* Prints a dmesg-style prefix: "[sec.ms] LEVEL subsystem: " with no
 * trailing newline. Timestamp comes from kernel.apic_ticks (apic_timer
 * fires every ~10ms, see apic_timer.c). Follow it with a normal kprintf()
 * call (and a trailing "\n") to write the rest of the log line. */
void klog(klog_level_t level, const char *subsystem);
