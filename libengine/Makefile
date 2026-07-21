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
CPPFLAGS:=$(CPPFLAGS) -DLPL_TARGET_KERNEL=1 \
          -Iinclude \
          -I$(LPLPLUGIN_ROOT)/core/include \
          -I$(LPLPLUGIN_ROOT)/math/include \
          -I$(LPLPLUGIN_ROOT)/memory/include \
          -I$(LPLPLUGIN_ROOT)/container/include \
          -I$(LPLPLUGIN_ROOT)/ecs/include \
          -I$(LPLPLUGIN_ROOT)/concurrency/include \
          -I$(LPLPLUGIN_ROOT)/physics/include \
          -I$(LPLPLUGIN_ROOT)/platform/include \
          -I$(LPLPLUGIN_ROOT)/input/include \
          -I$(LPLPLUGIN_ROOT)/net/include \
          -I$(LPLPLUGIN_ROOT)/gpu/include \
          -I$(LPLPLUGIN_ROOT)/image/include \
          -I$(LPLPLUGIN_ROOT)/scene/include \
          -I$(LPLPLUGIN_ROOT)/render/include \
          -I$(LPLPLUGIN_ROOT)/engine/include \
          -I$(LPLPLUGIN_ROOT)/samples/include

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CXXFLAGS:=$(CXXFLAGS) $(ARCH_CXXFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(ARCH_CPPFLAGS)

# Engine objects mirror the LplPlugin source tree under obj/ so no .o ever lands
# in the (submodule / sibling) LplPlugin checkout.
ENGINE_OBJS=$(addprefix obj/,$(ARCH_ENGINE_SRCS:.cpp=.o))

# libengine-local sources (the C-callable facade / smoke entry points).
LOCAL_OBJS=\
obj/local/smoke/p0_smoke.o \
obj/local/smoke/p1_smoke.o \
obj/local/smoke/p1_ecs_smoke.o \
obj/local/smoke/p1_sched_smoke.o \
obj/local/smoke/p1_physics_smoke.o \
obj/local/smoke/p2_hal_smoke.o \
obj/local/smoke/p3_render_smoke.o \
obj/local/smoke/p4_image_smoke.o \
obj/local/smoke/p4_image_present_smoke.o \
obj/local/smoke/p4_scene_smoke.o \
obj/local/smoke/p5_render_smoke.o \
obj/local/smoke/p5_render_present_smoke.o \
obj/local/smoke/p6_smoke.o \
obj/local/smoke/sim_fold_smoke.o \
obj/local/client_app.o \
obj/local/server_app.o

OBJS=$(ENGINE_OBJS) $(LOCAL_OBJS)

BINARIES=libengine.a

.PHONY: all clean install install-headers install-libs

all: $(BINARIES)

libengine.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# Engine sources, pulled from LPLPLUGIN_ROOT.
obj/%.o: $(LPLPLUGIN_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS) $(CPPFLAGS)

# libengine-local sources.
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

-include $(ENGINE_OBJS:.o=.d)
-include $(LOCAL_OBJS:.o=.d)
