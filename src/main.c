#include "include/bootinfo.h"
#include "include/gdt.h"
#include "include/idt.h"
#include "include/kernel.h"
#include "include/limine.h"
#include "include/paging.h"
#include "include/pmm.h"
#include "include/stdio.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

Kernel kernel = {0};

void init_kernel() {
  if (fb_request.response == NULL ||
      fb_request.response->framebuffer_count < 1) {
    return;
  }
  kernel.kernel_addr = *(kernel_address_request.response);
  kernel.kernel_file = *(kernel_file_req.response);
  kernel.kernel_size = (uint64_t)kernel.kernel_file.kernel_file->size;
  kernel.memmap = *memmap_request.response;
  kernel.hhdm = (hhdm_request.response)->offset;
  kernel.framebuffer = (fb_request.response)->framebuffers;
  kernel.fg_colour = 0xd8d9d7;
  kernel.bg_colour = 0x012456;
}

void kmain(void) {
  init_kernel();
  init_PMM();
  initPML4();
  initGdt();
  InitIdt();
  struct limine_framebuffer *framebuffer = fb_request.response->framebuffers[0];
  for (size_t i = 0; i < 100; i++) {
    volatile uint32_t *fb_ptr = framebuffer->address;
    fb_ptr[i * (framebuffer->pitch / 4) + i] = 0xffffff;
  }

  hcf();
}
