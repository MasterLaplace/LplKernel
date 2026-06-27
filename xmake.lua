-- /////////////////////////////////////////////////////////////////////////////
-- /// @file xmake.lua
-- /// @brief Native xmake build for LplKernel (i686 freestanding kernel).
-- ///
-- /// A faithful port of the config.sh / headers.sh / build.sh Makefile build,
-- /// offered ALONGSIDE the shell scripts (which stay the zero-dependency path).
-- /// Three tiers, mirroring the libk recipe:
-- ///   libk       (libc/)      -> freestanding C support library
-- ///   libengine  (LplPlugin)  -> the engine compiled -ffreestanding (Model B)
-- ///   lpl.kernel (kernel/)    -> the kernel image, linked -lengine -lk -lgcc
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

local GRAPHICS_MODE = has_config("graphics") and 1 or 0

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
-- libengine — the LplPlugin engine compiled -ffreestanding into a static lib.
-- HARD determinism: SSE math, contraction off, bit-identical to the xmake
-- (Linux) oracle. -mstackrealign because the i386 kernel stack is not yet
-- 16-byte aligned on entry. LPL_TARGET_KERNEL=1 routes lpl/std/* to kernel_std.
-- ===========================================================================
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
        path.join(LPLPLUGIN_ROOT, "render/include"),
        path.join(LPLPLUGIN_ROOT, "engine/include")
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
        path.join(LPLPLUGIN_ROOT, "physics/src/CollisionDetector.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/CollisionSolver.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/SleepingPolicy.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/AntiTunneling.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/Octree.cpp"),
        path.join(LPLPLUGIN_ROOT, "physics/src/CpuPhysicsBackend.cpp"),
        path.join(LPLPLUGIN_ROOT, "platform/src/kernel/KernelPlatform.cpp"),
        path.join(LPLPLUGIN_ROOT, "render/src/Camera.cpp"),
        path.join(LPLPLUGIN_ROOT, "render/src/kernel/KernelDisplayRenderer.cpp"),
        path.join(LPLPLUGIN_ROOT, "engine/src/Config.cpp"),
        path.join(LPLPLUGIN_ROOT, "engine/src/GameLoop.cpp")
    )
    -- libengine-local C-facade / smoke entry points.
    add_files("libengine/src/*.cpp")
target_end()

-- ===========================================================================
-- lpl.kernel — the kernel image. Custom link to honour the crt ordering
-- (crti, crtbegin, objs, libs, crtend, crtn) + the linker script + multiboot.
-- ===========================================================================
target("lpl-kernel")
    set_kind("binary")
    set_filename("lpl.kernel")
    add_deps("libengine", "libk")

    -- C: freestanding, no standard includes (headers come from -I dirs below).
    add_cflags("-nostdinc", "-ffreestanding", "-Wall", "-Wextra", {force = true})
    -- C++ (cxx_runtime / kstd_support): keep stdinc so the freestanding
    -- libstdc++ headers (<cstddef> ...) resolve; no exceptions/RTTI/guards.
    add_cxxflags(
        "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
        "-Wall", "-Wextra", {force = true}
    )
    add_defines("__is_kernel", "MULTIBOOT_VERSION=1")
    -- Assembler `.include "arch/i386/..."` paths are written relative to the
    -- kernel/ subdir (the Makefile assembled from there); point GNU as there.
    add_asflags("-DMULTIBOOT_VERSION=1", "-DGRAPHICS_MODE=" .. GRAPHICS_MODE,
                "-Ikernel", "-Wa,-Ikernel", {force = true})

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
    add_files("kernel/cxx/**.cpp")

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

        local libengine = target:dep("libengine"):targetfile()
        local libk = target:dep("libk"):targetfile()
        local out = target:targetfile()

        -- $(CC) -T linker.ld -o lpl.kernel <free flags> crti crtbegin OBJS \
        --       -nostdlib -lengine -lk -lgcc crtend crtn
        local argv = {"-T", linker, "-o", out, "-ffreestanding", "-O2", "-g", "-nostdlib", crti, crtbegin}
        table.join2(argv, rest)
        table.join2(argv, {libengine, libk, "-lgcc", crtend, crtn})

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
