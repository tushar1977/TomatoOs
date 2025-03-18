#include "../include/util.h"

void outPortB(uint16_t port, uint8_t value) {

  asm volatile("outb %1 ,%0" : : "dN"(port), "a"(value));
}

char inPortB(uint16_t port) {

  char rv;
  asm volatile("inb %1, %0" : "=a"(rv) : "dN"(port));
  return rv;
}
