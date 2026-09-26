#include "libengine/libengine.h"

#include <lpl/image/Image.hpp>
#include <lpl/image/Painter.hpp>
#include <lpl/image/Surface.hpp>
#include <lpl/platform/kernel/KernelPlatform.hpp>

namespace {

/**
 * @brief Paints a vertical gradient over the whole image.
 * @param painter Painter over the image.
 * @param width   Image width in pixels.
 * @param height  Image height in pixels.
 */
void paintGradientBackground(lpl::image::Painter &painter, lpl::core::u32 width, lpl::core::u32 height)
{
    for (lpl::core::u32 y = 0u; y < height; ++y)
    {
        const lpl::core::u32 shade = (y * 180u) / height;
        painter.fillRect(0, static_cast<lpl::core::i32>(y), static_cast<lpl::core::i32>(width), 1,
                         lpl::image::packRgba(20, shade, 90));
    }
}

/**
 * @brief Paints a few filled shapes, an outline and a diagonal over the background.
 * @param painter Painter over the image.
 * @param width   Image width in pixels.
 * @param height  Image height in pixels.
 */
void paintShapes(lpl::image::Painter &painter, lpl::core::u32 width, lpl::core::u32 height)
{
    const lpl::core::i32 w = static_cast<lpl::core::i32>(width);
    const lpl::core::i32 h = static_cast<lpl::core::i32>(height);
    painter.fillRect(40, 40, 160, 100, lpl::image::packRgba(220, 60, 60));
    painter.fillCircle(w / 2, h / 2, 70, lpl::image::packRgba(60, 200, 120, 200));
    painter.drawRect(40, 40, 160, 100, lpl::image::packRgba(255, 255, 255));
    painter.drawLine(0, 0, w - 1, h - 1, lpl::image::packRgba(255, 230, 0));
}

} // namespace

extern "C" void libengine_p4_image_present_smoke(libengine_p4_image_present_smoke_result_t *out)
{
    using namespace lpl;
    using lpl::core::u32;

    if (out == nullptr)
        return;
    *out = libengine_p4_image_present_smoke_result_t{};

    platform::kernel::KernelPlatform platformBackends;
    platform::IDisplayBackend &display = platformBackends.display();

    platform::SurfaceDescriptor surface;
    if (!display.querySurface(surface) || surface.buffer == nullptr || surface.width == 0u || surface.height == 0u)
        return;

    out->display_available = 1u;
    out->width = surface.width;
    out->height = surface.height;

    image::Image scene(surface.width, surface.height);
    image::Painter painter(scene);

    paintGradientBackground(painter, surface.width, surface.height);
    paintShapes(painter, surface.width, surface.height);

    out->image_signature = image::foldSignature(scene);

    const u32 pitch = (surface.pitch != 0u) ? surface.pitch : surface.width * 4u;
    image::blitToFramebuffer(scene, surface.buffer, surface.width, surface.height, pitch, 0, 0);
    display.present();

    out->present_ok = 1u;
}
