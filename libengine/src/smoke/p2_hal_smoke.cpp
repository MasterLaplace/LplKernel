#include "libengine/libengine.h"

#include <lpl/platform/kernel/KernelPlatform.hpp>

namespace {

/**
 * @brief Queries the display surface, clears it to a known colour and reads pixel (0, 0) back.
 * @param platform The kernel platform.
 * @param out      Receives the surface geometry and whether the colour came back.
 */
void probeDisplay(lpl::platform::kernel::KernelPlatform &platform, libengine_p2_hal_smoke_result_t *out)
{
    lpl::platform::IDisplayBackend &display = platform.display();
    lpl::platform::SurfaceDescriptor surface;
    if (!display.querySurface(surface))
        return;

    out->display_available = 1u;
    out->surface_width = surface.width;
    out->surface_height = surface.height;
    out->surface_bpp = surface.bitsPerPixel;

    constexpr lpl::core::u32 kClearColor = 0x00112233u;
    display.clear(kClearColor);
    display.present();

    const lpl::core::u32 readback = display.readPixel(0u, 0u);
    out->clear_readback_raw = readback;
    out->clear_readback_ok = (readback == kClearColor) ? 1u : 0u;
}

/**
 * @brief Reads the clock's tick contract and checks the timestamp counter is monotonic.
 * @param platform The kernel platform.
 * @param out      Receives the tick frequency, a tick snapshot and whether the counter advanced.
 */
void probeClock(lpl::platform::kernel::KernelPlatform &platform, libengine_p2_hal_smoke_result_t *out)
{
    lpl::platform::IClockBackend &clock = platform.clock();
    out->clock_tick_hertz = clock.tickHertz();
    out->clock_tick_observed = clock.tickCount();
    const lpl::core::u64 tsc0 = clock.timestampCounter();
    const lpl::core::u64 tsc1 = clock.timestampCounter();
    out->clock_tsc_advanced = (tsc1 >= tsc0 && tsc1 != 0u) ? 1u : 0u;
}

/**
 * @brief Drains the decoded-character ring, which is empty when headless.
 *
 * @note The pending count taken before draining is the observable; the drain itself only
 *       proves the ring can be emptied without fault.
 *
 * @param platform The kernel platform.
 * @param out      Receives the pending count.
 */
void drainInput(lpl::platform::kernel::KernelPlatform &platform, libengine_p2_hal_smoke_result_t *out)
{
    lpl::platform::IInputBackend &input = platform.input();
    out->input_pending_count = input.pendingCount();
    char character = '\0';
    while (input.tryPopCharacter(character))
    {
    }
    out->input_query_ok = 1u;
}

/**
 * @brief Allocates pinned graphics memory, translates it to a physical address, and frees it.
 * @param platform The kernel platform.
 * @param out      Receives whether the allocation and the translation succeeded.
 */
void probeGpuMemory(lpl::platform::kernel::KernelPlatform &platform, libengine_p2_hal_smoke_result_t *out)
{
    lpl::platform::IGpuMemoryBackend &gpuMemory = platform.gpuMemory();
    auto allocation = gpuMemory.allocate(4096u, lpl::platform::GpuMemoryFlags::kPersistentlyMapped |
                                                    lpl::platform::GpuMemoryFlags::kHostCoherent);
    if (!allocation.has_value())
        return;

    out->gpu_alloc_ok = 1u;
    out->gpu_physical_nonzero = (allocation->physicalAddress != 0u) ? 1u : 0u;
    gpuMemory.free(*allocation);
}

} // namespace

extern "C" void libengine_p2_hal_smoke(libengine_p2_hal_smoke_result_t *out)
{
    if (out == nullptr)
        return;

    *out = libengine_p2_hal_smoke_result_t{};

    lpl::platform::kernel::KernelPlatform platform;
    probeDisplay(platform, out);
    probeClock(platform, out);
    drainInput(platform, out);
    probeGpuMemory(platform, out);
}
