
#include "uacpi/kernel_api.h"
#include "kernel.h"
#include "paging.h"
#include "pmm.h"
#include "printf.h"
#include "uacpi/log.h"
#include "uacpi/platform/types.h"
#include "util.h"
#include <uacpi/platform/arch_helpers.h>
#include <uacpi/types.h>

// Returns the PHYSICAL address of the RSDP structure via *out_rsdp_address.
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {

  *out_rsdp_address =
      (uint64_t)(getPhysicalAddress((void *)kernel.rsdp_address));
  kprintf("%x", kernel.rsdp_address);
  return UACPI_STATUS_OK;
}

/*
 * Map a physical memory range starting at 'addr' with length 'len', and return
 * a virtual address that can be used to access it.
 *
 * NOTE: 'addr' may be misaligned, in this case the host is expected to round it
 *       down to the nearest page-aligned boundary and map that, while making
 *       sure that at least 'len' bytes are still mapped starting at 'addr'. The
 *       return value preserves the misaligned offset.
 *
 *       Example for uacpi_kernel_map(0x1ABC, 0xF00):
 *           1. Round down the 'addr' we got to the nearest page boundary.
 *              Considering a PAGE_SIZE of 4096 (or 0x1000), 0x1ABC rounded down
 *              is 0x1000, offset within the page is 0x1ABC - 0x1000 => 0xABC
 *           2. Requested 'len' is 0xF00 bytes, but we just rounded the address
 *              down by 0xABC bytes, so add those on top. 0xF00 + 0xABC =>
 * 0x19BC
 *           3. Round up the final 'len' to the nearest PAGE_SIZE boundary, in
 *              this case 0x19BC is 0x2000 bytes (2 pages if PAGE_SIZE is 4096)
 *           4. Call the VMM to map the aligned address 0x1000 (from step 1)
 *              with length 0x2000 (from step 3). Let's assume the returned
 *              virtual address for the mapping is 0xF000.
 *           5. Add the original offset within page 0xABC (from step 1) to the
 *              resulting virtual address 0xF000 + 0xABC => 0xFABC. Return it
 *              to uACPI.
 */
#define PAGE_SIZE 4096
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {

  uacpi_phys_addr aligned_addr = ALIGN_DOWN(addr, PAGE_SIZE);
  uacpi_size offset = addr - aligned_addr;
  uacpi_size adjusted_len = ALIGN_UP(len + offset, PAGE_SIZE);
  uacpi_size pages = adjusted_len / PAGE_SIZE;

  uint64_t virt_base = (uint64_t)kernel.hhdm + aligned_addr;
  uint64_t phys_base = (uint64_t)aligned_addr;

  for (uacpi_size i = 0; i < pages; ++i) {
    map_page(virt_base + i * PAGE_SIZE, phys_base + i * PAGE_SIZE,
             KERNEL_PFLAG_PRESENT, PAGE_SIZE);
  }

  return (void *)((uint64_t)kernel.hhdm + addr);
}

/*
 * Unmap a virtual memory range at 'addr' with a length of 'len' bytes.
 *
 * NOTE: 'addr' may be misaligned, see the comment above 'uacpi_kernel_map'.
 *       Similar steps to uacpi_kernel_map can be taken to retrieve the
 *       virtual address originally returned by the VMM for this mapping
 *       as well as its true length.
 */
void uacpi_kernel_unmap(void *addr, uacpi_size len) {
  // hhdm doesnt need unmap
}

#ifndef UACPI_FORMATTED_LOGGING
void uacpi_kernel_log(uacpi_log_level, const uacpi_char *) {

  uacpi_log_level lvl = UACPI_LOG_INFO;

  if (lvl == UACPI_LOG_WARN) {
    lvl = UACPI_LOG_WARN;
  }

  if (lvl == UACPI_LOG_ERROR) {
    lvl = UACPI_LOG_ERROR;
  }

  kprintf("%s", lvl);
}
#else
UACPI_PRINTF_DECL(2, 3)
void uacpi_kernel_log(uacpi_log_level, const uacpi_char *, ...);
void uacpi_kernel_vlog(uacpi_log_level, const uacpi_char *, uacpi_va_list);
#endif

