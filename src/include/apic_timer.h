#pragma once

#define APIC_REGISTER_TIMER_DIV 0x3E0
#define APIC_REGISTER_TIMER_INITCNT 0x380
#define APIC_REGISTER_TIMER_CURRCNT 0x390
#define TIMER_ENTRY 0x320
#define APIC_REGISTER_LVT_TIMER 0x320
#define APIC_LVT_INT_MASKED (1 << 16)
#define APIC_LVT_TIMER_MODE_PERIODIC (1 << 17)
#include "idt.h"
void init_apic_timer();
void test_apic_timer();

__attribute__((interrupt)) void apic_timer_handler(struct IDTEFrame *frame);
