
#ifndef IDT_H
#define IDT_H

#include "stdint.h"
#include "util.h"

struct InterruptDescriptor64 {
  uint16_t address_low;
  uint16_t selector;
  uint8_t ist;
  uint8_t flags;
  uint16_t address_mid;
  uint32_t address_high;
  uint32_t reserved;
} __attribute__((packed));

void exception_handler(struct IDTEFrame registers);
struct Idt_ptr {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed));

void InitIdt();
void setIdtGate(struct InterruptDescriptor64 *idt_entries, uint8_t num,
                void *base, uint16_t sel, uint8_t flags);

extern void divideException();
extern void debugException();
extern void breakpointException();
extern void overflowException();
extern void boundRangeExceededException();
extern void invalidOpcodeException();
extern void deviceNotAvaliableException();
extern void doubleFaultException();
extern void coprocessorSegmentOverrunException();
extern void invalidTSSException();
extern void segmentNotPresentException();
extern void stackSegmentFaultException();
extern void generalProtectionFaultException();
extern void pageFaultException();
extern void floatingPointException();
extern void alignmentCheckException();
extern void machineCheckException();
extern void simdFloatingPointException();
extern void virtualisationException();

extern void irq1();

#endif