/*
 * Only the above ^^^ API may be used by early table access and
 * UACPI_BAREBONES_MODE.
 */
#ifndef UACPI_BAREBONES_MODE

/*
 * Convenience initialization/deinitialization hooks that will be called by
 * uACPI automatically when appropriate if compiled-in.
 */
#ifdef UACPI_KERNEL_INITIALIZATION
/*
 * This API is invoked for each initialization level so that appropriate parts
 * of the host kernel and/or glue code can be initialized at different stages.
 *
 * uACPI API that triggers calls to uacpi_kernel_initialize and the respective
 * 'current_init_lvl' passed to the hook at that stage:
 * 1. uacpi_initialize() -> UACPI_INIT_LEVEL_EARLY
 * 2. uacpi_namespace_load() -> UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED
 * 3. (start of) uacpi_namespace_initialize() ->
 * UACPI_INIT_LEVEL_NAMESPACE_LOADED
 * 4. (end of) uacpi_namespace_initialize() ->
 * UACPI_INIT_LEVEL_NAMESPACE_INITIALIZED
 */
uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl);
void uacpi_kernel_deinitialize(void);
#endif

/*
 * Open a PCI device at 'address' for reading & writing.
 *
 * Note that this must be able to open any arbitrary PCI device, not just
 * those detected during kernel PCI enumeration, since the following pattern
 * is relatively common in AML firmware: Device (THC0)
 *    {
 *        // Device at 00:10.06
 *        Name (_ADR, 0x00100006)  // _ADR: Address
 *
 *        OperationRegion (THCR, PCI_Config, Zero, 0x0100)
 *        Field (THCR, ByteAcc, NoLock, Preserve)
 *        {
 *            // Vendor ID field in the PCI configuration space
 *            VDID,   32
 *        }
 *
 *        // Check if the device at 00:10.06 actually exists, that is reading
 *        // from its configuration space returns something other than 0xFFs.
 *        If ((VDID != 0xFFFFFFFF))
 *        {
 *            // Actually create the rest of the device's body if it's present
 *            // in the system, otherwise skip it.
 *        }
 *    }
 *
 * The handle returned via 'out_handle' is used to perform IO on the
 * configuration space of the device.
 */
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address,
                                          uacpi_handle *out_handle) {

  uacpi_pci_address *pci =
      (uacpi_pci_address *)k_malloc(sizeof(uacpi_pci_address));
  *pci = address;
  *out_handle = (uacpi_handle)pci;
  return UACPI_STATUS_OK;
}
void uacpi_kernel_pci_device_close(uacpi_handle addr) { k_free((void *)addr); }

/*
 * Read & write the configuration space of a previously open PCI device.
 */

uacpi_status uacpi_kernel_pci_read(uacpi_handle handle, uacpi_size offset,
                                   uacpi_u8 width, uacpi_u64 *out) {
  uacpi_pci_address *address = (uacpi_pci_address *)handle;
  if (address->segment != 0) {
    kprintf("[acpi::glue] (write) Bad PCI segment{%d}!\n", address->segment);
  }

  switch (width) {
  case 1:
    *out = pciConfigReadWord(address->bus, address->device, address->function,
                             offset & ~1);
    *out = (*out >> ((offset & 1) * 8)) & 0xFF;
    break;
  case 2:
    *out = pciConfigReadWord(address->bus, address->device, address->function,
                             offset);
    break;
  case 4:
    *out = pciConfigReadWord(address->bus, address->device, address->function,
                             offset);
    *out |= ((uacpi_u64)pciConfigReadWord(address->bus, address->device,
                                          address->function, offset + 2)
             << 16);
    break;
  default:
    return UACPI_STATUS_INVALID_ARGUMENT;
  }

  return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset,
                                    uacpi_u8 *value) {
  return uacpi_kernel_pci_read(device, offset, 1, (uacpi_u64 *)value);
}
uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset,
                                     uacpi_u16 *value) {

  return uacpi_kernel_pci_read(device, offset, 2, (uacpi_u64 *)value);
}
uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset,
                                     uacpi_u32 *value) {

  return uacpi_kernel_pci_read(device, offset, 4, (uacpi_u64 *)value);
}

