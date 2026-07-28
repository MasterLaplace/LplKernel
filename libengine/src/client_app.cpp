/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Kernel client entry point — the freestanding mirror of
** LplPlugin/apps/client/main.cpp.
**
** This is the whole kernel<->engine seam: build a Config, construct an Engine
** with a KernelPlatform and the game World, init/run/shutdown. The only
** difference from the hosted client is the injected platform (KernelPlatform
** over the HAL, instead of LinuxPlatform over GLFW/chrono) and the profile
** flags. The kernel holds no renderer, scene, ECS or game logic whatsoever.
**
** Exposed to the C kernel through one extern "C" symbol, because kernel.c is C
** and this translation unit must be built with the engine's C++23 + SSE
** determinism flags.
*/
#include <lpl/core/Log.hpp>
#include <lpl/engine/Config.hpp>
#include <lpl/engine/Engine.hpp>
#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/pack/GamePack.hpp>
#include <lpl/pack/ParityPackBlob.hpp>
#include <lpl/pack/RecipeCodec.hpp>
#include <lpl/ecology/LivingRecipe.hpp>
#include <lpl/procgen/WorldRecipe.hpp>
#include <lpl/samples/CubePileWorld.hpp>
#include <lpl/samples/TerrainWorld.hpp>
#include <lpl/std/memory.hpp>

#include "libengine/libengine.h"

extern "C" void libengine_client_app_run(const void *pack_bytes, lpl::core::u32 pack_size)
{
    static lpl::platform::kernel::KernelLogger logger;
    lpl::core::Log::setLogger(&logger);

    lpl::core::Log::info("=== LplKernel Client ===");

    // ── The game arrives as bytes, not as code ───────────────────────────────
    //
    // A cartridge (.lplpak GRUB module) when GRUB loaded one, the reference pack
    // compiled into the image otherwise. Same freestanding reader, same content
    // hash, same wire layout as the parity gate uses — there is no second path
    // by which a world can reach ring 0.
    //
    // A cartridge that fails to validate is NOT silently replaced by the built-in
    // one: a corrupt game must be reported, not papered over.
    lpl::procgen::WorldRecipe recipe = lpl::procgen::parityWorldRecipe();
    lpl::ecology::LivingRecipe living = lpl::ecology::parityLivingRecipe();
    {
        const lpl::core::u8 *bytes = lpl::pack::kParityPackBytes;
        lpl::core::u32 size = lpl::pack::kParityPackSize;
        bool fromCartridge = false;
        if (pack_bytes != nullptr && pack_size != 0u)
        {
            bytes = static_cast<const lpl::core::u8 *>(pack_bytes);
            size = pack_size;
            fromCartridge = true;
        }

        lpl::pack::View view;
        lpl::pack::RecipeV1 wire{};
        if (view.open(bytes, size) && view.readRecipe(wire))
        {
            recipe = lpl::pack::toEngineRecipe(wire);
            lpl::core::Log::info(fromCartridge ? "Client: world decoded from the cartridge"
                                               : "Client: world decoded from the built-in pack");

            // The ecosystem is a SEPARATE section, and its absence is legitimate:
            // a cartridge may describe a world with nothing declared living on
            // it, and the host then keeps its own defaults. Only a wrong-sized
            // section is refused, by the reader.
            lpl::pack::LivingV1 livingWire{};
            if (view.readLiving(livingWire))
            {
                living = lpl::pack::toEngineLiving(livingWire);
                lpl::core::Log::info("Client: ecosystem decoded from the pack");
            }
            else
            {
                lpl::core::Log::info("Client: the pack declares no ecosystem, using the built-in one");
            }
        }
        else
        {
            lpl::core::Log::error("Client: the pack failed to validate — falling back to the compiled recipe");
        }
    }

    // Budgets are sized for the kernel's 4 MiB heap, not a desktop's. The hosted
    // client's defaults (a 64 MiB arena, 65536 world cells) would exhaust it
    // during Engine::init and starve the simulation of its entity chunks.
    // Which engine built-in system groups this World gets. Physics is the real
    // engine::systems::PhysicsSystem (the same one legacy's client registered
    // via core.registerSystem(Systems::PhysicsSystem()), and the one the server
    // profile uses) — not a bespoke per-game system: CubePileWorld no longer
    // steps its own CpuPhysicsBackend by hand, it just contributes Position/
    // Velocity/AABB/Mass entities to the World's registry like any other game
    // would. Rendering and networking stay off: CubePileWorld does its own
    // rasterizing, and there is no network in ring 0.
    auto config = lpl::engine::Config::Builder{}
                      .tickRate(60)
                      .serverMode(false)
                      .headless(false)
                      .arenaSize(256u * 1024u)
                      // Persistent ECS storage for the World's 1024 cubes:
                      // 4 chunks x (Position+Velocity+AABB+Mass) x 2 buffers.
                      // Bounded here so a World can never eat the 4 MiB heap.
                      .worldArenaSize(512u * 1024u)
                      .worldCellCapacity(1024u)
                      .enablePhysics(true)
                      .enableNetworking(false)
                      .enableRendering(false)
                      .enableGpu(false)
                      .enableBci(false)
                      // Runs the authoritative tick inside a real-time section.
                      // There the heap still serves its bounded O(1) paths (a
                      // slab hit, a TLSF block) and refuses only the unbounded
                      // ones (pool growth, first-fit walk), so a threatened
                      // deadline is caught rather than assumed. Measured on this
                      // profile: 0 unbounded and ~4.5 bounded operations per
                      // step, which holds the deadline without being zero yet.
                      .enableRealTimeGuard(true)
                      .build();

    // Which World the client profile runs.
    //
    // TerrainWorld is the showcase: it generates a landscape in ring 0 from a
    // seed — the same lpl::procgen passes the host folds — and runs the ai/ and
    // ecology/ modules on it. CubePileWorld remains the physics demo and, more
    // importantly, the shape the parity oracle exercises; switching the payload
    // does not touch that gate, which folds runCubePileAndFold directly and never
    // goes through a World.
    lpl::engine::Engine engine{config, lpl::pmr::make_unique<lpl::platform::kernel::KernelPlatform>(),
#if defined(LPL_KERNEL_WORLD_CUBEPILE)
                               lpl::pmr::make_unique<lpl::samples::CubePileWorld>()};
#else
                               lpl::pmr::make_unique<lpl::samples::TerrainWorld>(recipe, living)};
#endif

    if (auto result = engine.init(); !result)
    {
        lpl::core::Log::error("Kernel client init failed");
        return;
    }

    engine.run();
    engine.shutdown();

    lpl::core::Log::info("Kernel client exited cleanly");
}
