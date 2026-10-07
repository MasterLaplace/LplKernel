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
# engine's battery is left out and LPL_PLUGIN_UNAVAILABLE is set. The kernel's own
# tests still run: they call nothing in the engine.
#
# The mind and the memory ride on the same seam one rung higher: -lknowledge and
# -lassistant come FIRST, because both call into libengine — the mind for the foundation
# (Fixed32, CORDIC, the arena), the memory for `lpl::history`, the arithmetic of doubt it
# consumes rather than reimplements — and a static archive only satisfies references that
# are already outstanding when the linker reaches it.
#
# -lknowledge before -lassistant is the order to keep: nothing in the memory calls the mind
# today, but the reverse is where this is going (a demon that recalls what it knows), and an
# order chosen once is one fewer thing to rediscover at a link error.
ENABLE_LIBENGINE?=1
ENABLE_LIBASSISTANT?=0
ENABLE_LIBKNOWLEDGE?=0
ifeq ($(ENABLE_LIBENGINE),1)
KNOWLEDGE_LIB:=
ASSISTANT_LIB:=
ifeq ($(ENABLE_LIBKNOWLEDGE),1)
KNOWLEDGE_LIB:=-lknowledge
KNOWLEDGE_ARCHIVE:=$(wildcard $(DESTDIR)/usr/lib/libknowledge.a)
else
CPPFLAGS:=$(CPPFLAGS) -DLPL_KNOWLEDGE_UNAVAILABLE=1
endif
ifeq ($(ENABLE_LIBASSISTANT),1)
ASSISTANT_LIB:=-lassistant
ASSISTANT_ARCHIVE:=$(wildcard $(DESTDIR)/usr/lib/libassistant.a)
else
CPPFLAGS:=$(CPPFLAGS) -DLPL_ASSISTANT_UNAVAILABLE=1
endif
LIBS:=$(LIBS) -nostdlib $(KNOWLEDGE_LIB) $(ASSISTANT_LIB) -lengine -lkxx -lk -lgcc
# Relink the kernel whenever the engine archive changes. -lengine is resolved
# from the sysroot, so a rebuilt libengine.a (e.g. after an LplPlugin header
# edit) would otherwise leave a stale engine baked into lpl.kernel. wildcard
# keeps standalone `make` (no DESTDIR/sysroot) from gaining a bogus prereq.
ENGINE_ARCHIVE:=$(wildcard $(DESTDIR)/usr/lib/libengine.a)
else
LIBS:=$(LIBS) -nostdlib -lk -lgcc
CPPFLAGS:=$(CPPFLAGS) -DLPL_PLUGIN_UNAVAILABLE=1 -DLPL_ASSISTANT_UNAVAILABLE=1 -DLPL_KNOWLEDGE_UNAVAILABLE=1
endif

# The tests (kernel/testing/test.h) and the engine's battery. ON by default for the
# script/Makefile build (debug). Pass SMOKE_TESTS=0 (or xmake "release") to compile
# every test out of a production image. The memory poisoning comes with them: the
# tests check it, and only a build that poisons can fail those checks.
#
# A test is any .c under tests/ or $(ARCHDIR)/tests/, so adding one edits no list.
# Sorted, because the order of $(wildcard) depends on the version of make; the
# runner orders the tests itself, but the link should not change with the host.
SMOKE_TESTS?=1
KERNEL_TEST_OBJS:=
ifeq ($(SMOKE_TESTS),1)
CPPFLAGS:=$(CPPFLAGS) -DLPL_KERNEL_ENABLE_SMOKE_TESTS -DLPL_KERNEL_DEBUG_POISON
KERNEL_TEST_OBJS:=kernel/testing/test.o \
    $(patsubst %.c,%.o,$(sort $(wildcard tests/*/*.c arch/$(HOSTARCH)/tests/*/*.c)))
endif

# What the image says about itself at boot: the profile and the mode it was built for, and
# the commit, stamped into identity.o alone. The stamp file changes only when they do, and
# identity.o depends on it: make cannot see a -D change.
ifeq ($(SATELLITE_MODE),1)
KERNEL_PROFILE:=satellite
else ifeq ($(GRAPHICS_MODE),1)
KERNEL_PROFILE:=client
else
KERNEL_PROFILE:=server
endif
ifeq ($(SMOKE_TESTS),1)
KERNEL_BUILD:=$(KERNEL_PROFILE).debug
else
KERNEL_BUILD:=$(KERNEL_PROFILE).release
endif
KERNEL_COMMIT:=$(shell git rev-parse --short=7 HEAD 2>/dev/null || echo unknown)$(shell git diff --quiet HEAD -- 2>/dev/null || echo -dirty)
IDENTITY_STAMP:=kernel/core/identity.stamp
$(shell echo '$(KERNEL_BUILD) $(KERNEL_COMMIT)' | cmp -s - $(IDENTITY_STAMP) || echo '$(KERNEL_BUILD) $(KERNEL_COMMIT)' > $(IDENTITY_STAMP))
kernel/core/identity.o: $(IDENTITY_STAMP)
kernel/core/identity.o: CPPFLAGS+=-DKERNEL_BUILD='"$(KERNEL_BUILD)"' -DKERNEL_COMMIT='"$(KERNEL_COMMIT)"'

