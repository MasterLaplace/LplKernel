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
          -I$(LPLPLUGIN_ROOT)/memory/include \
          -I$(LPLPLUGIN_ROOT)/testing/include

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CXXFLAGS:=$(CXXFLAGS) $(ARCH_CXXFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(ARCH_CPPFLAGS)

# Mind objects mirror the LplAssistant source tree under obj/ so no .o ever lands
# in the sibling checkout.
MIND_OBJS=$(addprefix obj/,$(ARCH_ASSISTANT_SRCS:.cpp=.o))

# libassistant-local sources: the C-callable facade the kernel calls.
LOCAL_OBJS=\
obj/local/assistant_boot.o \
obj/local/identity.o

OBJS=$(MIND_OBJS) $(LOCAL_OBJS)

BINARIES=libassistant.a

# The mind's tests, for debug images: the LPL_TEST of LplAssistant's tests/<module>/ for every
# module compiled above, and those of tests/ here for what only this kernel provides. The kernel
# links the whole archive, because nothing names a test; their runner is in libenginetests.a.
ASSISTANT_MODULES=infer mind satellite
SMOKE_TESTS?=1
ASSISTANT_TEST_SRCS:=$(sort $(patsubst $(LPLASSISTANT_ROOT)/%,%,$(foreach module,$(ASSISTANT_MODULES),\
    $(wildcard $(LPLASSISTANT_ROOT)/tests/$(module)/*.cpp))))
ASSISTANT_TEST_OBJS:=$(addprefix obj/,$(ASSISTANT_TEST_SRCS:.cpp=.o)) \
    $(patsubst %.cpp,obj/local/%.o,$(sort $(wildcard tests/*.cpp)))
ifeq ($(SMOKE_TESTS),1)
BINARIES+=libassistanttests.a
endif

.PHONY: all clean install install-headers install-libs

all: $(BINARIES)

# Each archive is written anew: `ar r` never removes a member, and the kernel links the tests'
# archive whole, so a test deleted or renamed would still run. The list of tests is stamped,
# because deleting one changes no prerequisite make can see.
libassistant.a: $(OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS)

ASSISTANT_TEST_STAMP:=obj/local/tests.stamp
$(shell mkdir -p obj/local; echo '$(ASSISTANT_TEST_OBJS)' | cmp -s - $(ASSISTANT_TEST_STAMP) || echo '$(ASSISTANT_TEST_OBJS)' > $(ASSISTANT_TEST_STAMP))

libassistanttests.a: $(ASSISTANT_TEST_OBJS) $(ASSISTANT_TEST_STAMP)
	rm -f $@
	$(AR) rcs $@ $(ASSISTANT_TEST_OBJS)

# Mind sources, pulled from LPLASSISTANT_ROOT.
obj/%.o: $(LPLASSISTANT_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS) $(CPPFLAGS)

# libassistant-local sources.
# The LplAssistant commit compiled in, stamped into identity.o alone. The stamp file changes
# only when the commit does, and identity.o depends on it: make cannot see a -D change.
LPLASSISTANT_COMMIT:=$(shell git -C $(LPLASSISTANT_ROOT) rev-parse --short=7 HEAD 2>/dev/null || echo unknown)$(shell git -C $(LPLASSISTANT_ROOT) diff --quiet HEAD -- 2>/dev/null || echo -dirty)
IDENTITY_STAMP:=obj/local/identity.stamp
$(shell mkdir -p obj/local; echo '$(LPLASSISTANT_COMMIT)' | cmp -s - $(IDENTITY_STAMP) || echo '$(LPLASSISTANT_COMMIT)' > $(IDENTITY_STAMP))
obj/local/identity.o: $(IDENTITY_STAMP)
obj/local/identity.o: CPPFLAGS+=-DLPLASSISTANT_COMMIT='"$(LPLASSISTANT_COMMIT)"'

obj/local/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS) $(CPPFLAGS)

obj/local/tests/%.o: tests/%.cpp
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
-include $(ASSISTANT_TEST_OBJS:.o=.d)
