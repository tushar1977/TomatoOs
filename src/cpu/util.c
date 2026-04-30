#include "../include/util.h"
#include "../include/printf.h"
#include <stdint.h>

uint16_t pciConfigReadWord(uint8_t bus, uint8_t slot, uint8_t func,
                           uint8_t offset) {
  uint32_t address;
  uint32_t lbus = (uint32_t)bus;
  uint32_t lslot = (uint32_t)slot;
  uint32_t lfunc = (uint32_t)func;
  uint16_t tmp = 0;

  address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                       (offset & 0xFC) | ((uint32_t)0x80000000));

  outPortB(0xCF8, address);
  tmp = (uint16_t)((inPortB(0xCFC) >> ((offset & 2) * 8)) & 0xFFFF);
  return tmp;
}

void ConfigWriteDword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                      uint32_t conf) {
  uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) |
                                (offset & 0xfc) | ((uint32_t)0x80000000));
  outPortB(PCI_CONFIG_ADDRESS, address);
  outPortB(PCI_CONFIG_DATA, conf);
}

void dump_Registers(struct IDTEFrame registers) {

  kprintf("Registers:\n"
          "RAX: %x RBX: %x RCX: %x RDX: %x\n"
          "RSI: %x RDI: %x RBP: %x RSP: %x\n"
          "R8:  %x R9:  %x R10: %x R11: %x\n"
          "R12: %x R13: %x R14: %x R15: %x\n"
          "RIP: %x CS: %x\n",
          registers.rax, registers.rbx, registers.rcx, registers.rdx,
          registers.rsi, registers.rdi, registers.rbp, registers.rsp,
          registers.r8, registers.r9, registers.r10, registers.r11,
          registers.r12, registers.r13, registers.r14, registers.r15,
          registers.rip, registers.cs);
}

void disableLegacyPIC() {
  outPortB(PIC1_DATA, 0xff);
  outPortB(PIC2_DATA, 0xff);
}
void disable_interrupts() { asm("cli"); }

void enable_interrupts() { asm("sti"); }

void wait_for_interrupt() { asm("hlt"); }

void halt() {
  for (;;)
    wait_for_interrupt();
}

__attribute__((interrupt)) void nmi_handler(void *) {
  kprintf("NMI received\n");
  halt();
}

void cpuid(int code, uint32_t *a, uint32_t *d) {
  asm volatile("cpuid" : "=a"(*a), "=d"(*d) : "0"(code) : "ebx", "ecx");
}
