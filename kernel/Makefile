DEFAULT_HOST!=../default-host.sh
HOST?=DEFAULT_HOST
HOSTARCH!=../target-triplet-to-arch.sh $(HOST)

CFLAGS?=-O2 -g
CPPFLAGS?=
LDFLAGS?=
LIBS?=

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
BOOTDIR?=$(EXEC_PREFIX)/boot
INCLUDEDIR?=$(PREFIX)/include

CFLAGS:=$(CFLAGS) -ffreestanding -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -D__is_kernel -Iinclude

LDFLAGS:=$(LDFLAGS)
# Engine is optional (ENABLE_LIBENGINE, exported by config.sh). When present,
# the link order is -lengine -lkxx -lk: the engine calls the freestanding C++
# runtime (operator new / kstd::fatal) from libkxx, which in turn calls kmalloc/
# kfree and the halt primitives from libk. Archive members are pulled on demand
# by the kernel's references (e.g. libengine_p0_smoke). When absent, the kernel
# is pure C — no C++ runtime is needed, so libkxx is left out entirely — the
# smoke battery is forced off and LPL_PLUGIN_UNAVAILABLE is set.
ENABLE_LIBENGINE?=1
ifeq ($(ENABLE_LIBENGINE),1)
LIBS:=$(LIBS) -nostdlib -lengine -lkxx -lk -lgcc
# Relink the kernel whenever the engine archive changes. -lengine is resolved
# from the sysroot, so a rebuilt libengine.a (e.g. after an LplPlugin header
# edit) would otherwise leave a stale engine baked into lpl.kernel. wildcard
# keeps standalone `make` (no DESTDIR/sysroot) from gaining a bogus prereq.
ENGINE_ARCHIVE:=$(wildcard $(DESTDIR)/usr/lib/libengine.a)
else
LIBS:=$(LIBS) -nostdlib -lk -lgcc
CPPFLAGS:=$(CPPFLAGS) -DLPL_PLUGIN_UNAVAILABLE=1
SMOKE_TESTS:=0
endif

# Smoke/diagnostic battery (libengine P0..P6 + kernel smoke batch). ON by default
# for the script/Makefile build (debug); it calls libengine_* so it is forced off
# when the engine is absent. Pass SMOKE_TESTS=0 (or xmake "release") to compile
# the whole battery out of a production image.
SMOKE_TESTS?=1
ifeq ($(SMOKE_TESTS),1)
CPPFLAGS:=$(CPPFLAGS) -DLPL_KERNEL_ENABLE_SMOKE_TESTS
endif

# Graphics mode support (passed from build script)
GRAPHICS_MODE?=0
ASFLAGS:=-DGRAPHICS_MODE=$(GRAPHICS_MODE)

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CFLAGS:=$(CFLAGS) $(KERNEL_ARCH_CFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(KERNEL_ARCH_CPPFLAGS)
LDFLAGS:=$(LDFLAGS) $(KERNEL_ARCH_LDFLAGS)
LIBS:=$(LIBS) $(KERNEL_ARCH_LIBS)

# CFLAGS += -fstack-protector-strong
# LDFLAGS += -fstack-protector-strong

KERNEL_OBJS=\
$(KERNEL_ARCH_OBJS) \
kernel/memory/heap.o \
kernel/memory/slab.o \
kernel/memory/frame_arena.o \
kernel/memory/pool_allocator.o \
kernel/memory/ring_buffer.o \
kernel/memory/tlsf.o \
kernel/core/kernel_console.o \
kernel/core/kernel_splash.o \
kernel/core/kernel_smp.o \
kernel/core/kernel_smoke_batch.o \
kernel/core/libengine_smoke.o \
kernel/diag/sysmon.o \
kernel/core/kernel.o \
kernel/memory/helpers/pmm_helper.o \
kernel/memory/helpers/heap_helper.o \
kernel/memory/helpers/core_allocators_helper.o \
kernel/testing/smoke_test.o \

OBJS=\
$(ARCHDIR)/boot/crti.o \
$(ARCHDIR)/crtbegin.o \
$(KERNEL_OBJS) \
$(ARCHDIR)/crtend.o \
$(ARCHDIR)/boot/crtn.o \

LINK_LIST=\
$(LDFLAGS) \
$(ARCHDIR)/boot/crti.o \
$(ARCHDIR)/crtbegin.o \
$(KERNEL_OBJS) \
$(LIBS) \
$(ARCHDIR)/crtend.o \
$(ARCHDIR)/boot/crtn.o \

.PHONY: all clean install install-headers install-kernel
.SUFFIXES: .o .c .s .S

all: lpl.kernel

lpl.kernel: $(OBJS) $(ARCHDIR)/linker.ld $(ENGINE_ARCHIVE)
	$(CC) -T $(ARCHDIR)/linker.ld -o $@ $(CFLAGS) $(LINK_LIST)
	grub-file --is-x86-multiboot lpl.kernel

$(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o:
	OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@

.c.o:
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS) $(CPPFLAGS)

.s.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS) $(ASFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS) $(ASFLAGS)

clean:
	rm -f lpl.kernel
	rm -f $(OBJS) *.o */*.o */*/*.o
	rm -f $(OBJS:.o=.d) *.d */*.d */*/*.d

debug: CFLAGS+=-DDEBUG -g3 -O0
debug: all

install: install-headers install-kernel

install-headers:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp -R --preserve=timestamps include/. $(DESTDIR)$(INCLUDEDIR)/.

install-kernel: lpl.kernel
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp lpl.kernel $(DESTDIR)$(BOOTDIR)

-include $(OBJS:.o=.d)
