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
INCLUDEDIR?=$(PREFIX)/include
LIBDIR?=$(EXEC_PREFIX)/lib

# The freestanding C++ runtime: no exceptions, no RTTI, no guarded statics —
# the kernel ships no unwinder, no typeinfo and no __cxa_guard runtime.
#
# -nostdinc is filtered out (config.sh puts it in CFLAGS for the C side): the
# freestanding libstdc++ headers (<cstddef>, <new>, <type_traits> ...) live in
# the toolchain include dir, which -nostdinc would hide. kstd is defined
# exactly as "the subset the freestanding libstdc++ does NOT provide".
#
# gnu++17 (not gnu++23): this runtime uses no C++20/23 feature, so it builds on
# older cross toolchains (the CI's gcc 10). C++23 stays a libengine-only
# requirement — libengine has its own gcc14/gnu++23 build.
CXXFLAGS:=$(filter-out -nostdinc,$(CFLAGS)) -std=gnu++17 -ffreestanding \
	-fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -D__is_libkxx -Iinclude

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CXXFLAGS:=$(CXXFLAGS) $(ARCH_CXXFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(ARCH_CPPFLAGS)

OBJS=\
$(ARCH_OBJS) \
src/cxx_runtime.o \
src/support.o \

BINARIES=libkxx.a

.PHONY: all clean install install-headers install-libs
.SUFFIXES: .o .cpp

all: $(BINARIES)

libkxx.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

.cpp.o:
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS) $(CPPFLAGS)

clean:
	rm -f $(BINARIES) *.a
	rm -f $(OBJS) *.o */*.o */*/*.o
	rm -f $(OBJS:.o=.d) *.d */*.d */*/*.d

install: install-headers install-libs

install-headers:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp -R --preserve=timestamps include/. $(DESTDIR)$(INCLUDEDIR)/.

install-libs: $(BINARIES)
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp $(BINARIES) $(DESTDIR)$(LIBDIR)

-include $(OBJS:.o=.d)