# Interactive console (commands read from the keyboard or COM1). A development
# surface, and the one thing an immutable API-only node is defined by not having:
# no shell means no arbitrary command, and no configuration drift between two
# boots of the same image. ON for the script/Makefile build, compiled out by
# `xmake -m release`.
#
# Deliberately NOT tied to SMOKE_TESTS, unlike everything else diagnostic here: a
# release image without the engine has no test and no world, and the console is the
# only payload it has left.
KERNEL_CONSOLE?=1
ifeq ($(KERNEL_CONSOLE),1)
CPPFLAGS:=$(CPPFLAGS) -DLPL_KERNEL_ENABLE_CONSOLE
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
kernel/memory/backpressure.o \
kernel/memory/ring_buffer.o \
kernel/memory/section_protection.o \
kernel/memory/tlsf.o \
kernel/core/console.o \
kernel/core/reconciler.o \
kernel/core/splash.o \
kernel/core/smp.o \
kernel/core/identity.o \
kernel/ai/tensor_arena.o \
kernel/ai/model_slot.o \
kernel/ai/inference_budget.o \
kernel/dialogue/dialogue_channel.o \
kernel/power/processor_sleep.o \
kernel/power/tickless.o \
kernel/power/frequency_scaling.o \
kernel/power/wakeup_accounting.o \
kernel/satellite/satellite_app.o \
kernel/testing/smoke_libengine.o \
kernel/diag/sysmon.o \
kernel/diag/telemetry.o \
kernel/core/kernel.o \
kernel/memory/helpers/pmm_helper.o \
kernel/memory/helpers/heap_helper.o \
kernel/memory/helpers/core_allocators_helper.o \
kernel/memory/helpers/section_protection_helper.o \
$(KERNEL_TEST_OBJS) \

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

# make sees the sources an object is built from, not the flags it is built with: a new -D
# left every object it did not touch as it was, and the image mixed the two. Each object
# depends on a stamp of the flags, rewritten only when they change. A rule writes it, not
# the parse: headers.sh installs the headers without the profile's variables, and a stamp
# written on every parse would flip between the two and rebuild everything every time.
FLAGS_STAMP:=kernel/core/flags.stamp
$(FLAGS_STAMP): FORCE
	@echo '$(CFLAGS) $(CPPFLAGS) $(ASFLAGS)' | cmp -s - $@ || echo '$(CFLAGS) $(CPPFLAGS) $(ASFLAGS)' > $@
$(KERNEL_OBJS) $(ARCHDIR)/boot/crti.o $(ARCHDIR)/boot/crtn.o: $(FLAGS_STAMP)

.PHONY: FORCE
FORCE:

.PHONY: all clean install install-headers install-kernel
.SUFFIXES: .o .c .s .S

all: lpl.kernel

# The COMPILER's name, not the first word of $(CC): $(CC) may carry a wrapper in front
# of it — ccache, distcc, sccache — and firstword then hands back the wrapper. Deriving
# `nm` from that produced `ccache lpl.kernel`, which ccache reports as "could not find
# compiler lpl.kernel" from inside a post-link check, three steps away from the cause.
# Filtering for the word that ends in -gcc names the compiler however many words precede it.
CC_PROGRAM:=$(lastword $(filter %-gcc %gcc,$(CC)))
NM?=$(subst -gcc,-nm,$(CC_PROGRAM))
OBJDUMP?=$(subst -gcc,-objdump,$(CC_PROGRAM))

lpl.kernel: $(OBJS) $(ARCHDIR)/linker.ld $(ENGINE_ARCHIVE) $(ASSISTANT_ARCHIVE) $(KNOWLEDGE_ARCHIVE)
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
	rm -f $(OBJS) *.o */*.o */*/*.o */*/*/*.o */*/*/*/*.o
	rm -f $(OBJS:.o=.d) *.d */*.d */*/*.d */*/*/*.d */*/*/*/*.d
	rm -f $(IDENTITY_STAMP) $(FLAGS_STAMP)

debug: CFLAGS+=-DDEBUG -g3 -O0
debug: all

# What this configuration compiles, for tools that preprocess the kernel the way the build does
# (tools/unbuilt-branches.sh): the compiler, the flags of C and of preprocessed assembly, one
# source per object, and the objects no source builds (crtbegin.o and crtend.o come from the
# compiler). The flags are read here, so they cannot drift from the build.
OBJECT_SOURCES=$(foreach object,$(OBJS),$(firstword $(wildcard $(object:.o=.c) $(object:.o=.S) $(object:.o=.s))))
OBJECTS_WITHOUT_SOURCE=$(filter-out $(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o,\
    $(foreach object,$(OBJS),$(if $(wildcard $(object:.o=.c) $(object:.o=.S) $(object:.o=.s)),,$(object))))
.PHONY: print-compile-commands
print-compile-commands:
	@echo 'cc: $(CC)'
	@echo 'c-flags: -std=gnu11 $(CFLAGS) $(CPPFLAGS)'
	@echo 'asm-flags: $(CFLAGS) $(CPPFLAGS) $(ASFLAGS)'
	@for source in $(OBJECT_SOURCES); do echo "source: $$source"; done
	@for object in $(OBJECTS_WITHOUT_SOURCE); do echo "no-source: $$object"; done

install: install-headers install-kernel

install-headers:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp -R --preserve=timestamps include/. $(DESTDIR)$(INCLUDEDIR)/.

install-kernel: lpl.kernel
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp lpl.kernel $(DESTDIR)$(BOOTDIR)

-include $(OBJS:.o=.d)
