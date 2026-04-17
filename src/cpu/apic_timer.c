#include "include/apic_timer.h"
#include "apic.h"
#include "include/pit.h"
#include "kernel.h"
#include "printf.h"
#include "util.h"

__attribute__((interrupt)) void apic_timer_handler(struct IDTEFrame *frame) {

  kernel.apic_ticks++;
  end_of_interrupt();
}
void init_apic_timer() {

  kernel.apic_ticks = 0;
  write_lapic(kernel.lapic_base, APIC_REGISTER_TIMER_DIV, 0x3);

  write_lapic(kernel.lapic_base, APIC_REGISTER_TIMER_INITCNT, 0xFFFFFFFF);

  disable_interrupts();
  wait_for_pit();
  write_lapic(kernel.lapic_base, APIC_REGISTER_LVT_TIMER, APIC_LVT_INT_MASKED);

  uint32_t ticksIn10ms =
      0xFFFFFFFF - read_lapic(kernel.lapic_base, APIC_REGISTER_TIMER_CURRCNT);

  write_lapic(kernel.lapic_base, APIC_REGISTER_LVT_TIMER,
              32 | APIC_LVT_TIMER_MODE_PERIODIC);
  write_lapic(kernel.lapic_base, APIC_REGISTER_TIMER_DIV, 0x3);
  write_lapic(kernel.lapic_base, APIC_REGISTER_TIMER_INITCNT, ticksIn10ms);
  uint32_t gsi = kernel.irq_overrides[0];
  set_ioapic_entry(0x21, 0, 0, 0);
  unmask_ioapic(gsi, 0);
}

void sleep(uint64_t target_ticks) {
  uint64_t start = kernel.apic_ticks;
  while ((kernel.apic_ticks - start) < target_ticks) {
    wait_for_interrupt();
  }
}
