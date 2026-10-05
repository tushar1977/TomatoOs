#pragma once
#include <stdint.h>

#define IRQ_TYPE_LEGACY 1
#define IRQ_TYPE_OTHER 0

#define MAX_DYNAMIC_IRQS 8
#define DYNAMIC_IRQ_BASE 48

typedef struct {
  void (*func)(void *arg);
  void *arg;
  uint8_t used;
} irq_t;
void init_irq_subsystem(void);
uint8_t irq_create(uint16_t irq, uint8_t type, void (*func)(void *arg),
                   void *arg, uint64_t flags);
void irq_destroy(uint8_t vector);
