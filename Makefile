BUILD ?= debug
ifeq ($(filter $(BUILD),debug release test),)
$(error BUILD must be one of: debug release test)
endif

CC      := gcc
LD      := ld
OBJCOPY := objcopy
QEMU    := qemu-system-x86_64

ROOT     := $(CURDIR)
OUT      := build/$(BUILD)
KERNEL   := $(OUT)/sobi-kernel.elf
ISO      := $(OUT)/sobi.iso
MAP      := $(OUT)/sobi-kernel.map
LIMINE   := third_party/limine
OVMF     ?= /usr/share/OVMF/OVMF_CODE_4M.fd
QEMU_MEM ?= 256M

C_SRCS := $(shell find kernel -name '*.c' | sort)
S_SRCS := $(shell find kernel -name '*.S' | sort)
OBJS   := $(patsubst %.c,$(OUT)/%.o,$(C_SRCS)) $(patsubst %.S,$(OUT)/%.o,$(S_SRCS))
DEPS   := $(OBJS:.o=.d)

CPPFLAGS := -Iinclude -MMD -MP
CPPFLAGS += -DSOBI_BUILD_$(shell echo $(BUILD) | tr a-z A-Z)=1 -DSOBI_BUILD_NAME=\"$(BUILD)\"

CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto
CFLAGS += -fno-PIC -fno-PIE -fno-builtin -m64 -march=x86-64
CFLAGS += -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel
CFLAGS += -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS += -Wvla -Wundef -Wpointer-arith -Wwrite-strings -Werror

ifeq ($(BUILD),release)
CFLAGS   += -O2 -g
else
CFLAGS   += -O0 -g3
endif
ifeq ($(BUILD),test)
CPPFLAGS += -DSOBI_TEST=1
endif

ASFLAGS := -m64 -g

LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -z noexecstack
LDFLAGS += --warn-common --fatal-warnings -T linker.ld -Map=$(MAP)

QEMU_BASE := -M q35 -m $(QEMU_MEM) -no-reboot -cdrom $(ISO)
QEMU_TEST := -device isa-debug-exit,iobase=0xf4,iosize=0x04

.PHONY: all kernel iso run run-uefi gdb test verify-elf limine clean distclean

all: kernel

kernel: $(KERNEL)

$(KERNEL): $(OBJS) linker.ld Makefile scripts/gensyms.sh
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	./scripts/gensyms.sh $@ $(OUT)/ksyms.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(OUT)/ksyms.c -o $(OUT)/ksyms.o
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(OUT)/ksyms.o

$(OUT)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: %.S Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -c $< -o $@

-include $(DEPS)

limine: $(LIMINE)/limine

$(LIMINE)/limine:
	./scripts/fetch-limine.sh

iso: $(ISO)

$(ISO): $(KERNEL) boot/limine.conf scripts/mkiso.sh $(LIMINE)/limine
	./scripts/mkiso.sh $(KERNEL) boot/limine.conf $@

run: $(ISO)
	$(QEMU) $(QEMU_BASE) -display none -serial stdio

run-uefi: $(ISO)
	$(QEMU) $(QEMU_BASE) -drive if=pflash,format=raw,readonly=on,file=$(OVMF) -display none -serial stdio

gdb: $(ISO)
	./scripts/gdb.sh $(KERNEL) "$(QEMU) $(QEMU_BASE) -display none -serial stdio -s -S"

verify-elf: $(KERNEL)
	./scripts/verify-elf.sh $(KERNEL)

test:
	$(MAKE) BUILD=test verify-elf
	$(MAKE) BUILD=test iso
	@fail=0; \
	for t in tests/*_test.sh; do \
		echo "== $$t =="; \
		./$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "test: FAILED"; exit 1; fi; \
	echo "test: all suites passed"

clean:
	rm -rf build

distclean: clean
	rm -rf $(LIMINE)
