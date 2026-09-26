#include <lpl/core/Log.hpp>
#include <lpl/engine/Boot.hpp>
#include <lpl/pack/ViewerPackBlob.hpp>
#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/samples/CubePileWorld.hpp>
#include <lpl/samples/TerrainWorld.hpp>
#include <lpl/std/memory.hpp>
#include <lpl/std/vector.hpp>

#include "libengine/libengine.h"

namespace {

/**
 * @brief Copies the cartridge GRUB loaded into a buffer the boot is allowed to write.
 *
 * @details A COPY, not the module itself. A cartridge that carries a parity section can
 *          repair itself, and repairing needs somewhere to put the corrected byte — so
 *          BootRequest takes a mutable buffer. The bytes GRUB handed over are the kernel's own
 *          memory and could be written in place, but a boot that mutates the module it was
 *          loaded from is a boot that cannot be retried, and this copy costs one allocation of
 *          a kilobyte or two, once.
 *
 * @param pack_bytes The module GRUB loaded, or NULL.
 * @param pack_size  Its size in bytes.
 * @param request    Receives the copy's address and size when there is a cartridge.
 */
void useWritableCopyOfCartridge(const void *pack_bytes, lpl::core::u32 pack_size, lpl::engine::BootRequest &request)
{
    static lpl::pmr::vector<lpl::core::u8> writablePack;
    if (pack_bytes == nullptr || pack_size == 0u)
        return;

    const auto *source = static_cast<const lpl::core::u8 *>(pack_bytes);
    writablePack.resize(pack_size, lpl::core::u8{0});
    for (lpl::core::u32 i = 0u; i < pack_size; ++i)
        writablePack[i] = source[i];
    request.packBytes = writablePack.data();
    request.packSize = pack_size;
}

/**
 * @brief Builds the World the client profile runs.
 *
 * @details TerrainWorld is the showcase: it generates a landscape in ring 0 from a seed — the
 *          same lpl::procgen passes the host folds — and runs the ai/ and ecology/ modules on
 *          it. CubePileWorld remains the physics demo, selected by LPL_KERNEL_WORLD_CUBEPILE.
 *
 * @note Switching the payload does not touch the parity gate, which folds runCubePileAndFold
 *       directly and never goes through a World.
 *
 * @param recipe The world recipe the cartridge decoded to.
 * @param living The living recipe that goes with it.
 * @param view   How the world is shown.
 * @return The World.
 */
lpl::pmr::unique_ptr<lpl::engine::World> makeClientWorld(const lpl::procgen::WorldRecipe &recipe,
                                                         const lpl::ecology::LivingRecipe &living,
                                                         const lpl::engine::ViewProfile &view)
{
#if defined(LPL_KERNEL_WORLD_CUBEPILE)
    (void) recipe;
    (void) living;
    (void) view;
    return lpl::pmr::unique_ptr<lpl::engine::World>{lpl::pmr::make_unique<lpl::samples::CubePileWorld>()};
#else
    return lpl::pmr::unique_ptr<lpl::engine::World>{
        lpl::pmr::make_unique<lpl::samples::TerrainWorld>(recipe, living, view)};
#endif
}

} // namespace

extern "C" void libengine_client_app_run(const void *pack_bytes, lpl::core::u32 pack_size)
{
    static lpl::platform::kernel::KernelLogger logger;
    lpl::core::Log::setLogger(&logger);

    lpl::engine::BootRequest request;
    request.host = lpl::engine::HostProfile::Ring0Client;
    request.tickRate = 60u;
    request.banner = "=== LplKernel Client ===";
    useWritableCopyOfCartridge(pack_bytes, pack_size, request);
    request.fallbackPackBytes = lpl::pack::kViewerPackBytes;
    request.fallbackPackSize = lpl::pack::kViewerPackSize;

    lpl::engine::bootGame(request, lpl::pmr::make_unique<lpl::platform::kernel::KernelPlatform>(), makeClientWorld);
}
