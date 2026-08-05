# Makefile for libassistant — the demon's mind, linked into the kernel.
#
# Mirrors libengine/Makefile: an out-of-tree source list, a sysroot install, and
# the archive as an explicit prerequisite of the kernel link so a stale library can
# never be frozen into the image. That last point is not theoretical — it cost
# hours once already, when only libengine.a had changed and the kernel did not
# relink.

DEFAULT_HOST!=../default-host.sh
HOST?=DEFAULT_HOST
HOSTARCH!=../target-triplet-to-arch.sh $(HOST)

CXXFLAGS?=-O2 -g
CPPFLAGS?=
LIBS?=

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
INCLUDEDIR?=$(PREFIX)/include
LIBDIR?=$(EXEC_PREFIX)/lib

# Roots of the two source trees this archive draws from. LplAssistant supplies the
# forward pass; LplPlugin supplies the foundation it is written against (Fixed32,
# CORDIC, the one arena implementation). Both default to a sibling checkout so a
# bare `make` in this directory still works.
LPLASSISTANT_ROOT?=$(abspath ../../LplAssistant)
LPLPLUGIN_ROOT?=$(abspath ../../LplPlugin)

# gnu++20 and not 23: LplAssistant sets C++20 as its own standard, and a module
# compiled to a different standard here than in its own repository is a module
# whose two builds are not the same translation.
#
# LPL_HAS_FOUNDATION is what turns lpl/Foundation.hpp from the standalone aliases
# into the real lpl::core and lpl::math — without it the determinism contract is not
# even expressible, and Foundation.hpp says as much rather than emulating Fixed32.
CXXFLAGS:=$(CXXFLAGS) -std=gnu++20 -ffreestanding -fno-exceptions -fno-rtti \
          -fno-threadsafe-statics -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -DLPL_TARGET_KERNEL=1 -DLPL_HAS_FOUNDATION \
          -Iinclude \
          -I$(LPLASSISTANT_ROOT)/include \
          -I$(LPLASSISTANT_ROOT)/infer/include \
          -I$(LPLASSISTANT_ROOT)/satellite/include \
          -I$(LPLASSISTANT_ROOT)/mind/include \
          -I$(LPLPLUGIN_ROOT)/agent/include \
          -I$(LPLPLUGIN_ROOT)/core/include \
          -I$(LPLPLUGIN_ROOT)/math/include \
          -I$(LPLPLUGIN_ROOT)/memory/include

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CXXFLAGS:=$(CXXFLAGS) $(ARCH_CXXFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(ARCH_CPPFLAGS)

# Mind objects mirror the LplAssistant source tree under obj/ so no .o ever lands
# in the sibling checkout.
MIND_OBJS=$(addprefix obj/,$(ARCH_ASSISTANT_SRCS:.cpp=.o))

# libassistant-local sources (the C-callable facade / smoke entry points).
LOCAL_OBJS=\
obj/local/smoke/p14_mind_smoke.o \
obj/local/smoke/p15_satellite_smoke.o \
obj/local/smoke/p16_agency_smoke.o \
obj/local/smoke/p17_reasoning_smoke.o \
obj/local/assistant_boot.o

OBJS=$(MIND_OBJS) $(LOCAL_OBJS)

BINARIES=libassistant.a

.PHONY: all clean install install-headers install-libs

all: $(BINARIES)

libassistant.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# Mind sources, pulled from LPLASSISTANT_ROOT.
obj/%.o: $(LPLASSISTANT_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS) $(CPPFLAGS)

# libassistant-local sources.
obj/local/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS) $(CPPFLAGS)

clean:
	rm -f $(BINARIES) *.a
	rm -rf obj

install: install-headers install-libs

install-headers:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp -R --preserve=timestamps include/. $(DESTDIR)$(INCLUDEDIR)/.

install-libs: $(BINARIES)
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp $(BINARIES) $(DESTDIR)$(LIBDIR)

-include $(MIND_OBJS:.o=.d)
-include $(LOCAL_OBJS:.o=.d)