uacpi_status uacpi_kernel_pci_write(uacpi_handle handle, uacpi_size offset,
                                    uacpi_u8 width, uacpi_u64 value) {
  uacpi_pci_address *address = (uacpi_pci_address *)handle;
  if (address->segment != 0) {
    kprintf("[acpi::glue] Bad PCI segment{%d}!\n", address->segment);
  }

  switch (width) {
  case 1: {
    {
      uint32_t dword = pciConfigReadWord(address->bus, address->device,
                                         address->function, offset & ~0x3);
      uint32_t shift = (offset & 0x3) * 8;
      dword &= ~(0xFF << shift);
      dword |= (value & 0xFF) << shift;
      ConfigWriteDword(address->bus, address->device, address->function,
                       offset & ~0x3, dword);
    }
  } break;
  case 2: {
    uint32_t dword = pciConfigReadWord(address->bus, address->device,
                                       address->function, offset & ~0x3);
    uint32_t shift = (offset & 0x2) * 8;
    dword &= ~(0xFFFF << shift);
    dword |= (value & 0xFFFF) << shift;
    ConfigWriteDword(address->bus, address->device, address->function,
                     offset & ~0x3, dword);
    break;
  }
  case 4:
    ConfigWriteDword(address->bus, address->device, address->function, offset,
                     value);
    break;
  default:
    return UACPI_STATUS_INVALID_ARGUMENT;
  }

  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset,
                                     uacpi_u8 value) {
  return uacpi_kernel_pci_write(device, offset, 1, value);
}
uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset,
                                      uacpi_u16 value) {
  return uacpi_kernel_pci_write(device, offset, 2, value);
}
uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset,
                                      uacpi_u32 value) {
  return uacpi_kernel_pci_write(device, offset, 4, value);
}

/*
 * Map a SystemIO address at [base, base + len) and return a
 * kernel-implemented handle that can be used for reading and writing the IO
 * range.
 *
 * NOTE: The x86 architecture uses the in/out family of instructions
 *       to access the SystemIO address space.
 */
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len,
                                 uacpi_handle *out_handle) {
  *out_handle = (uacpi_handle)base;
  return UACPI_STATUS_OK;
}
void uacpi_kernel_io_unmap(uacpi_handle handle) { (void)0; }

/*
 * Read/Write the IO range mapped via uacpi_kernel_io_map
 * at a 0-based 'offset' within the range.
 *
 * NOTE:
 * The x86 architecture uses the in/out family of instructions
 * to access the SystemIO address space.
 *
 * You are NOT allowed to break e.g. a 4-byte access into four 1-byte
 * accesses. Hardware ALWAYS expects accesses to be of the exact width.
 */
uacpi_status uacpi_kernel_io_read(uacpi_handle handle, uacpi_size offset,
                                  uacpi_u8 width, uacpi_u64 *out) {
  uacpi_io_addr target = (uacpi_io_addr)handle + offset;
  switch (width) {
  case 1:
    *out = inPortB(target);
    break;
  case 2:
    *out = inPortB(target);
    break;
  case 4:
    *out = inPortB(target);
    break;
  default:
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset,
                                   uacpi_u8 *out_value) {
  return uacpi_kernel_io_read(handle, offset, 1, (uacpi_u64 *)out_value);
}
uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset,
                                    uacpi_u16 *out_value) {
  return uacpi_kernel_io_read(handle, offset, 2, (uacpi_u64 *)out_value);
}
uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset,
                                    uacpi_u32 *out_value) {
  return uacpi_kernel_io_read(handle, offset, 4, (uacpi_u64 *)out_value);
}

