#pragma once
#include "stdint.h"
#define PIT_FREQ 1193182
unsigned read_pit_count(void);
void set_pit_count(unsigned count);
void wait_for_pit();
