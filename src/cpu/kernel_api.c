#include "uacpi/kernel_api.h"
#include "apic_timer.h"
#include "irq.h"
#include "kernel.h"
#include "klog.h"
#include "kmem.h"
#include "paging.h"
#include "pci.h"
#include "pmm.h"
#include "printf.h"
#include "spinlock.h"
#include "uacpi/log.h"
#include "uacpi/platform/types.h"
#include "util.h"
#include <uacpi/platform/arch_helpers.h>
#include <uacpi/types.h>

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
  *out_rsdp_address = virt_to_phys((void *)kernel.rsdp_address);
  return UACPI_STATUS_OK;
}

#define PAGE_SIZE 4096
void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
  uint64_t offset = addr & (PAGE_SIZE - 1);
  uint64_t aligned_phys = addr & ~(PAGE_SIZE - 1);
  uint64_t mapped_len =
      ((offset + len + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
  size_t num_pages = mapped_len / PAGE_SIZE;

  uint64_t virt = kernel.hhdm + aligned_phys;

  map_page(virt, aligned_phys, PTE_PRESENT | PTE_WRITABLE, num_pages);

  return (void *)(virt + offset);
}
void uacpi_kernel_unmap(void *addr, uacpi_size len) {
  // TODO:
}

#ifndef UACPI_FORMATTED_LOGGING
void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char *msg) {
  klog_level_t level;

  switch (lvl) {
  case UACPI_LOG_WARN:
    level = KLOG_WARN;
    break;
  case UACPI_LOG_ERROR:
    level = KLOG_ERROR;
    break;
  case UACPI_LOG_INFO:
  default:
    level = KLOG_INFO;
    break;
  }

  klog(level, "uACPI");
  kprintf("%s\n", msg);
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

  uacpi_pci_address *addr = kmalloc(sizeof(uacpi_pci_address));
  if (!addr)
    return UACPI_STATUS_OUT_OF_MEMORY;
  *addr = address;
  *out_handle = (uacpi_handle)addr;
  return UACPI_STATUS_OK;
}
void uacpi_kernel_pci_device_close(uacpi_handle addr) { kfree(addr); }

uacpi_status uacpi_kernel_pci_read(uacpi_handle handle, uacpi_size offset,
                                   uacpi_u8 width, uacpi_u64 *out) {
  uacpi_pci_address *address = (uacpi_pci_address *)handle;
  if (address->segment != 0) {
    klog(KLOG_WARN, "PCI");
    kprintf("unsupported PCI segment %d (read)\n", address->segment);
  }

  switch (width) {
  case 1:
    *out = pci_read_config8(address->bus, address->device, address->function,
                            (uint8_t)offset);
    break;
  case 2:
    *out = pci_read_config16(address->bus, address->device, address->function,
                             (uint8_t)offset);
    break;
  case 4:
    *out = pci_read_config32(address->bus, address->device, address->function,
                             (uint8_t)offset);
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
    klog(KLOG_WARN, "PCI");
    kprintf("unsupported PCI segment %d (write)\n", address->segment);
  }

  switch (width) {
  case 1:
    pci_write_config8(address->bus, address->device, address->function,
                      (uint8_t)offset, (uint8_t)value);
    break;
  case 2:
    pci_write_config16(address->bus, address->device, address->function,
                       (uint8_t)offset, (uint16_t)value);
    break;
  case 4:
    pci_write_config32(address->bus, address->device, address->function,
                       (uint8_t)offset, (uint32_t)value);
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
  (void)len;
  uacpi_io_addr *handle = kmalloc(sizeof(uacpi_io_addr));
  if (!handle) {
    return UACPI_STATUS_OUT_OF_MEMORY;
  }
  *handle = base;
  *out_handle = (uacpi_handle)handle;
  return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) { kfree(handle); }

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
    *out = inPortW(target);
    break;
  case 4:
    *out = inPortD(target);
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
    outPortW(target, value);
    break;
  case 4:
    outPortD(target, value);
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
void *uacpi_kernel_alloc(uacpi_size size) { return kmalloc(size); }

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
void uacpi_kernel_free(void *mem) { kfree(mem); }
#else
void uacpi_kernel_free(void *mem, uacpi_size size_hint);
#endif
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
  return kernel.apic_ticks * 10000000ULL;
}
void uacpi_kernel_stall(uacpi_u8 usec) {
  uint32_t count =
      (uint32_t)(((uint64_t)kernel.lapic_ticks_per_10ms * usec) / 10000ULL);
  lapic_busy_wait_ticks(count);
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
  uint32_t ticks_per_ms = kernel.lapic_ticks_per_10ms / 10;
  for (uacpi_u64 i = 0; i < msec; i++) {
    lapic_busy_wait_ticks(ticks_per_ms);
  }
}
/*
 * Create/free an opaque non-recursive kernel mutex object.
 */
uacpi_handle uacpi_kernel_create_mutex(void) {
  Spinlock *lock = kmalloc(sizeof(Spinlock));
  return (uacpi_handle)lock;
}
void uacpi_kernel_free_mutex(uacpi_handle handle) { kfree((void *)handle); }

/*
 * Create/free an opaque kernel (semaphore-like) event object.
 */
uacpi_handle uacpi_kernel_create_event(void) { return (uacpi_handle)1; }
void uacpi_kernel_free_event(uacpi_handle) { asm volatile("nop"); }

/*
 * Returns a unique identifier of the currently executing thread.
 */
uacpi_thread_id uacpi_kernel_get_thread_id(void) { return 0; }

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle lock, uacpi_u16) {
  (void)lock;
  return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle lock) { (void)lock; }

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle, uacpi_u16) { return 1; }

void uacpi_kernel_signal_event(uacpi_handle) { asm volatile("nop"); }

void uacpi_kernel_reset_event(uacpi_handle) { asm volatile("nop"); }

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *) {
  return UACPI_STATUS_OK;
}

#if defined(__x86_64__)

uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx,
    uacpi_handle *out_irq_handle) {
  uint8_t vector = irq_create(irq, IRQ_TYPE_LEGACY,
                              (void (*)(void *))((void *)handler), ctx, 0);

  if (vector == 0)
    return UACPI_STATUS_OUT_OF_MEMORY;

  *out_irq_handle = (uacpi_handle)(uintptr_t)vector;

  return UACPI_STATUS_OK;
}

#else

uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler base, uacpi_handle ctx,
    uacpi_handle *out_irq_handle) {
  (void)irq;
  (void)base;
  (void)ctx;
  (void)out_irq_handle;
  return UACPI_STATUS_OK;
}

#endif

uacpi_interrupt_ret uacpi_kernel_disable_interrupts(void) {
  disable_interrupts();
}

/*
 * Restore the state of the interrupt flags to the kernel-defined value provided
 * in 'state'.
 */
void uacpi_kernel_restore_interrupts(uacpi_interrupt_ret state) {
  (void)state;
  enable_interrupts();
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler,
                                                      uacpi_handle irq_handle) {
  (void)irq_handle;
  return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
  Spinlock *lock = (Spinlock *)kmalloc(sizeof(Spinlock));
  return (uacpi_handle)lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle hnd) { kfree((void *)hnd); }

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle hnd) {
  (void)hnd;
  return UACPI_STATUS_OK;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle hnd, uacpi_cpu_flags) {
  (void)hnd;
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type, uacpi_work_handler,
                                        uacpi_handle ctx) {
  (void)ctx;
  return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
  return UACPI_STATUS_OK;
}
#endif // !UACPI_BAREBONES_MODE
