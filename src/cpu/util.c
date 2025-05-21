#include "../include/util.h"
#include "../include/printf.h"

void outPortB(uint16_t port, uint8_t value) {

  asm volatile("outb %1 ,%0" : : "dN"(port), "a"(value));
}

char inPortB(uint16_t port) {

  char rv;
  asm volatile("inb %1, %0" : "=a"(rv) : "dN"(port));
  return rv;
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
  // Remap PIC
  outPortB(0x20, 0x11); // ICW1: Initialize PIC1
  outPortB(0xA0, 0x11); // ICW1: Initialize PIC2

  outPortB(0x21, 0x20); // ICW2: Map PIC1 to vectors 0x20-0x27
  outPortB(0xA1, 0x28); // ICW2: Map PIC2 to vectors 0x28-0x2F

  outPortB(0x21, 0x02); // ICW3: Tell PIC1 that PIC2 is at IRQ2
  outPortB(0xA1, 0x04); // ICW3: Tell PIC2 its cascade identity

  outPortB(0x21, 0x01); // ICW4: Set 8086 mode for PIC1
  outPortB(0xA1, 0x01); // ICW4: Set 8086 mode for PIC2

  outPortB(0x21, 0xFF);
  outPortB(0xA1, 0xFF);
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
