#include "include/pit.h"
#include "include/printf.h"
#include "include/util.h"

unsigned read_pit_count(void) {
  unsigned count;

  outPortB(0x43, 0x00);

  uint8_t low = inPortB(0x40);
  uint8_t high = inPortB(0x40);

  count = low | (high << 8);
  return count;
}

void test() {
  disable_interrupts();
  set_pit_count(11931);

  uint16_t last = 0xFFFF;
  while (1) {
    uint16_t val = read_pit_count();
    if (val != last && val < last) {
      last = val;
    }
    if (val == 0) {

      break;
    }
  }
  return;
}
void set_pit_count(unsigned count) {
  // Disable interrupts
  outPortB(0x43, 0x30);
  // Set low byte
  outPortB(0x40, count & 0xFF);          // Low byte
  outPortB(0x40, (count & 0xFF00) >> 8); // High byte
  return;
}
