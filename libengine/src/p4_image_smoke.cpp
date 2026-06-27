/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P4 image smoke — exercises the portable lpl::image module (integer-only
** color conversions + Image container) inside the freestanding kernel build.
** The reported signature must match the Linux oracle (tests/test-image-parity)
** bit-for-bit: the conversions are pure integer arithmetic so any divergence
** would signal a cross-target codegen/ABI problem, not rounding.
*/
#include "libengine/libengine.h"

#include <lpl/image/Image.hpp>
#include <lpl/image/Painter.hpp>

extern "C" void libengine_p4_image_smoke(libengine_p4_image_smoke_result_t *out)
{
    using namespace lpl;
    using lpl::core::u32;

    if (out == nullptr)
        return;

    *out = libengine_p4_image_smoke_result_t{};

    out->red_hue = image::rgbToHsb(image::packRgba(255, 0, 0)).hue;
    out->green_hue = image::rgbToHsb(image::packRgba(0, 255, 0)).hue;
    out->blue_hue = image::rgbToHsb(image::packRgba(0, 0, 255)).hue;

    const image::Rgba gray = image::packRgba(128, 128, 128);
    out->gray_roundtrip = (image::hsbToRgb(image::rgbToHsb(gray)) == gray) ? 1u : 0u;

    out->white_luma = image::luminanceOf(image::packRgba(255, 255, 255));

    image::Image red(4u, 4u);
    red.fill(image::packRgba(255, 0, 0));
    out->hist_red_count = red.histogram().red[255];

    image::Image grad(2u, 2u);
    grad.set(0, 0, image::packRgba(0, 0, 0));
    grad.set(1, 0, image::packRgba(255, 0, 0));
    grad.set(0, 1, image::packRgba(0, 255, 0));
    grad.set(1, 1, image::packRgba(0, 0, 255));
    out->centre_pixel = grad.sampleBilinear(0x8000u, 0x8000u) & 0x00FFFFFFu;

    image::Image scene(32u, 32u);
    image::paintParityScene(scene);
    out->painter_signature = image::foldSignature(scene);

    out->smoke_ok = (out->red_hue == 0u && out->green_hue == 120u && out->blue_hue == 240u &&
                     out->gray_roundtrip == 1u && out->white_luma == 255u && out->hist_red_count == 16u)
                        ? 1u
                        : 0u;
}
