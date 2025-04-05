#include "include/acpi.h"
#include "include/apic.h"
#include "include/bootinfo.h"
#include "include/flanterm.h"
#include "include/gdt.h"
#include "include/idt.h"
#include "include/kernel.h"
#include "include/limine.h"
#include "include/paging.h"
#include "include/pmm.h"
#include "include/util.h"
#include "limits.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

Kernel kernel = {0};

void init_framebuffer() {
  uint32_t *fb_addr = (uint32_t *)kernel.framebuffer[0]->address;
  uint64_t width = kernel.framebuffer[0]->width;
  uint64_t height = kernel.framebuffer[0]->height;
  uint64_t pitch = kernel.framebuffer[0]->pitch / 4;

  for (uint64_t y = 0; y < height; y++) {
    for (uint64_t x = 0; x < width; x++) {
      fb_addr[y * pitch + x] = kernel.bg_colour;
    }
  }
}

void init_kernel() {
  if (fb_request.response == NULL ||
      fb_request.response->framebuffer_count < 1) {
    return;
  }
  kernel.kernel_addr = *(kernel_address_request.response);
  kernel.kernel_file = *(kernel_file_req.response);
  kernel.kernel_size = (uint64_t)kernel.kernel_file.kernel_file->size;
  kernel.rsdp_table = (RSDP *)(rsdp_request.response)->address;
  kernel.memmap = *memmap_request.response;
  kernel.hhdm = (hhdm_request.response)->offset;
  kernel.framebuffer = fb_request.response->framebuffers;
  kernel.fg_colour = 0xFFFFFF;
  kernel.bg_colour = 0x000068;
  kernel.ft_ctx = flanterm_fb_init(
      NULL, NULL, kernel.framebuffer[0]->address, kernel.framebuffer[0]->width,
      kernel.framebuffer[0]->height, kernel.framebuffer[0]->pitch,
      kernel.framebuffer[0]->red_mask_size,
      kernel.framebuffer[0]->red_mask_shift,
      kernel.framebuffer[0]->green_mask_size,
      kernel.framebuffer[0]->green_mask_shift,
      kernel.framebuffer[0]->blue_mask_size,
      kernel.framebuffer[0]->blue_mask_shift, NULL, NULL, NULL,
      &kernel.bg_colour, &kernel.fg_colour, NULL, &kernel.fg_colour, NULL, 1, 0,
      1, 0, 0, 0);
}

void kmain(void) {
  init_kernel();
  init_framebuffer();
  init_PMM();
  initPML4();
  initGdt();
  InitIdt();
  init_acpi();
  init_apic();
  halt();
}
