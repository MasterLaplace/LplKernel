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
kernel/core/console.o \
kernel/core/splash.o \
kernel/core/smp.o \
kernel/testing/smoke_batch.o \
kernel/testing/smoke_libengine.o \
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

NM?=$(subst -gcc,-nm,$(firstword $(CC)))
OBJDUMP?=$(subst -gcc,-objdump,$(firstword $(CC)))

lpl.kernel: $(OBJS) $(ARCHDIR)/linker.ld $(ENGINE_ARCHIVE)
	$(CC) -T $(ARCHDIR)/linker.ld -o $@ $(CFLAGS) $(LINK_LIST)
	grub-file --is-x86-multiboot lpl.kernel
	@$(MAKE) --no-print-directory check-kernel-end

# The PMM treats every byte past _kernel_end as free RAM. An allocatable
# section placed beyond it (an orphan the linker script forgot to collect,
# typically a C++ COMDAT `.text.*` / `.bss.*`) is therefore handed out as free
# memory while the kernel is still using it, and the corruption only shows up
# much later, somewhere else entirely. Catch it at link time instead.
.PHONY: check-kernel-end
check-kernel-end:
	@end=`$(NM) lpl.kernel | awk '$$3 == "_kernel_end" { print $$1 }'`; \
	if [ -z "$$end" ]; then echo "[check] _kernel_end introuvable" >&2; exit 1; fi; \
	over=`$(OBJDUMP) -h lpl.kernel | awk -v e="$$end" ' \
	    /^[ \t]*[0-9]+ / { name=$$2; size=strtonum("0x" $$3); vma=strtonum("0x" $$4); getline; \
	        if ($$0 ~ /ALLOC/ && size > 0 && vma + size > strtonum("0x" e)) \
	            printf "%s (vma=0x%x size=0x%x)\n", name, vma, size }'`; \
	if [ -n "$$over" ]; then \
	    echo "[check] ERREUR: section allouable au-dela de _kernel_end (0x$$end):" >&2; \
	    echo "$$over" >&2; \
	    echo "[check] ajoutez le motif manquant dans $(ARCHDIR)/linker.ld" >&2; \
	    exit 1; \
	fi; \
	echo "[check] _kernel_end (0x$$end) couvre toutes les sections allouables"

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
