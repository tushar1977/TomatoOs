#include "include/kernel.h"
#include "include/limine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Set the base revision to 3, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.
uint32_t initial_page_dir[1024] = {
    0b10000011,             // Entry for page directory entry 0
    [1 ... 767] = 0,        // Fill remaining 767 entries with zero
    (0 << 22) | 0b10000011, // Map first page
    (1 << 22) | 0b10000011, // Map second page
    (2 << 22) | 0b10000011, // Map third page
    (3 << 22) | 0b10000011, // Map fourth page
    [772 ... 1023] = 0      // Fill remaining 252 entries with zero
};
__attribute__((
    used, section(".limine_requests"))) static volatile LIMINE_BASE_REVISION(3);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((
    used,
    section(
        ".limine_requests"))) static volatile struct limine_framebuffer_request
    framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used,
               section(".limine_requests_"
                       "start"))) static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((
    used,
    section(
        ".limine_requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER;

// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// Implement them as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!
// They CAN be moved to a different .c file.

// Halt and catch fire function.
static void hcf(void) {
  for (;;) {
    asm("hlt");
  }
}

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
  // Ensure the bootloader actually understands our base revision (see spec).
  if (LIMINE_BASE_REVISION_SUPPORTED == false) {
    hcf();
  }

  // Ensure we got a framebuffer.
  if (framebuffer_request.response == NULL ||
      framebuffer_request.response->framebuffer_count < 1) {
    hcf();
  }

  // Fetch the first framebuffer.
  struct limine_framebuffer *framebuffer =
      framebuffer_request.response->framebuffers[0];

  // Note: we assume the framebuffer model is RGB with 32-bit pixels.
  for (size_t i = 0; i < 100; i++) {
    volatile uint32_t *fb_ptr = framebuffer->address;
    fb_ptr[i * (framebuffer->pitch / 4) + i] = 0xffffff;
  }

  kernel_main(0, NULL);
}
