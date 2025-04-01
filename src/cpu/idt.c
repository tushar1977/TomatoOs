#include "../include/idt.h"
#include "../include/fb.h"
#include "../include/kernel.h"
#include "../include/paging.h"
#include "../include/pmm.h"
#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/tty.h"
#include "../include/util.h"
#include "stdint.h"

void InitIdt() {
  uint64_t virt =
      (uint64_t)k_malloc(256 * sizeof(struct InterruptDescriptor64));
  struct InterruptDescriptor64 *IDT =
      (struct InterruptDescriptor64
           *)((uint64_t)k_malloc(256 * sizeof(struct InterruptDescriptor64)) +
              ((uint64_t)kernel.hhdm));

  outPortB(0x20, 0x11);
  outPortB(0xA0, 0x11);

  outPortB(0x21, 0x20);
  outPortB(0xA1, 0x28);

  outPortB(0x21, 0x04);
  outPortB(0xA1, 0x02);

  outPortB(0x21, 0x01);
  outPortB(0xA1, 0x01);

  outPortB(0x21, 0x0);
  outPortB(0xA1, 0x0);

  setIdtGate(IDT, 0, &divideException, 0x08, 0x8E);
  setIdtGate(IDT, 1, &debugException, 0x08, 0x8E);
  setIdtGate(IDT, 3, &breakpointException, 0x08, 0x8E);
  setIdtGate(IDT, 4, &overflowException, 0x08, 0x8E);
  setIdtGate(IDT, 5, &boundRangeExceededException, 0x08, 0x8E);
  setIdtGate(IDT, 6, &invalidOpcodeException, 0x08, 0x8E);
  setIdtGate(IDT, 7, &deviceNotAvaliableException, 0x08, 0x8E);
  setIdtGate(IDT, 8, &doubleFaultException, 0x08, 0x8E);
  setIdtGate(IDT, 9, &coprocessorSegmentOverrunException, 0x08, 0x8E);
  setIdtGate(IDT, 10, &invalidTSSException, 0x08, 0x8E);
  setIdtGate(IDT, 11, &segmentNotPresentException, 0x08, 0x8E);
  setIdtGate(IDT, 12, &stackSegmentFaultException, 0x08, 0x8E);
  setIdtGate(IDT, 13, &generalProtectionFaultException, 0x08, 0x8E);
  setIdtGate(IDT, 14, &pageFaultException, 0x08, 0x8E);
  setIdtGate(IDT, 16, &floatingPointException, 0x08, 0x8E);
  setIdtGate(IDT, 17, &alignmentCheckException, 0x08, 0x8E);
  setIdtGate(IDT, 18, &machineCheckException, 0x08, 0x8E);
  setIdtGate(IDT, 19, &simdFloatingPointException, 0x08, 0x8E);
  setIdtGate(IDT, 20, &virtualisationException, 0x08, 0x8E);

  kernel.idtr.base = (uint64_t)IDT;
  kernel.idtr.limit = (sizeof(struct InterruptDescriptor64) * 256) - 1;

  asm volatile("lidt %0" ::"m"(kernel.idtr));
}
void setIdtGate(struct InterruptDescriptor64 *idt_entries, uint8_t num,
                void *base, uint16_t sel, uint8_t flags) {

  idt_entries[num].address_low = (uint64_t)base & 0xFFFF;
  idt_entries[num].address_mid = ((uint64_t)base >> 16) & 0xFFFF;
  idt_entries[num].address_high = ((uint64_t)base >> 32);
  idt_entries[num].selector = sel;
  idt_entries[num].flags = flags;
  idt_entries[num].ist = 0;
}

void exception_handler(struct IDTEFrame registers) {
  asm("cli");

  const char *label = "Unknown Exception";

  switch (registers.type) {
  case 0:
    label = "Divide by Zero Exception";
    break;
  case 1:
    label = "Debug Exception";
    break;
  case 3:
    label = "Breakpoint Exception";
    break;
  case 4:
    label = "Overflow Exception";
    break;
  case 5:
    label = "Bound Range Exceeded";
    break;
  case 6:
    label = "Invalid Opcode Exception";
    break;
  case 7:
    label = "Device Not Available";
    break;
  case 8:
    label = "Double Fault";
    break;
  case 9:
    label = "Coprocessor Segment Overrun";
    break;
  case 10:
    label = "Invalid TSS";
    break;
  case 11:
    label = "Segment Not Present";
    break;
  case 12:
    label = "Stack-Segment Fault";
    break;
  case 13:
    label = "General Protection Fault";
    break;
  case 14:
    label = "Page Fault";
    break;
  case 16:
    label = "Floating-Point Exception";
    break;
  case 17:
    label = "Alignment Check";
    break;
  case 18:
    label = "Machine Check";
    break;
  case 19:
    label = "SIMD Floating-Point Exception";
    break;
  case 20:
    label = "Virtualization Exception";
    break;
  }

  flanterm_write(kernel.ft_ctx, label, strlen(label));

  asm("hlt");
}
