-- /////////////////////////////////////////////////////////////////////////////
-- /// @file xmake.lua
-- /// @brief Native xmake build for LplKernel (i686 freestanding kernel).
-- ///
-- /// A faithful port of the config.sh / headers.sh / build.sh Makefile build,
-- /// offered ALONGSIDE the shell scripts (which stay the zero-dependency path).
-- /// Four tiers, mirroring the libk recipe:
-- ///   libk       (libc/)      -> freestanding C support library
-- ///   libkxx     (libkxx/)    -> freestanding C++ runtime (operator new, kstd)
-- ///   libengine  (LplPlugin)  -> the engine compiled -ffreestanding (Model B)
-- ///   lpl.kernel (kernel/)    -> the kernel image, -lengine -lkxx -lk -lgcc
-- ///
-- /// Usage:
-- ///   xmake f --graphics=y --keyboard=fr   # configure (modes below)
-- ///   xmake                                # build lpl.kernel
-- ///   xmake iso                            # build a bootable GRUB ISO
-- ///   xmake qemu                           # boot the kernel in QEMU (serial)
-- ///
-- /// The cross toolchain (i686-elf-gcc 14) is taken from $CROSS_BIN, else
-- /// ~/opt/cross14/bin, else $PATH. Override with `CROSS_BIN=/path xmake`.
-- /////////////////////////////////////////////////////////////////////////////

set_project("LplKernel")
set_xmakever("2.7.0")

-- Single source of truth for the engine (git submodule, sibling fallback).
local LPLPLUGIN_ROOT = os.getenv("LPLPLUGIN_ROOT")
if not LPLPLUGIN_ROOT or LPLPLUGIN_ROOT == "" then
    if os.isdir(path.join(os.scriptdir(), "LplPlugin/core/include")) then
        LPLPLUGIN_ROOT = path.join(os.scriptdir(), "LplPlugin")
    else
        LPLPLUGIN_ROOT = path.join(os.scriptdir(), "../LplPlugin")
    end
end

-- Optional engine module: if the LplPlugin source tree is not present, the
-- kernel still builds standalone (no libengine, smoke battery stubbed out). This
-- is the "no xmake/LplPlugin → fallback to a plain kernel" path.
local LPLPLUGIN_AVAILABLE = os.isdir(path.join(LPLPLUGIN_ROOT, "core/include"))

