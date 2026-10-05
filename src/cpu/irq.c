#include "irq.h"
#include "apic.h"
#include "idt.h"
#include "kernel.h"
#include "util.h"
#include <stdint.h>

void setIdtGate(struct InterruptDescriptor64 *idt_entries, uint8_t num,
                void *base, uint16_t sel, uint8_t flags);

static irq_t irq_table[MAX_DYNAMIC_IRQS];

#define IRQ_TRAMPOLINE(n)                                                      \
  __attribute__((interrupt)) void irq_trampoline_##n(void *frame) {            \
    (void)frame;                                                               \
    if (irq_table[n].used && irq_table[n].func) {                              \
      irq_table[n].func(irq_table[n].arg);                                     \
    }                                                                          \
    end_of_interrupt();                                                        \
  }

IRQ_TRAMPOLINE(0)
IRQ_TRAMPOLINE(1)
IRQ_TRAMPOLINE(2)
IRQ_TRAMPOLINE(3)
IRQ_TRAMPOLINE(4)
IRQ_TRAMPOLINE(5)
IRQ_TRAMPOLINE(6)
IRQ_TRAMPOLINE(7)

static void (*irq_trampolines[MAX_DYNAMIC_IRQS])(void *) = {
    irq_trampoline_0, irq_trampoline_1, irq_trampoline_2, irq_trampoline_3,
    irq_trampoline_4, irq_trampoline_5, irq_trampoline_6, irq_trampoline_7};

void init_irq_subsystem(void) { memset(irq_table, 0, sizeof(irq_table)); }

uint8_t irq_create(uint16_t irq, uint8_t type, void (*func)(void *arg),
                   void *arg, uint64_t flags) {
  int slot = -1;
  for (int i = 0; i < MAX_DYNAMIC_IRQS; i++) {
    if (!irq_table[i].used) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    return 0;
  }

  irq_table[slot].used = 1;
  irq_table[slot].func = func;
  irq_table[slot].arg = arg;

  uint8_t vector = DYNAMIC_IRQ_BASE + slot;

  setIdtGate(IDT, vector, irq_trampolines[slot], 0x08, 0x8E);

  if (type == IRQ_TYPE_LEGACY) {
    uint32_t gsi = irq;
    if (irq < 16) {
      gsi = kernel.irq_overrides[irq];
    }
    set_ioapic_entry(vector, gsi, (uint8_t)flags, 0);
    unmask_ioapic(gsi, 0);
  }

  return vector;
}

void irq_destroy(uint8_t vector) {
  int slot = vector - DYNAMIC_IRQ_BASE;
  if (slot < 0 || slot >= MAX_DYNAMIC_IRQS) {
    return;
  }
  irq_table[slot].used = 0;
  irq_table[slot].func = NULL;
  irq_table[slot].arg = NULL;
}
