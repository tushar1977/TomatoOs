#include "../include/klog.h"
#include "../include/kernel.h"
#include "../include/printf.h"
#include <stdint.h>

#define MS_PER_TICK 10

static void fmt_uint_padded(char *out, uint64_t value, int width, char pad) {
  char tmp[20];
  int n = 0;

  do {
    tmp[n++] = '0' + (value % 10);
    value /= 10;
  } while (value > 0);

  int i = 0;
  for (int p = n; p < width; p++)
    out[i++] = pad;
  while (n > 0)
    out[i++] = tmp[--n];
  out[i] = '\0';
}

static const char *level_label(klog_level_t level) {
  switch (level) {
  case KLOG_WARN:
    return "\033[1;33mWARN \033[0m";
  case KLOG_ERROR:
    return "\033[1;31mERROR\033[0m";
  case KLOG_DEBUG:
    return "\033[1;35mDEBUG\033[0m";
  case KLOG_INFO:
  default:
    return "\033[1;36mINFO \033[0m";
  }
}

void klog(klog_level_t level, const char *subsystem) {
  uint64_t total_ms = kernel.apic_ticks * MS_PER_TICK;
  char secbuf[20];
  char msbuf[8];

  fmt_uint_padded(secbuf, total_ms / 1000, 5, ' ');
  fmt_uint_padded(msbuf, total_ms % 1000, 3, '0');

  kprintf("\033[1;34m[%s.%s]\033[0m %s \033[1m%s\033[0m: ", secbuf, msbuf,
          level_label(level), subsystem);
}
