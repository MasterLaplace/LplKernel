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

-- Single source of truth for the mind. A sibling checkout, not a submodule —
-- decision 1 of docs/ARCHITECTURE_cible.md, settled the way libassistant's
-- make.config already assumed.
local LPLASSISTANT_ROOT = os.getenv("LPLASSISTANT_ROOT")
if not LPLASSISTANT_ROOT or LPLASSISTANT_ROOT == "" then
    LPLASSISTANT_ROOT = path.join(os.scriptdir(), "../LplAssistant")
end

-- Optional mind module, and optional in a stronger sense than the engine: it is
-- written against the engine's foundation (Fixed32, CORDIC, the one arena), so it
-- can never be present when LplPlugin is not.
local LPLASSISTANT_AVAILABLE =
    LPLPLUGIN_AVAILABLE and os.isdir(path.join(LPLASSISTANT_ROOT, "infer/include"))

-- Single source of truth for the memory. A sibling checkout, like the mind and for the
-- same reason: it is a repository in its own right, it builds and is tested on its own,
-- and a submodule would tie its history to this one's.
local LPLKNOWLEDGE_ROOT = os.getenv("LPLKNOWLEDGE_ROOT")
if not LPLKNOWLEDGE_ROOT or LPLKNOWLEDGE_ROOT == "" then
    LPLKNOWLEDGE_ROOT = path.join(os.scriptdir(), "../LplKnowledge")
end

