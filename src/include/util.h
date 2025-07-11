#pragma once
#include "string.h"
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define CEIL_DIV(a, b) (((a + b) - 1) / b)

void outPortB(uint16_t port, uint8_t value);

char inPortB(uint16_t port);

struct IDTEFrame {
  uint64_t cr2;
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rbp;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t rbx;
  uint64_t rax;
  uint64_t type;
  uint64_t code;
  uint64_t rip;
  uint64_t cs;
  uint64_t flags;
  uint64_t rsp;
  uint64_t ss;
} __attribute__((aligned(8)));
;

void disable_interrupts();
void enable_interrupts();

void disableLegacyPIC();
void wait_for_interrupt();
void halt();
void cpuid(int code, uint32_t *a, uint32_t *d);

uint16_t pciConfigReadWord(uint8_t bus, uint8_t slot, uint8_t func,
                           uint8_t offset);

void ConfigWriteDword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                      uint32_t conf);
