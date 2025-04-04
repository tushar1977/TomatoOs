#include "../include/printf.h"
#include "../include/flanterm.h"
#include "../include/kernel.h"
#include "../include/string.h"
#include "limits.h"
#include <stdarg.h>
Spinlock lock;
int kprintf(const char *restrict format, ...) {
  va_list parameters;
  va_start(parameters, format);
  char buf[32];
  int written = 0;

  while (*format != '\0') {
    if (format[0] == '\033' && format[1] == '[') {
      const char *seq_start = format;
      format += 2;

      size_t seq_len = 0;
      while (format[seq_len] && format[seq_len] != 'm')
        seq_len++;
      if (format[seq_len] == 'm') {
        size_t total_len = seq_len + 3;
        flanterm_write(kernel.ft_ctx, seq_start, total_len);
        format += seq_len + 1;
        written += total_len;
      } else {
        flanterm_write(kernel.ft_ctx, seq_start, 2);
        format += 2;
        written += 2;
      }
      continue;
    }
    size_t maxrem = INT_MAX - written;

    if (format[0] != '%' || format[1] == '%') {
      if (format[0] == '%')
        format++;
      size_t amount = 1;
      while (format[amount] && format[amount] != '%')
        amount++;
      if (maxrem < amount) {
        return -1;
      }

      flanterm_write(kernel.ft_ctx, format, amount);
      format += amount;
      written += amount;
      continue;
    }

    const char *format_begun_at = format++;

    switch (*format) {
    case 'c': {
      format++;
      char c = (char)va_arg(parameters, int);
      flanterm_write(kernel.ft_ctx, &c, 1);
      written += 1;
      break;
    }

    case 's': {
      format++;
      const char *str = va_arg(parameters, const char *);
      size_t len = strlen(str);
      flanterm_write(kernel.ft_ctx, str, len);
      written += len;
      break;
    }

    case 'd': {
      format++;
      int num = va_arg(parameters, int);
      int i = 0;
      if (num < 0) {
        buf[i++] = '-';
        num = -num;
      } else if (num == 0) {
        buf[i++] = '0';
      }

      int digits = 0;
      int tmp = num;
      while (tmp > 0) {
        buf[i++] = '0' + (tmp % 10);
        tmp /= 10;
        digits++;
      }

      int start = (buf[0] == '-') ? 1 : 0;
      int end = i - 1;
      while (start < end) {
        char c = buf[start];
        buf[start] = buf[end];
        buf[end] = c;
        start++;
        end--;
      }

      flanterm_write(kernel.ft_ctx, buf, i);
      written += i;
      break;
    }

    case 'x': {
      format++;
      unsigned int num = va_arg(parameters, unsigned int);
      buf[0] = '0';
      buf[1] = 'x';

      if (num == 0) {
        buf[2] = '0';
        flanterm_write(kernel.ft_ctx, buf, 3);
        written += 3;
      } else {
        int i = 2;
        bool started = false;

        for (int shift = 28; shift >= 0; shift -= 4) {
          int nibble = (num >> shift) & 0xF;
          if (nibble != 0 || started) {
            buf[i++] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
            started = true;
          }
        }

        flanterm_write(kernel.ft_ctx, buf, i);
        written += i;
      }
      break;
    }

    default: {
      flanterm_write(kernel.ft_ctx, format_begun_at, 1);
      format++;
      written++;
      break;
    }
    }
  }

  va_end(parameters);
  return written;
}

void k_ok() { kprintf(" \033[1;32mOK!\033[0m\n"); }

void k_fail() { kprintf(" \033[1;31mFAIL!\033[0m\n"); }

void k_debug(const char *msg) {
  spinlock_aquire(&lock);
  kprintf("\033[1;35mDEBUG!\033[0m %s", msg);
  k_ok();
  spinlock_release(&lock);
}
