# Makefile for libknowledge — the demon's memory, linked into the kernel.
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

# Roots of the two source trees this archive draws from. LplKnowledge supplies the
# reader; LplPlugin supplies the foundation it is written against (Fixed32, the
# containers, and `lpl::history` — the arithmetic of doubt this module CONSUMES and
# deliberately does not reimplement). Both default to a sibling checkout so a bare
# `make` in this directory still works.
LPLKNOWLEDGE_ROOT?=$(abspath ../../LplKnowledge)
LPLPLUGIN_ROOT?=$(abspath ../../LplPlugin)

# gnu++20 and not 23: this archive is compiled with the same standard libassistant uses,
# and LplKnowledge's own build asks for C++23. The reader uses no feature above 20 —
# checked, and the reason it matters is that a module compiled to one standard here and
# another in its own repository is a module whose two builds are not the same translation.
#
# LPL_HAS_FOUNDATION is what turns lpl/Foundation.hpp from the standalone aliases into the
# real lpl::core and lpl::math. Without it there is no Fixed32, so `knowledge/History.hpp`
# and the P18 gate compile to nothing — Foundation.hpp says as much rather than emulating
# it, and the kernel path always has the foundation by construction.
CXXFLAGS:=$(CXXFLAGS) -std=gnu++20 -ffreestanding -fno-exceptions -fno-rtti \
          -fno-threadsafe-statics -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -DLPL_TARGET_KERNEL=1 -DLPL_HAS_FOUNDATION \
          -Iinclude \
          -I$(LPLKNOWLEDGE_ROOT)/include \
          -I$(LPLKNOWLEDGE_ROOT)/knowledge/include \
          -I$(LPLKNOWLEDGE_ROOT)/corpus/include \
          -I$(LPLPLUGIN_ROOT)/core/include \
          -I$(LPLPLUGIN_ROOT)/math/include \
          -I$(LPLPLUGIN_ROOT)/memory/include \
          -I$(LPLPLUGIN_ROOT)/history/include

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CXXFLAGS:=$(CXXFLAGS) $(ARCH_CXXFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(ARCH_CPPFLAGS)

# Knowledge objects mirror the LplKnowledge source tree under obj/ so no .o ever lands
# in the sibling checkout.
KNOWLEDGE_OBJS=$(addprefix obj/,$(ARCH_KNOWLEDGE_SRCS:.cpp=.o))

# libknowledge-local sources (the C-callable facade / smoke entry points).
LOCAL_OBJS=\
obj/local/smoke/p18_corpus_smoke.o \
obj/local/knowledge_boot.o

OBJS=$(KNOWLEDGE_OBJS) $(LOCAL_OBJS)

BINARIES=libknowledge.a

.PHONY: all clean install install-headers install-libs

all: $(BINARIES)

libknowledge.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# Knowledge sources, pulled from LPLKNOWLEDGE_ROOT.
obj/%.o: $(LPLKNOWLEDGE_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS) $(CPPFLAGS)

# libknowledge-local sources.
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

-include $(KNOWLEDGE_OBJS:.o=.d)
-include $(LOCAL_OBJS:.o=.d)