uacpi_status uacpi_kernel_io_write(uacpi_handle handle, uacpi_size offset,
                                   uacpi_u8 width, uacpi_u64 value) {
  uacpi_io_addr target = (uacpi_io_addr)handle + offset;
  switch (width) {
  case 1:
    outPortB(target, value);
    break;
  case 2:
    outPortB(target, value);
    break;
  case 4:
    outPortB(target, value);
    break;
  default:
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset,
                                    uacpi_u8 in_value) {
  return uacpi_kernel_io_write(handle, offset, 1, in_value);
}
uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset,
                                     uacpi_u16 in_value) {
  return uacpi_kernel_io_write(handle, offset, 2, in_value);
}
uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset,
                                     uacpi_u32 in_value) {
  return uacpi_kernel_io_write(handle, offset, 4, in_value);
}
/*
 * Allocate a block of memory of 'size' bytes.
 * The contents of the allocated memory are unspecified.
 */
void *uacpi_kernel_alloc(uacpi_size size) { return (void *)k_malloc(size); }

#ifdef UACPI_NATIVE_ALLOC_ZEROED
/*
 * Allocate a block of memory of 'size' bytes.
 * The returned memory block is expected to be zero-filled.
 */
void *uacpi_kernel_alloc_zeroed(uacpi_size size);
#endif

/*
 * Free a previously allocated memory block.
 *
 * 'mem' might be a NULL pointer. In this case, the call is assumed to be a
 * no-op.
 *
 * An optionally enabled 'size_hint' parameter contains the size of the original
 * allocation. Note that in some scenarios this incurs additional cost to
 * calculate the object size.
 */
#ifndef UACPI_SIZED_FREES
void uacpi_kernel_free(void *mem) { k_free(mem); }
#else
void uacpi_kernel_free(void *mem, uacpi_size size_hint);
#endif
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
  return 0; // returning 0 instead of NULL for u64
}

/*
 * Spin for N microseconds.
 */
void uacpi_kernel_stall(uacpi_u8 usec) { (void)usec; }

/*
 * Sleep for N milliseconds.
 */
void uacpi_kernel_sleep(uacpi_u64 msec) { (void)msec; }

/*
 * Create/free an opaque non-recursive kernel mutex object.
 */
uacpi_handle uacpi_kernel_create_mutex(void) {
  return (uacpi_handle)0x1001; // dummy non-null handle
}
void uacpi_kernel_free_mutex(uacpi_handle handle) { (void)handle; }

/*
 * Create/free an opaque kernel (semaphore-like) event object.
 */
uacpi_handle uacpi_kernel_create_event(void) {
  return (uacpi_handle)0x1002; // dummy non-null handle
}
void uacpi_kernel_free_event(uacpi_handle handle) { (void)handle; }

/*
 * Returns a unique identifier of the currently executing thread.
 */
uacpi_thread_id uacpi_kernel_get_thread_id(void) {
  return (uacpi_thread_id)1; // dummy thread ID, not UACPI_THREAD_ID_NONE
}

/*
 * Try to acquire the mutex with a millisecond timeout.
 */
uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle,
                                        uacpi_u16 timeout) {
  (void)handle;
  (void)timeout;
  return UACPI_STATUS_OK;
}
void uacpi_kernel_release_mutex(uacpi_handle handle) { (void)handle; }

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
  (void)handle;
  (void)timeout;
  return UACPI_FALSE;
}

void uacpi_kernel_signal_event(uacpi_handle handle) { (void)handle; }

void uacpi_kernel_reset_event(uacpi_handle handle) { (void)handle; }
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *) {
  return UACPI_STATUS_OK;
}

uacpi_status
uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler,
                                       uacpi_handle ctx,
                                       uacpi_handle *out_irq_handle) {
  *out_irq_handle = (uacpi_handle)0xDEADBEEF;
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler,
                                                      uacpi_handle irq_handle) {
  return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
  return (uacpi_handle)0xCAFEBABE;
}

void uacpi_kernel_free_spinlock(uacpi_handle) {}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle) { return 0; }

void uacpi_kernel_unlock_spinlock(uacpi_handle, uacpi_cpu_flags) {}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type, uacpi_work_handler,
                                        uacpi_handle ctx) {
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
  return UACPI_STATUS_OK;
}
#endif // !UACPI_BAREBONES_MODE
