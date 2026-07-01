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
# -lengine goes BEFORE -lk: the engine's operator new/kstd code calls kmalloc/
# kfree (resolved from libk) and its archive members are pulled on demand by the
# kernel's references (e.g. libengine_p0_smoke). When absent, the kernel links
# without it, the smoke battery is forced off, and LPL_PLUGIN_UNAVAILABLE is set.
ENABLE_LIBENGINE?=1
ifeq ($(ENABLE_LIBENGINE),1)
LIBS:=$(LIBS) -nostdlib -lengine -lk -lgcc
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

# Freestanding C++ runtime support (operator new/delete + Itanium ABI stubs +
# kernel_std support). No exceptions/RTTI/threadsafe-statics: the kernel ships
# no unwinder/typeinfo/guard runtime. Compiled with the C++ driver ($(CXX)).
# Drop -nostdinc for C++: the freestanding libstdc++ headers (<cstddef>, etc.)
# live in the toolchain include dir, which -nostdinc would hide. libk headers
# remain reachable via the sysroot -isystem path.
# gnu++17 (not gnu++23): the kernel's own runtime uses no C++20/23 features, so
# it builds on older cross toolchains (e.g. the CI's gcc 10). C++23 stays a
# libengine-only requirement — libengine has its own gcc14/gnu++23 build.
CXXFLAGS:=$(filter-out -nostdinc,$(CFLAGS)) -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics

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
kernel/graphics/sysmon.o \
kernel/graphics/client_app.o \
kernel/core/kernel.o \
cxx/cxx_runtime.o \
cxx/kstd_support.o \
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
.SUFFIXES: .o .c .s .S .cpp

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

.cpp.o:
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS) $(CPPFLAGS)

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