-- ---------------------------------------------------------------------------
-- Cross toolchain: i686-elf-gcc/g++/ar. Assembly + link go through the gcc
-- driver (matching the Makefile's .s.o / link rules using $(CC)).
-- ---------------------------------------------------------------------------
toolchain("i686elf")
    set_kind("standalone")
    set_toolset("cc", "i686-elf-gcc")
    set_toolset("cxx", "i686-elf-g++")
    set_toolset("as", "i686-elf-gcc")
    set_toolset("ld", "i686-elf-gcc")
    set_toolset("ar", "i686-elf-ar")
    on_load(function (toolchain)
        local bindir = os.getenv("CROSS_BIN")
        if not bindir or bindir == "" then
            bindir = path.join(os.getenv("HOME") or "", "opt/cross14/bin")
        end
        if os.isdir(bindir) then
            toolchain:set("bindir", bindir)
        end
    end)
toolchain_end()

-- Build modes: `xmake f -m debug` (default, ships the smoke battery) or
-- `xmake f -m release` (production image, smoke battery compiled out). The
-- optimize/symbols level is pinned below to -O2 -g in BOTH modes so the
-- bit-identical determinism signatures never depend on the mode; the mode only
-- toggles whether the diagnostic smoke battery is built in.
add_rules("mode.debug", "mode.release")

set_toolchains("i686elf")
set_languages("gnu11", "gnuxx23")
set_optimize("faster")         -- -O2 parity with the Makefile (-O2 -g)
set_symbols("debug")
add_cflags("-g", {force = true})
add_cxxflags("-g", {force = true})

-- ---------------------------------------------------------------------------
-- Build-mode options (mirror build.sh flags).
-- ---------------------------------------------------------------------------
option("graphics")
    set_default(false)
    set_showmenu(true)
    set_description("Request a VBE linear framebuffer at boot (else text mode)")
option_end()

option("realtime")
    set_default(true)
    set_showmenu(true)
    set_description("Client/realtime build: Free-List PMM (else server/buddy)")
option_end()

option("apic_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("Enable the APIC periodic-mode smoke test")
option_end()

option("keyboard")
    set_default("us")
    set_showmenu(true)
    set_values("us", "fr")
    set_description("Keyboard layout: us=QWERTY, fr=AZERTY")
option_end()

option("smoke")
    set_default(true)
    set_showmenu(true)
    set_description("Compile the libengine P0..P6 + kernel smoke/diagnostic battery into the image")
option_end()

local GRAPHICS_MODE = has_config("graphics") and 1 or 0
-- The smoke battery is built when explicitly requested, never in release mode (a
-- production image), and only when the engine is actually linked in (it calls
-- libengine_* symbols). `--smoke=n`, `-m release`, or a missing LplPlugin each
-- compile it out.
local ENABLE_SMOKE = has_config("smoke") and not is_mode("release") and LPLPLUGIN_AVAILABLE

-- ===========================================================================
-- libk — freestanding C support library (libc/ FREEOBJS, libk variant).
-- ===========================================================================
target("libk")
    set_kind("static")
    set_basename("k")
    add_cflags("-nostdinc", "-ffreestanding", "-Wall", "-Wextra", "-fstack-protector-strong",
               "-std=gnu99", {force = true})
    add_defines("__is_libc", "__is_libk")
    add_sysincludedirs("libc/include", "kernel/include")
    add_files(
        "libc/math/math.c",
        "libc/stdio/printf.c",
        "libc/stdio/putchar.c",
        "libc/stdio/puts.c",
        "libc/stdlib/abort.c",
        "libc/string/memcmp.c",
        "libc/string/memcpy.c",
        "libc/string/memmove.c",
        "libc/string/memset.c",
        "libc/string/strlen.c"
    )
target_end()

-- ===========================================================================
-- libkxx — freestanding C++ runtime support (operator new/delete + Itanium ABI
-- stubs + the kstd fatal sink). The C++ counterpart of libk: it is what makes
-- C++ code linkable into the kernel image at all. Consumers are libengine (and
-- any future kernel C++ TU); it resolves kmalloc/kfree and the halt primitives
-- from libk, so the link order is always -lengine -lkxx -lk.
--
-- No -nostdinc here (unlike libk): the freestanding libstdc++ headers
-- (<cstddef>, <new>, <type_traits> ...) live in the toolchain include dir, and
-- kstd is defined as exactly the subset libstdc++ does NOT provide freestanding.
--
-- Gated on LPLPLUGIN_AVAILABLE like libengine: without the engine the kernel is
-- pure C and this runtime has no consumer. Building it anyway would demand a C++
-- cross toolchain WITH freestanding libstdc++ on the fallback path, which
-- build_lplkernel.yml (gcc 10, all-gcc + all-target-libgcc only) does not have.
-- ===========================================================================
if LPLPLUGIN_AVAILABLE then
target("libkxx")
    set_kind("static")
    set_basename("kxx")
    add_cxxflags("-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                 "-Wall", "-Wextra", {force = true})
    set_languages("gnuxx17")
    add_defines("__is_libkxx")
    add_includedirs("libkxx/include")
    add_sysincludedirs("libc/include")
    add_files("libkxx/src/*.cpp")
target_end()
end -- if LPLPLUGIN_AVAILABLE

-- ===========================================================================
-- libengine — the LplPlugin engine compiled -ffreestanding into a static lib.
-- HARD determinism: SSE math, contraction off, bit-identical to the xmake
-- (Linux) oracle. -mstackrealign because the i386 kernel stack is not yet
-- 16-byte aligned on entry. LPL_TARGET_KERNEL=1 routes lpl/std/* to kstd (libkxx).
-- ===========================================================================
if LPLPLUGIN_AVAILABLE then
target("libengine")
    set_kind("static")
    set_basename("engine")
    add_cxxflags(
        "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
        "-Wall", "-Wextra",
        "-msse2", "-mfpmath=sse", "-ffp-contract=off", "-fno-math-errno", "-mstackrealign",
        {force = true}
    )
    add_defines("LPL_TARGET_KERNEL=1")
    add_sysincludedirs(
        "kernel/include",          -- <kernel/hal/hal.h>
        "libkxx/include",          -- <kstd/vector.hpp> etc. (via lpl/std/*)
        "libc/include"             -- freestanding <stdint.h> etc.
    )
    add_includedirs(
        "libengine/include",
        path.join(LPLPLUGIN_ROOT, "core/include"),
        path.join(LPLPLUGIN_ROOT, "math/include"),
        path.join(LPLPLUGIN_ROOT, "memory/include"),
        path.join(LPLPLUGIN_ROOT, "container/include"),
        path.join(LPLPLUGIN_ROOT, "ecs/include"),
        path.join(LPLPLUGIN_ROOT, "concurrency/include"),
        path.join(LPLPLUGIN_ROOT, "physics/include"),
        path.join(LPLPLUGIN_ROOT, "platform/include"),
        path.join(LPLPLUGIN_ROOT, "input/include"),
        -- net/ is not compiled here (LPL_HAS_NET is left undefined); this is only
        -- for the header-only lpl/net/Endpoint.hpp that EventQueue.hpp needs.
        path.join(LPLPLUGIN_ROOT, "net/include"),
        path.join(LPLPLUGIN_ROOT, "gpu/include"),
        path.join(LPLPLUGIN_ROOT, "image/include"),
        path.join(LPLPLUGIN_ROOT, "scene/include"),
        path.join(LPLPLUGIN_ROOT, "render/include"),
        path.join(LPLPLUGIN_ROOT, "engine/include"),
        path.join(LPLPLUGIN_ROOT, "samples/include")
    )
    -- Engine sources (single source of truth), mirroring ARCH_ENGINE_SRCS.
    add_files(
        path.join(LPLPLUGIN_ROOT, "core/src/Log.cpp"),
        path.join(LPLPLUGIN_ROOT, "math/src/Cordic.cpp"),
        path.join(LPLPLUGIN_ROOT, "math/src/Statistics.cpp"),
        path.join(LPLPLUGIN_ROOT, "math/src/Simd.cpp"),
        path.join(LPLPLUGIN_ROOT, "memory/src/ArenaAllocator.cpp"),
        path.join(LPLPLUGIN_ROOT, "ecs/src/Partition.cpp"),
        path.join(LPLPLUGIN_ROOT, "ecs/src/Registry.cpp"),
        path.join(LPLPLUGIN_ROOT, "ecs/src/SystemScheduler.cpp"),
        path.join(LPLPLUGIN_ROOT, "ecs/src/WorldPartition.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/CollisionDetector.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/CollisionSolver.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/SleepingPolicy.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/AntiTunneling.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/Octree.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/CpuPhysicsBackend.cpp"),
        path.join(LPLPLUGIN_ROOT, "platform/src/kernel/KernelPlatform.cpp"),
        path.join(LPLPLUGIN_ROOT, "input/src/InputManager.cpp"),
        path.join(LPLPLUGIN_ROOT, "image/src/Image.cpp"),
        path.join(LPLPLUGIN_ROOT, "image/src/Painter.cpp"),
        path.join(LPLPLUGIN_ROOT, "image/src/Codec.cpp"),
        path.join(LPLPLUGIN_ROOT, "scene/src/Scene.cpp"),
        path.join(LPLPLUGIN_ROOT, "render/src/Camera.cpp"),
        path.join(LPLPLUGIN_ROOT, "render/src/kernel/KernelDisplayRenderer.cpp"),
        path.join(LPLPLUGIN_ROOT, "engine/src/Config.cpp"),
        path.join(LPLPLUGIN_ROOT, "engine/src/GameLoop.cpp"),
        path.join(LPLPLUGIN_ROOT, "engine/src/systems/MovementSystem.cpp"),
        path.join(LPLPLUGIN_ROOT, "engine/src/systems/PhysicsSystem.cpp"),
        path.join(LPLPLUGIN_ROOT, "engine/src/Engine.cpp")
    )
    -- libengine-local: the kernel client entry (client_app.cpp, which constructs
    -- lpl::engine::Engine) + the P0..P6 and parity-fold smoke/diagnostic entry
    -- points, kept in their own src/smoke/ subtree.
    add_files("libengine/src/*.cpp", "libengine/src/smoke/*.cpp")
target_end()
end -- if LPLPLUGIN_AVAILABLE

-- ===========================================================================
-- lpl.kernel — the kernel image. Custom link to honour the crt ordering
-- (crti, crtbegin, objs, libs, crtend, crtn) + the linker script + multiboot.
-- ===========================================================================
target("lpl-kernel")
    set_kind("binary")
    set_filename("lpl.kernel")
    add_deps("libk")
    if LPLPLUGIN_AVAILABLE then
        add_deps("libengine", "libkxx")
    else
        -- No engine: stub the smoke facade + skip the smoke battery entirely.
        add_defines("LPL_PLUGIN_UNAVAILABLE=1")
    end

    -- C: freestanding, no standard includes (headers come from -I dirs below).
    add_cflags("-nostdinc", "-ffreestanding", "-Wall", "-Wextra", {force = true})
    add_defines("__is_kernel", "MULTIBOOT_VERSION=1")
    -- Assembler `.include "arch/i386/..."` paths are written relative to the
    -- kernel/ subdir (the Makefile assembled from there); point GNU as there.
    add_asflags("-DMULTIBOOT_VERSION=1", "-DGRAPHICS_MODE=" .. GRAPHICS_MODE,
                "-Ikernel", "-Wa,-Ikernel", {force = true})

    if ENABLE_SMOKE then
        add_defines("LPL_KERNEL_ENABLE_SMOKE_TESTS")
    end
    if has_config("realtime") then
        add_defines("LPL_KERNEL_REAL_TIME_MODE")
    end
    if has_config("apic_smoke") then
        add_defines("KERNEL_SMOKE_TEST_ENABLE_APIC_PERIODIC_MODE=1u")
    end
    if get_config("keyboard") == "fr" then
        add_defines("LPL_KERNEL_KEYBOARD_LAYOUT_AZERTY")
    end

    add_includedirs("kernel/include")
    add_sysincludedirs("libc/include", "libengine/include")

    -- All i386 + portable kernel sources (crti.s/crtn.s are partitioned out in
    -- the link step below; the ARM crt files are a different arch and excluded
    -- by globbing arch/i386 only).
    add_files("kernel/arch/i386/**.c")
    add_files("kernel/arch/i386/**.s", "kernel/arch/i386/**.S")
    add_files("kernel/kernel/**.c")

    on_link(function (target)
        import("core.base.option")
        local cc = target:tool("cc")
        local scriptdir = os.scriptdir()
        local linker = path.join(scriptdir, "kernel/arch/i386/linker.ld")

        -- Partition object files: crti / crtn must bracket the rest.
        local crti, crtn, rest = nil, nil, {}
        for _, o in ipairs(target:objectfiles()) do
            local name = path.filename(o)
            if name:startswith("crti.") then crti = o
            elseif name:startswith("crtn.") then crtn = o
            else table.insert(rest, o) end
        end
        assert(crti and crtn, "crti.o/crtn.o not found among kernel objects")

        -- crtbegin/crtend ship with the compiler (global ctor/dtor framing).
        local crtbegin = os.iorunv(cc, {"-print-file-name=crtbegin.o"}):trim()
        local crtend = os.iorunv(cc, {"-print-file-name=crtend.o"}):trim()

        local libengine = target:dep("libengine") and target:dep("libengine"):targetfile() or nil
        local libkxx = target:dep("libkxx") and target:dep("libkxx"):targetfile() or nil
        local libk = target:dep("libk"):targetfile()
        local out = target:targetfile()

        -- $(CC) -T linker.ld -o lpl.kernel <free flags> crti crtbegin OBJS \
        --       -nostdlib [-lengine -lkxx] -lk -lgcc crtend crtn
        -- Link order is libengine -> libkxx -> libk: the engine calls the C++
        -- runtime (operator new / kstd::fatal) in libkxx, which calls kmalloc/
        -- kfree and the halt primitives in libk. Both are omitted entirely when
        -- LplPlugin is unavailable (the kernel is then pure C).
        local argv = {"-T", linker, "-o", out, "-ffreestanding", "-O2", "-g", "-nostdlib", crti, crtbegin}
        table.join2(argv, rest)
        if libengine then table.insert(argv, libengine) end
        if libkxx then table.insert(argv, libkxx) end
        table.join2(argv, {libk, "-lgcc", crtend, crtn})

        os.mkdir(path.directory(out))
        if option.get("verbose") then cprint("${dim}%s %s", cc, table.concat(argv, " ")) end
        os.execv(cc, argv)
        -- Fail loudly if the image is not a valid multiboot kernel.
        os.execv("grub-file", {"--is-x86-multiboot", out})
        cprint("${green}[lpl.kernel]${clear} linked + multiboot OK -> %s", out)
    end)
target_end()

-- ===========================================================================
-- Tasks: ISO image (GRUB) and QEMU boot, using the xmake-built kernel.
-- ===========================================================================
task("iso")
    set_menu({usage = "xmake iso", description = "Build a bootable GRUB rescue ISO"})
    on_run(function ()
        os.exec("xmake build lpl-kernel")
        local kernel = table.unpack(os.files(path.join(os.scriptdir(), "build/**/lpl.kernel")))
        assert(kernel, "lpl.kernel not found — run `xmake` first")
        local iso = path.join(os.scriptdir(), "iso")
        os.tryrm(iso)
        os.mkdir(path.join(iso, "boot/grub"))
        os.cp(kernel, path.join(iso, "boot/lpl.kernel"))
        local cfg, modules
        if has_config("graphics") then
            cfg = "set timeout=0\nset default=0\ninsmod all_video\ninsmod vbe\ninsmod gfxterm\n"
                .. "set gfxmode=1024x768x32,800x600x32,auto\nterminal_output gfxterm\n"
                .. "menuentry \"lpl\" {\n  set gfxpayload=keep\n  multiboot /boot/lpl.kernel\n  boot\n}\n"
            modules = "multiboot all_video gfxterm"
        else
            cfg = "set timeout=0\nset default=0\nmenuentry \"lpl\" {\n  multiboot /boot/lpl.kernel\n  boot\n}\n"
            modules = "multiboot"
        end
        io.writefile(path.join(iso, "boot/grub/grub.cfg"), cfg)
        os.execv("grub-mkrescue", {"-o", path.join(os.scriptdir(), "lpl.iso"), iso,
            "--install-modules=", "--modules=" .. modules, "--fonts=", "--themes=", "--locales="})
        cprint("${green}[iso]${clear} -> %s", path.join(os.scriptdir(), "lpl.iso"))
    end)
task_end()

task("qemu")
    set_menu({usage = "xmake qemu", description = "Boot in QEMU: graphical window + serial console on the tty"})
    on_run(function ()
        -- Window shown (default display) + serial multiplexed with the QEMU
        -- monitor on the controlling tty (-serial mon:stdio). In graphics mode
        -- we boot the GRUB ISO with -vga std so the VBE linear framebuffer
        -- actually exists (the QEMU -kernel multiboot path has none → text mode).
        if has_config("graphics") then
            os.exec("xmake iso")
            os.execv("qemu-system-i386", {"-cdrom", path.join(os.scriptdir(), "lpl.iso"),
                "-m", "256M", "-vga", "std", "-serial", "mon:stdio", "-no-reboot"})
        else
            os.exec("xmake build lpl-kernel")
            local kernel = table.unpack(os.files(path.join(os.scriptdir(), "build/**/lpl.kernel")))
            assert(kernel, "lpl.kernel not found — run `xmake` first")
            os.execv("qemu-system-i386", {"-kernel", kernel,
                "-m", "256M", "-serial", "mon:stdio", "-no-reboot"})
        end
    end)
task_end()

task("debug")
    set_menu({usage = "xmake debug", description = "Build (debug) + boot QEMU halted with a gdb stub (-s -S)"})
    on_run(function ()
        -- Boot QEMU with the gdb stub on :1234, CPU halted (-S) until a debugger
        -- attaches and continues. Serial goes to serial.log so VS Code can run
        -- this as a background preLaunchTask. The kernel is built with -g, so the
        -- artefact under build/ carries full symbols (see launch.json `file`).
        os.exec("xmake build lpl-kernel")
        local kernel = table.unpack(os.files(path.join(os.scriptdir(), "build/**/lpl.kernel")))
        assert(kernel, "lpl.kernel not found — run `xmake` first")
        local serial = path.join(os.scriptdir(), "serial.log")
        local common = {"-m", "256M", "-s", "-S", "-serial", "file:" .. serial, "-no-reboot"}
        cprint("${yellow}[debug]${clear} QEMU halted on tcp::1234 — attach gdb (F5). serial -> %s", serial)
        if has_config("graphics") then
            os.execv("qemu-system-i386", table.join({"-kernel", kernel, "-device", "virtio-gpu-pci"}, common))
        else
            os.execv("qemu-system-i386", table.join({"-kernel", kernel}, common))
        end
    end)
task_end()
