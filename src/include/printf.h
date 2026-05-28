#pragma once
#include "spinlock.h"

int kprintf(const char *_restrict, ...);
void clear();
void k_ok();
void k_debug(const char *msg);
void k_fail();
