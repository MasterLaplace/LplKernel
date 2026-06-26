/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P2 HAL smoke — exercises the engine platform backends (lpl::platform)
** through their kernel HAL implementations: display surface query + clear +
** pixel read-back, the clock tick/timestamp contract, the input ring drain,
** and a pinned graphics-memory allocate / physical-translate / free. Proves
** the kernel platform seam is wired end to end (the P2 HAL bring-up gate).
** These values are observability, not part of the bit-identical determinism
** contract (surface geometry is QEMU-config dependent; clock/timestamp are
** wall-clock).
*/
#include "libengine/libengine.h"

#include <lpl/platform/kernel/KernelPlatform.hpp>

extern "C" void libengine_p2_hal_smoke(libengine_p2_hal_smoke_result_t *out)
{
    using namespace lpl::platform;
    using lpl::core::u32;

    if (out == nullptr)
        return;

    *out = libengine_p2_hal_smoke_result_t{};

    kernel::KernelPlatform platform;

    // --- Display: query, clear to a known color, read pixel (0,0) back. ---
    IDisplayBackend &display = platform.display();
    SurfaceDescriptor surface;
    if (display.querySurface(surface))
    {
        out->display_available = 1u;
        out->surface_width = surface.width;
        out->surface_height = surface.height;
        out->surface_bpp = surface.bitsPerPixel;

        constexpr u32 kClearColor = 0x00112233u;
        display.clear(kClearColor);
        display.present();

        const u32 readback = display.readPixel(0u, 0u);
        out->clear_readback_raw = readback;
        out->clear_readback_ok = (readback == kClearColor) ? 1u : 0u;
    }

    // --- Clock: tick contract + monotonic timestamp counter. ---
    IClockBackend &clock = platform.clock();
    out->clock_tick_hertz = clock.tickHertz();
    out->clock_tick_observed = clock.tickCount();
    const lpl::core::u64 tsc0 = clock.timestampCounter();
    const lpl::core::u64 tsc1 = clock.timestampCounter();
    out->clock_tsc_advanced = (tsc1 >= tsc0 && tsc1 != 0u) ? 1u : 0u;

    // --- Input: drain the decoded-character ring (empty when headless). ---
    IInputBackend &input = platform.input();
    out->input_pending_count = input.pendingCount();
    char character = '\0';
    while (input.tryPopCharacter(character))
    {
        // Drain fully; the count above is the observable.
    }
    out->input_query_ok = 1u;

    // --- Graphics memory: pinned allocate, translate, free. ---
    IGpuMemoryBackend &gpuMemory = platform.gpuMemory();
    auto allocation = gpuMemory.allocate(4096u, GpuMemoryFlags::kPersistentlyMapped | GpuMemoryFlags::kHostCoherent);
    if (allocation.has_value())
    {
        out->gpu_alloc_ok = 1u;
        out->gpu_physical_nonzero = (allocation->physicalAddress != 0u) ? 1u : 0u;
        gpuMemory.free(*allocation);
    }
}
