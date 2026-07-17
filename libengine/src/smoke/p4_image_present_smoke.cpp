/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P4 image present smoke — paints a 2D scene with the lpl::image Painter into a
** full-surface Image and blits it to the display scanout via the IDisplayBackend
** HAL, then presents. Proves the Image -> hardware_abstraction_layer_display -> GPU/LFB path end to end.
*/
#include "libengine/libengine.h"

#include <lpl/image/Image.hpp>
#include <lpl/image/Painter.hpp>
#include <lpl/image/Surface.hpp>
#include <lpl/platform/kernel/KernelPlatform.hpp>

extern "C" void libengine_p4_image_present_smoke(libengine_p4_image_present_smoke_result_t *out)
{
    using namespace lpl;
    using lpl::core::i32;
    using lpl::core::u32;

    if (out == nullptr)
        return;
    *out = libengine_p4_image_present_smoke_result_t{};

    platform::kernel::KernelPlatform platformBackends;
    platform::IDisplayBackend &display = platformBackends.display();

    platform::SurfaceDescriptor surface;
    if (!display.querySurface(surface) || surface.buffer == nullptr || surface.width == 0u || surface.height == 0u)
        return; // no surface (text-mode boot) -> smoke skipped

    out->display_available = 1u;
    out->width = surface.width;
    out->height = surface.height;

    image::Image scene(surface.width, surface.height);
    image::Painter painter(scene);

    // Vertical gradient background.
    for (u32 y = 0u; y < surface.height; ++y)
    {
        const u32 shade = (y * 180u) / surface.height;
        painter.fillRect(0, static_cast<i32>(y), static_cast<i32>(surface.width), 1, image::packRgba(20, shade, 90));
    }

    // A few filled shapes + an outline + a diagonal.
    painter.fillRect(40, 40, 160, 100, image::packRgba(220, 60, 60));
    painter.fillCircle(static_cast<i32>(surface.width) / 2, static_cast<i32>(surface.height) / 2, 70,
                       image::packRgba(60, 200, 120, 200));
    painter.drawRect(40, 40, 160, 100, image::packRgba(255, 255, 255));
    painter.drawLine(0, 0, static_cast<i32>(surface.width) - 1, static_cast<i32>(surface.height) - 1,
                     image::packRgba(255, 230, 0));

    out->image_signature = image::foldSignature(scene);

    const u32 pitch = (surface.pitch != 0u) ? surface.pitch : surface.width * 4u;
    image::blitToFramebuffer(scene, surface.buffer, surface.width, surface.height, pitch, 0, 0);
    display.present();

    out->present_ok = 1u;
}
