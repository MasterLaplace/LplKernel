/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Kernel client entry point — the freestanding mirror of
** LplPlugin/apps/client/main.cpp.
**
** There is almost nothing left here, and that is the point. Decoding a cartridge,
** sizing the budgets for the machine, constructing the Engine and running
** init/run/shutdown are the same on every host, so they moved into
** engine::bootGame and engine::HostProfile. What remains is what only the KERNEL
** can say: which platform seam to inject, where the cartridge bytes are, and which
** World the client profile runs.
**
** Exposed to the C kernel through one extern "C" symbol, because kernel.c is C and
** this translation unit must be built with the engine's C++23 + SSE determinism
** flags.
*/
#include <lpl/core/Log.hpp>
#include <lpl/engine/Boot.hpp>
#include <lpl/pack/ViewerPackBlob.hpp>
#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/samples/CubePileWorld.hpp>
#include <lpl/samples/TerrainWorld.hpp>
#include <lpl/std/memory.hpp>
#include <lpl/std/vector.hpp>

#include "libengine/libengine.h"

extern "C" void libengine_client_app_run(const void *pack_bytes, lpl::core::u32 pack_size)
{
    static lpl::platform::kernel::KernelLogger logger;
    lpl::core::Log::setLogger(&logger);

    lpl::engine::BootRequest request;
    request.host = lpl::engine::HostProfile::Ring0Client;
    request.tickRate = 60u;
    request.banner = "=== LplKernel Client ===";
    // A COPY, not the module itself.
    //
    // A cartridge that carries a parity section can repair itself, and repairing needs
    // somewhere to put the corrected byte — so BootRequest takes a mutable buffer. The
    // bytes GRUB handed us are the kernel's own memory and could be written in place,
    // but a boot that mutates the module it was loaded from is a boot that cannot be
    // retried, and this copy costs one allocation of a kilobyte or two, once.
    static lpl::pmr::vector<lpl::core::u8> writablePack;
    if (pack_bytes != nullptr && pack_size != 0u)
    {
        const auto *source = static_cast<const lpl::core::u8 *>(pack_bytes);
        writablePack.resize(pack_size, lpl::core::u8{0});
        for (lpl::core::u32 i = 0u; i < pack_size; ++i)
            writablePack[i] = source[i];
        request.packBytes = writablePack.data();
        request.packSize = pack_size;
    }
    // The built-in fallback is the VIEWER's world, not the parity gate's: the gate
    // needs a 24x24 world small enough to fold in a boot battery, the demo needs
    // one worth looking at. Sharing one blob meant the published browser demo — a
    // deployment whose runner has no lpl-bake, so no cartridge — showed the test
    // world.
    request.fallbackPackBytes = lpl::pack::kViewerPackBytes;
    request.fallbackPackSize = lpl::pack::kViewerPackSize;

    lpl::engine::bootGame(
        request, lpl::pmr::make_unique<lpl::platform::kernel::KernelPlatform>(),
        [](const lpl::procgen::WorldRecipe &recipe, const lpl::ecology::LivingRecipe &living,
           const lpl::engine::ViewProfile &view) {
            // TerrainWorld is the showcase: it generates a landscape in ring 0 from
            // a seed — the same lpl::procgen passes the host folds — and runs the
            // ai/ and ecology/ modules on it. CubePileWorld remains the physics
            // demo; switching the payload does not touch the parity gate, which
            // folds runCubePileAndFold directly and never goes through a World.
#if defined(LPL_KERNEL_WORLD_CUBEPILE)
            (void) recipe;
            (void) living;
            return lpl::pmr::unique_ptr<lpl::engine::World>{lpl::pmr::make_unique<lpl::samples::CubePileWorld>()};
#else
            return lpl::pmr::unique_ptr<lpl::engine::World>{
                lpl::pmr::make_unique<lpl::samples::TerrainWorld>(recipe, living, view)};
#endif
        });
}
