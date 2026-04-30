#pragma once
#include "stdint.h"
#include <stddef.h>
static uint8_t heap[1024 * 1024];
static size_t heap_offset = 0;
void *kmalloc(size_t size);
void kfree(void *ptr);
