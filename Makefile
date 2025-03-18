CC = i686-elf-gcc
LD = i686-elf-ld -m elf_i386
CFLAGS = -ffreestanding -g -Wall -Wextra -m32 -Iinclude -Ilimine
LDFLAGS = -nostdlib
BUILD_DIR = build
BIN_DIR = bin
SYSROOT_DIR = sysroot
KERNEL_BIN = $(BIN_DIR)/myos

# Source files for the kernel
SRC =  src/drivers/keyboard.c \
       src/drivers/serial.c \
       src/stdio/puts.c \
       src/stdio/tty.c \
       src/stdio/putchar.c \
       src/stdio/printf.c \
			 src/kernel.c \
			 src/main.c \
       src/stdlib/abort.c \
       src/string/memcmp.c \
       src/string/memcpy.c \
       src/string/memmove.c \
       src/string/memset.c \
       src/string/memstr.c \
       src/string/strncpy.c \
       src/string/strlen.c \
       src/fs/vfs.c \
       src/mem/free.c \
       src/mem/malloc.c \
       src/mem/realloc.c \
       src/mem/paging.c \
       src/mem/calloc.c \
       src/processor/spinlock.c \
       src/processor/kprint.c \
       src/cpu/gdt.c \
       src/cpu/util.c \
       src/cpu/syscall.c \
       src/cpu/idt.c

# Assembly source files
ASM_SRC = src/asm/idt.asm src/asm/gdt.asm

# Object files for the kernel
OBJ = $(SRC:src/%.c=$(BUILD_DIR)/%.o) $(ASM_SRC:src/%.asm=$(BUILD_DIR)/%.o)

# Source files for libk (kernel library)
LIBK_SRC = $(SRC)  # You can add more files specific to libk if needed
LIBK_OBJ = $(LIBK_SRC:src/%.c=$(BUILD_DIR)/%.libk.o)

# Output library file
LIBK = $(BIN_DIR)/libk.a

all: clean libk kernel image install_limine

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) image.iso iso_root
	rm -f $(OBJ) $(LIBK_OBJ) *.o */*.o */*/*.o
	rm -f $(OBJ:.o=.d) $(LIBK_OBJ:.o=.d) *.d */*.d */*/*.d

install-headers:
	mkdir -p $(SYSROOT_DIR)/usr/include
	cp -R --preserve=timestamps include/. $(SYSROOT_DIR)/usr/include/.

install-libs: $(LIBK)
	mkdir -p $(SYSROOT_DIR)/usr/lib
	cp $(LIBK) $(SYSROOT_DIR)/usr/lib/.

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Build libk.a
libk: $(LIBK)

$(LIBK): $(LIBK_OBJ)
	$(AR) rcs $@ $(LIBK_OBJ)

# Rule to compile libk object files
$(BUILD_DIR)/%.libk.o: src/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -D__is_libk -c $< -o $@

# Rule to compile .asm files into .o files
$(BUILD_DIR)/%.o: src/%.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	nasm -f elf32 $< -o $@

# Build the kernel
kernel: $(BUILD_DIR) $(BIN_DIR) $(OBJ) $(LIBK)
	$(LD) $(LDFLAGS) -T linker.ld -o $(KERNEL_BIN) $(OBJ) -L$(BIN_DIR) -lk

# Rule to compile kernel object files
$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Create the bootable ISO image
image: kernel
	mkdir -p iso_root/boot
	cp -v $(KERNEL_BIN) iso_root/boot/
	mkdir -p iso_root/boot/limine
	cp -v limine/limine-bios.sys limine/limine-bios-cd.bin \
	      limine/limine-uefi-cd.bin limine.conf iso_root/boot/limine/

	mkdir -p iso_root/EFI/BOOT
	cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/

	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o image.iso

# Install Limine bootloader
install_limine: image
	./limine/limine bios-install image.iso

.PHONY: all clean install-headers install-libs libk kernel image install_limine
