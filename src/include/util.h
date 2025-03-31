#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

#define CEIL_DIV(a, b) (((a + b) - 1) / b)

void outPortB(uint16_t port, uint8_t value);

char inPortB(uint16_t port);
struct InterruptRegisters {
  uint64_t r11, r10, r9, r8;
  uint64_t rsi, rdi, rdx, rcx, rax;
  uint64_t int_no, err_code;
  uint64_t rsp, rflags, cs, rip;
};

#endif
