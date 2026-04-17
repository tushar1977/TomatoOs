#include "limine.h"
__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_memmap_request
    memmap_request = {.id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct
    limine_executable_file_request kernel_file_req = {
        .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID, .revision = 0};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_hhdm_request
    hhdm_request = {.id = LIMINE_HHDM_REQUEST_ID, .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct
    limine_executable_address_request kernel_address_request = {
        .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID, .revision = 2};

__attribute__((
    used,
    section(
        ".limine_requests"))) static volatile struct limine_framebuffer_request
    fb_request = {.id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_rsdp_request
    rsdp_request = {.id = LIMINE_RSDP_REQUEST_ID, .revision = 3};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_smbios_request
    smp_request = {.id = LIMINE_SMBIOS_REQUEST_ID, .revision = 0};

__attribute__((
    used, section(".limine_requests"))) static struct limine_internal_module
    initrd = {.path = "initrd", .flags = LIMINE_INTERNAL_MODULE_REQUIRED};

__attribute__((used,
               section(".limine_requests_start"))) static volatile uint64_t
    limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t
    limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static void hcf(void) {
  for (;;) {
#if defined(__x86_64__)
    asm("hlt");
#elif defined(__aarch64__) || defined(__riscv)
    asm("wfi");
#elif defined(__loongarch64)
    asm("idle 0");
#endif
  }
}
