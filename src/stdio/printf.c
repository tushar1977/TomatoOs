#include "../include/printf.h"
#include "../include/flanterm.h"
#include "../include/kernel.h"
#include "../include/string.h"
#include <stdarg.h>

void kprintf(const char *fmt, ...) {

  va_list args;
  va_start(args, fmt);

  char buf[32];

  while (*fmt) {
    if (strcmp(fmt, "%") == 0) {

      if (strcmp(fmt, "\\") == 0) {
        fmt++;

        switch (*fmt) {
        case 'n':
          flanterm_write(kernel.ft_ctx, "\n", 1);
          break;
        case 't':
          flanterm_write(kernel.ft_ctx, "\t", 1);
          break;
        case 'r':
          flanterm_write(kernel.ft_ctx, "\r", 1);
          break;
        case 'b':
          flanterm_write(kernel.ft_ctx, "\b", 1);
          break;
        case 'f':
          flanterm_write(kernel.ft_ctx, "\f", 1);
          break;
        case '\\':
          flanterm_write(kernel.ft_ctx, "\\", 1);
          break;
        case '\'':
          flanterm_write(kernel.ft_ctx, "'", 1);
          break;
        case '"':
          flanterm_write(kernel.ft_ctx, "\"", 1);
          break;
        case '0':
          flanterm_write(kernel.ft_ctx, "\0", 1);
          break;
        default:
          flanterm_write(kernel.ft_ctx, "\\", 1);
          flanterm_write(kernel.ft_ctx, fmt, 1);
          break;
        }

        fmt++;
        continue;
      }

      flanterm_write(kernel.ft_ctx, fmt, 1);
      fmt++;
      continue;
    }

    fmt++;
    switch (*fmt) {
    case 'd': {
      int num = va_arg(args, int);
      int i = 0;
      if (num < 0) {
        buf[i++] = '-';
        num = -num;
      }
      int digits = 0;
      int tmp = num;
      do {
        buf[i++] = '0' + (tmp % 10);
        tmp /= 10;
        digits++;
      } while (tmp > 0);
      // Reverse the digits
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
      break;
    }

    case 'x': { // Hexadecimal
      unsigned int num = va_arg(args, unsigned int);
      buf[0] = '0';
      buf[1] = 'x';
      if (num == 0) {
        buf[2] = '0';
        flanterm_write(kernel.ft_ctx, buf, 3);
      } else {
        int i = 2;
        for (int shift = 28; shift >= 0; shift -= 4) {
          int nibble = (num >> shift) & 0xF;
          if (nibble != 0 || i > 2) {
            buf[i++] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
          }
        }
        flanterm_write(kernel.ft_ctx, buf, i);
      }
      break;
    }

    case 'c': { // Character
      char c = (char)va_arg(args, int);
      flanterm_write(kernel.ft_ctx, &c, 1);
      break;
    }

    case 's': { // String
      const char *str = va_arg(args, const char *);
      size_t len = 0;
      while (str[len])
        len++;
      flanterm_write(kernel.ft_ctx, str, len);
      break;
    }

    case 'p': { // Pointer
      void *ptr = va_arg(args, void *);
      uintptr_t num = (uintptr_t)ptr;
      buf[0] = '0';
      buf[1] = 'x';
      int i = 2;
      for (int shift = (sizeof(uintptr_t) * 8) - 4; shift >= 0; shift -= 4) {
        int nibble = (num >> shift) & 0xF;
        if (nibble != 0 || i > 2) {
          buf[i++] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        }
      }
      flanterm_write(kernel.ft_ctx, buf, i);
      break;
    }

    default:
      flanterm_write(kernel.ft_ctx, fmt, 1);
      break;
    }

    fmt++;
  }

  va_end(args);
}