-- Optional memory module, optional in the same stronger sense as the mind: it reads a
-- corpus into `lpl::history`, which lives in libengine, so it can never be present when
-- LplPlugin is not.
local LPLKNOWLEDGE_AVAILABLE =
    LPLPLUGIN_AVAILABLE and os.isdir(path.join(LPLKNOWLEDGE_ROOT, "knowledge/include"))

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
        path.join(LPLPLUGIN_ROOT, "procgen/include"),
        path.join(LPLPLUGIN_ROOT, "codec/include"),
        path.join(LPLPLUGIN_ROOT, "rosetta/include"),
        path.join(LPLPLUGIN_ROOT, "history/include"),
        path.join(LPLPLUGIN_ROOT, "ai/include"),
        path.join(LPLPLUGIN_ROOT, "ecology/include"),
        path.join(LPLPLUGIN_ROOT, "pack/include"),
        path.join(LPLPLUGIN_ROOT, "samples/include")
    )
    -- Engine sources (single source of truth), mirroring ARCH_ENGINE_SRCS.
    add_files(
        path.join(LPLPLUGIN_ROOT, "core/src/Log.cpp"),
        path.join(LPLPLUGIN_ROOT, "math/src/Cordic.cpp"),
        path.join(LPLPLUGIN_ROOT, "math/src/StateHash.cpp"),
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
        -- procgen/: authoritative Fixed32 world generation (see the rationale in
        -- libengine/arch/i386/make.config). Kept in lock-step with that list.
        path.join(LPLPLUGIN_ROOT, "procgen/src/Heightfield.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Erosion.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Hydrology.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Biome.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/WaveFunctionCollapse.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Dungeon.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/WorldBuilder.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Voronoi.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Aggregation.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/LSystem.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Settlement.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Extrusion.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Botany.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/GaloisField.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/XorKernel.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/BitMatrix.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/GaussJordan.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/FourRussians.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/Prng.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/Fountain.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/Peeling.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/Erasure.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/ReedSolomon.cpp"),
        path.join(LPLPLUGIN_ROOT, "codec/src/Parity.cpp"),
        path.join(LPLPLUGIN_ROOT, "rosetta/src/MinimalIsa.cpp"),
        path.join(LPLPLUGIN_ROOT, "rosetta/src/Interpreter.cpp"),
        path.join(LPLPLUGIN_ROOT, "rosetta/src/SelfDescribing.cpp"),
        path.join(LPLPLUGIN_ROOT, "rosetta/src/Bootstrap.cpp"),
        path.join(LPLPLUGIN_ROOT, "rosetta/src/Engraving.cpp"),
        path.join(LPLPLUGIN_ROOT, "rosetta/src/Parity.cpp"),
        path.join(LPLPLUGIN_ROOT, "history/src/Fact.cpp"),
        path.join(LPLPLUGIN_ROOT, "history/src/Timeline.cpp"),
        path.join(LPLPLUGIN_ROOT, "history/src/PossibleWorld.cpp"),
        path.join(LPLPLUGIN_ROOT, "history/src/Chronicle.cpp"),
        path.join(LPLPLUGIN_ROOT, "history/src/Divergence.cpp"),
        path.join(LPLPLUGIN_ROOT, "history/src/HistorySystem.cpp"),
        path.join(LPLPLUGIN_ROOT, "history/src/Parity.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Chunking.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/QualityGate.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Routing.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/WorldRecipe.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Climate.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/ShapeGrammar.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Liminal.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/CaveSystem.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/HiGen.cpp"),
        path.join(LPLPLUGIN_ROOT, "procgen/src/Streaming.cpp"),

        -- ai/: authoritative agent behaviour. A creature deciding where to go
        -- moves an entity, so it is simulation state like any other.
        path.join(LPLPLUGIN_ROOT, "ai/src/StigmergyField.cpp"),
        path.join(LPLPLUGIN_ROOT, "ai/src/AiMap.cpp"),
        path.join(LPLPLUGIN_ROOT, "ai/src/AbstractWorld.cpp"),
        path.join(LPLPLUGIN_ROOT, "ai/src/Swarm.cpp"),
        path.join(LPLPLUGIN_ROOT, "ai/src/Social.cpp"),
        path.join(LPLPLUGIN_ROOT, "ai/src/SpringBody.cpp"),

        -- ecology/: populations over time. Slower tick, same contract.
        path.join(LPLPLUGIN_ROOT, "ecology/src/Populations.cpp"),
        path.join(LPLPLUGIN_ROOT, "ecology/src/Genome.cpp"),
        path.join(LPLPLUGIN_ROOT, "ecology/src/Society.cpp"),
        path.join(LPLPLUGIN_ROOT, "ecology/src/LivingRecipe.cpp"),
        -- pack/: the freestanding reader for baked game packages (.lplpak).
        path.join(LPLPLUGIN_ROOT, "pack/src/GamePack.cpp"),
        path.join(LPLPLUGIN_ROOT, "pack/src/EccSection.cpp"),
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
        path.join(LPLPLUGIN_ROOT, "engine/src/systems/CreatureSystems.cpp"),
        path.join(LPLPLUGIN_ROOT, "engine/src/systems/HeightfieldCollisionSystem.cpp"),
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
-- libassistant — the LplAssistant forward pass compiled -ffreestanding.
-- Same determinism flags as libengine, because anything linked into the kernel
-- obeys one arithmetic contract and not one per library. gnu++20 rather than 23:
-- LplAssistant sets C++20 as its own standard, and a module compiled to a
-- different standard here than in its own repository is a module whose two builds
-- are not the same translation.
--
-- The source list is EXPLICIT and must stay in lock-step with
-- libassistant/arch/i386/make.config. A glob would pull infer/src/Types.cpp in,
-- which carries a std::string and belongs to the hosted half.
-- ===========================================================================
if LPLASSISTANT_AVAILABLE then
target("libassistant")
    set_kind("static")
    set_basename("assistant")
    set_languages("gnuxx20")
    add_cxxflags(
        "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
        "-Wall", "-Wextra",
        "-msse2", "-mfpmath=sse", "-ffp-contract=off", "-fno-math-errno", "-mstackrealign",
        {force = true}
    )
    add_defines("LPL_TARGET_KERNEL=1", "LPL_HAS_FOUNDATION")
    add_sysincludedirs(
        "kernel/include",          -- <kernel/ai/tensor_arena.h>, <kernel/dialogue/...>
        "libkxx/include",
        "libc/include"
    )
    add_includedirs(
        "libassistant/include",
        path.join(LPLASSISTANT_ROOT, "include"),
        path.join(LPLASSISTANT_ROOT, "infer/include"),
        path.join(LPLASSISTANT_ROOT, "satellite/include"),
        path.join(LPLASSISTANT_ROOT, "mind/include"),
        -- agent/, headers only: lpl/agent/Decision.hpp declares the ONE decision seam
        -- the hosted demon and this one share. No library is linked — the header is
        -- self-contained and every symbol in it is a pure interface or a POD.
        path.join(LPLPLUGIN_ROOT, "agent/include"),
        path.join(LPLPLUGIN_ROOT, "core/include"),
        path.join(LPLPLUGIN_ROOT, "math/include"),
        path.join(LPLPLUGIN_ROOT, "memory/include")
    )
    add_files(
        path.join(LPLASSISTANT_ROOT, "satellite/src/Protocol.cpp"),
        path.join(LPLASSISTANT_ROOT, "satellite/src/VoiceActivity.cpp"),
        path.join(LPLASSISTANT_ROOT, "satellite/src/WakeWord.cpp"),
        path.join(LPLASSISTANT_ROOT, "satellite/src/Duplex.cpp"),
        path.join(LPLASSISTANT_ROOT, "satellite/src/PowerState.cpp"),
        path.join(LPLASSISTANT_ROOT, "satellite/src/Parity.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Quant.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Tensor.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/TensorArena.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Vocab.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Tokenizer.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Model.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/KvCache.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Attention.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/FeedForward.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Transformer.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Sampler.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/GrammarSampler.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Inference.cpp"),
        path.join(LPLASSISTANT_ROOT, "infer/src/Parity.cpp"),
        -- mind/, the agency floor. The freestanding half only: Conversation.cpp is
        -- hosted by contract (std::string, a vector store, a real model) and lives in
        -- the separate lpl-mind-hosted target for exactly that reason.
        path.join(LPLASSISTANT_ROOT, "mind/src/Persona.cpp"),
        path.join(LPLASSISTANT_ROOT, "mind/src/Intent.cpp"),
        path.join(LPLASSISTANT_ROOT, "mind/src/Budget.cpp"),
        path.join(LPLASSISTANT_ROOT, "mind/src/Memory.cpp"),
        path.join(LPLASSISTANT_ROOT, "mind/src/Recall.cpp"),
        path.join(LPLASSISTANT_ROOT, "mind/src/ReAct.cpp"),
        path.join(LPLASSISTANT_ROOT, "mind/src/Reasoning.cpp"),
        path.join(LPLASSISTANT_ROOT, "mind/src/Dialogue.cpp"),
        path.join(LPLASSISTANT_ROOT, "mind/src/Parity.cpp")
    )
    add_files("libassistant/src/*.cpp", "libassistant/src/smoke/*.cpp")
target_end()
end -- if LPLASSISTANT_AVAILABLE

-- ===========================================================================
-- libknowledge — the LplKnowledge reader compiled -ffreestanding.
-- Same determinism flags as libengine, because anything linked into the kernel obeys
-- one arithmetic contract and not one per library. gnu++20 like libassistant: the
-- reader uses no feature above 20, and a module compiled to a different standard here
-- than in its own repository is a module whose two builds are not the same translation.
--
-- The source list is EXPLICIT and must stay in lock-step with
-- libknowledge/arch/i386/make.config. A glob would pull harvest/, mirror/ and media/ in —
-- the hosted half, which allocates, parses text and speaks HTTP.
-- ===========================================================================
if LPLKNOWLEDGE_AVAILABLE then
target("libknowledge")
    set_kind("static")
    set_basename("knowledge")
    set_languages("gnuxx20")
    add_cxxflags(
        "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
        "-Wall", "-Wextra",
        "-msse2", "-mfpmath=sse", "-ffp-contract=off", "-fno-math-errno", "-mstackrealign",
        {force = true}
    )
    add_defines("LPL_TARGET_KERNEL=1", "LPL_HAS_FOUNDATION")
    add_sysincludedirs(
        "libkxx/include",
        "libc/include"
    )
    add_includedirs(
        "libknowledge/include",
        path.join(LPLKNOWLEDGE_ROOT, "include"),
        path.join(LPLKNOWLEDGE_ROOT, "knowledge/include"),
        path.join(LPLKNOWLEDGE_ROOT, "corpus/include"),
        path.join(LPLPLUGIN_ROOT, "core/include"),
        path.join(LPLPLUGIN_ROOT, "math/include"),
        path.join(LPLPLUGIN_ROOT, "memory/include"),
        -- history/, headers only: the arithmetic of doubt is compiled into libengine, and
        -- this module CONSUMES it. `history/Fact.hpp` states the division — it trades in
        -- identifiers and says the strings live in LplKnowledge.
        path.join(LPLPLUGIN_ROOT, "history/include")
    )
    add_files(
        path.join(LPLKNOWLEDGE_ROOT, "knowledge/src/Types.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "knowledge/src/KnowledgePack.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "knowledge/src/Query.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "knowledge/src/FactStore.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "knowledge/src/Provenance.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "knowledge/src/History.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "knowledge/src/Parity.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "corpus/src/Urn.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "corpus/src/Locus.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "corpus/src/Language.cpp"),
        path.join(LPLKNOWLEDGE_ROOT, "corpus/src/TextView.cpp")
    )
    add_files("libknowledge/src/*.cpp", "libknowledge/src/smoke/*.cpp")
target_end()
end -- if LPLKNOWLEDGE_AVAILABLE

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
    if LPLASSISTANT_AVAILABLE then
        add_deps("libassistant")
    else
        -- No mind: the P14 block compiles out, like the world does without LplPlugin.
        add_defines("LPL_ASSISTANT_UNAVAILABLE=1")
    end
    if LPLKNOWLEDGE_AVAILABLE then
        add_deps("libknowledge")
    else
        -- No memory: the P18 block compiles out, like the world does without LplPlugin.
        add_defines("LPL_KNOWLEDGE_UNAVAILABLE=1")
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
    add_sysincludedirs("libc/include", "libengine/include", "libassistant/include",
                       "libknowledge/include")

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

        local libknowledge = target:dep("libknowledge") and target:dep("libknowledge"):targetfile() or nil
        local libassistant = target:dep("libassistant") and target:dep("libassistant"):targetfile() or nil
        local libengine = target:dep("libengine") and target:dep("libengine"):targetfile() or nil
        local libkxx = target:dep("libkxx") and target:dep("libkxx"):targetfile() or nil
        local libk = target:dep("libk"):targetfile()
        local out = target:targetfile()

        -- $(CC) -T linker.ld -o lpl.kernel <free flags> crti crtbegin OBJS \
        --       -nostdlib [-lknowledge -lassistant -lengine -lkxx] -lk -lgcc crtend crtn
        -- Link order is libknowledge -> libassistant -> libengine -> libkxx -> libk: the
        -- memory calls `lpl::history` and the mind calls the foundation (Fixed32, CORDIC,
        -- the arena), both of which are in libengine, which calls the C++ runtime
        -- (operator new / kstd::fatal) in libkxx, which calls kmalloc/kfree and the halt
        -- primitives in libk. A static archive only satisfies references already
        -- outstanding when the linker reaches it, so this order is not a preference. All
        -- of them are omitted when LplPlugin is unavailable (the kernel is then pure C).
        local argv = {"-T", linker, "-o", out, "-ffreestanding", "-O2", "-g", "-nostdlib", crti, crtbegin}
        table.join2(argv, rest)
        if libknowledge then table.insert(argv, libknowledge) end
        if libassistant then table.insert(argv, libassistant) end
        if libengine then table.insert(argv, libengine) end
        if libkxx then table.insert(argv, libkxx) end
        table.join2(argv, {libk, "-lgcc", crtend, crtn})

        os.mkdir(path.directory(out))
        if option.get("verbose") then cprint("${dim}%s %s", cc, table.concat(argv, " ")) end
        os.execv(cc, argv)
        -- Fail loudly if the image is not a valid multiboot kernel.
        os.execv("grub-file", {"--is-x86-multiboot", out})

        -- The PMM treats every byte past _kernel_end as free RAM, so an
        -- allocatable section placed beyond it (a C++ COMDAT orphan the linker
        -- script failed to collect) is handed out while the kernel still uses
        -- it. Mirrors the check-kernel-end rule of the shell build path.
        local nm = cc:gsub("%-gcc$", "-nm")
        local objdump = cc:gsub("%-gcc$", "-objdump")
        local kernel_end = nil
        for _, line in ipairs(os.iorunv(nm, {out}):split("\n", {plain = true})) do
            local addr, name = line:match("^(%x+)%s+%a%s+(.+)$")
            if name == "_kernel_end" then kernel_end = tonumber(addr, 16) end
        end
        assert(kernel_end, "_kernel_end not found in " .. out)

        local overflow = {}
        local headers = os.iorunv(objdump, {"-h", out})
        local pending = nil
        for _, line in ipairs(headers:split("\n", {plain = true})) do
            local _, name, size, vma = line:match("^%s*(%d+)%s+(%S+)%s+(%x+)%s+(%x+)")
            if name then
                pending = {name = name, last = tonumber(vma, 16) + tonumber(size, 16), size = tonumber(size, 16)}
            elseif pending then
                if line:find("ALLOC", 1, true) and pending.size > 0 and pending.last > kernel_end then
                    table.insert(overflow, pending.name)
                end
                pending = nil
            end
        end
        assert(#overflow == 0, format("allocatable section(s) past _kernel_end (0x%x): %s\n"
            .. "add the missing wildcard in kernel/arch/i386/linker.ld", kernel_end, table.concat(overflow, ", ")))

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
