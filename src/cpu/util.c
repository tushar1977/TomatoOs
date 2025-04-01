#include "../include/util.h"

void outPortB(uint16_t port, uint8_t value) {

  asm volatile("outb %1 ,%0" : : "dN"(port), "a"(value));
}

char inPortB(uint16_t port) {

  char rv;
  asm volatile("inb %1, %0" : "=a"(rv) : "dN"(port));
  return rv;
}

void disable_interrupts() { asm("cli"); }

void enable_interrupts() { asm("sti"); }

void wait_for_interrupt() { asm("hlt"); }

void halt() {
  disable_interrupts();
  for (;;)
    wait_for_interrupt();
}

void cpuid(int code, uint32_t *a, uint32_t *d) {
  asm volatile("cpuid" : "=a"(*a), "=d"(*d) : "0"(code) : "ebx", "ecx");
}
