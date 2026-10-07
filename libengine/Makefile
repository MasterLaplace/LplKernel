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

# Root of the single-source-of-truth LplPlugin tree (exported by config.sh;
# defaults to a sibling checkout for standalone `make` invocations).
LPLPLUGIN_ROOT?=$(abspath ../../LplPlugin)

# Freestanding C++23, no exceptions/RTTI/threadsafe-statics (the kernel provides
# no unwinder, no typeinfo, no guard-variable runtime). LPL_TARGET_KERNEL=1
# routes every lpl/std/* umbrella to kstd instead of hosted libstdc++.
CXXFLAGS:=$(CXXFLAGS) -std=gnu++23 -ffreestanding -fno-exceptions -fno-rtti \
          -fno-threadsafe-statics -Wall -Wextra
# The LplPlugin modules the kernel compiles against: their headers, and their engine tests.
# net/ is here for the header-only lpl/net/Endpoint.hpp alone, which EventQueue.hpp needs.
ENGINE_MODULES=core testing math memory container ecs concurrency physics platform input net gpu image \
    scene render engine procgen codec rosetta history ai ecology pack samples
CPPFLAGS:=$(CPPFLAGS) -DLPL_TARGET_KERNEL=1 -Iinclude $(foreach module,$(ENGINE_MODULES),-I$(LPLPLUGIN_ROOT)/$(module)/include)

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CXXFLAGS:=$(CXXFLAGS) $(ARCH_CXXFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(ARCH_CPPFLAGS)

# Engine objects mirror the LplPlugin source tree under obj/ so no .o ever lands
# in the sibling LplPlugin checkout.
ENGINE_OBJS=$(addprefix obj/,$(ARCH_ENGINE_SRCS:.cpp=.o))

# libengine-local sources: the C-callable facade the kernel calls.
LOCAL_OBJS=\
obj/local/client_app.o \
obj/local/server_app.o \
obj/local/identity.o

OBJS=$(ENGINE_OBJS) $(LOCAL_OBJS)

BINARIES=libengine.a

# The engine's tests, for debug images: the LPL_TEST of LplPlugin's tests/<module>/ for every
# module above, those of tests/<module>/kernel/ for its sources only the kernel compiles, those
# of tests/ here for what only this kernel provides, their runner, and the seam the kernel's
# runner calls. The kernel links the whole archive, because nothing names a test.
SMOKE_TESTS?=1
ENGINE_TEST_SRCS:=testing/src/Runner.cpp $(sort $(patsubst $(LPLPLUGIN_ROOT)/%,%,$(foreach module,$(ENGINE_MODULES),\
    $(wildcard $(LPLPLUGIN_ROOT)/tests/$(module)/*.cpp $(LPLPLUGIN_ROOT)/tests/$(module)/kernel/*.cpp))))
ENGINE_TEST_OBJS:=$(addprefix obj/,$(ENGINE_TEST_SRCS:.cpp=.o)) obj/local/testing.o \
    $(patsubst %.cpp,obj/local/%.o,$(sort $(wildcard tests/*.cpp)))
ifeq ($(SMOKE_TESTS),1)
BINARIES+=libenginetests.a
endif

.PHONY: all clean install install-headers install-libs

all: $(BINARIES)

# Each archive is written anew: `ar r` never removes a member, and the kernel links the tests'
# archive whole, so a test deleted or renamed would still run. The list of tests is stamped,
# because deleting one changes no prerequisite make can see.
libengine.a: $(OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS)

ENGINE_TEST_STAMP:=obj/local/tests.stamp
$(shell mkdir -p obj/local; echo '$(ENGINE_TEST_OBJS)' | cmp -s - $(ENGINE_TEST_STAMP) || echo '$(ENGINE_TEST_OBJS)' > $(ENGINE_TEST_STAMP))

libenginetests.a: $(ENGINE_TEST_OBJS) $(ENGINE_TEST_STAMP)
	rm -f $@
	$(AR) rcs $@ $(ENGINE_TEST_OBJS)

# Engine sources, pulled from LPLPLUGIN_ROOT.
obj/%.o: $(LPLPLUGIN_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS) $(CPPFLAGS)

# The LplPlugin commit compiled in, stamped into identity.o alone. The stamp file changes
# only when the commit does, and identity.o depends on it: make cannot see a -D change.
LPLPLUGIN_COMMIT:=$(shell git -C $(LPLPLUGIN_ROOT) rev-parse --short=7 HEAD 2>/dev/null || echo unknown)$(shell git -C $(LPLPLUGIN_ROOT) diff --quiet HEAD -- 2>/dev/null || echo -dirty)
IDENTITY_STAMP:=obj/local/identity.stamp
$(shell mkdir -p obj/local; echo '$(LPLPLUGIN_COMMIT)' | cmp -s - $(IDENTITY_STAMP) || echo '$(LPLPLUGIN_COMMIT)' > $(IDENTITY_STAMP))
obj/local/identity.o: $(IDENTITY_STAMP)
obj/local/identity.o: CPPFLAGS+=-DLPLPLUGIN_COMMIT='"$(LPLPLUGIN_COMMIT)"'

# libengine-local sources.
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

-include $(ENGINE_OBJS:.o=.d)
-include $(LOCAL_OBJS:.o=.d)
-include $(ENGINE_TEST_OBJS:.o=.d)
