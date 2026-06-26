/**
 * @file hal_display.c
 * @brief Software-LFB display backend for the engine HAL.
 *
 * Implements the hal_display_* contract over the Multiboot linear framebuffer
 * driver. This is the "DRM/KMS scanout" seam: the engine renders through it
 * without knowing whether the surface is a software LFB or (later) a VirtIO-GPU
 * resource. Colors cross the ABI as packed 0x00RRGGBB and are translated to the
 * driver's color_t here so the engine never sees a kernel type.
 */
#include <kernel/hal/hal.h>

#include <kernel/drivers/framebuffer.h>

#include <stddef.h>

static inline color_t hal_color_from_rgb(uint32_t color_rgb)
{
    color_t color;
    color.r = (uint8_t) ((color_rgb >> 16) & 0xFFu);
    color.g = (uint8_t) ((color_rgb >> 8) & 0xFFu);
    color.b = (uint8_t) (color_rgb & 0xFFu);
    color.a = 0xFFu;
    return color;
}

static inline uint32_t hal_rgb_from_color(color_t color)
{
    return ((uint32_t) color.r << 16) | ((uint32_t) color.g << 8) | (uint32_t) color.b;
}

bool hal_display_query_surface(hal_surface_descriptor_t *out_descriptor)
{
    if (out_descriptor == NULL || !framebuffer_available())
        return false;

    const framebuffer_info_t *info = framebuffer_get_info();
    if (info == NULL || !info->initialized)
        return false;

    out_descriptor->buffer = info->buffer;
    out_descriptor->physical_address = info->physical_addr;
    out_descriptor->width = info->width;
    out_descriptor->height = info->height;
    out_descriptor->pitch = info->pitch;
    out_descriptor->bits_per_pixel = info->bpp;
    return true;
}

void hal_display_clear(uint32_t color_rgb)
{
    if (!framebuffer_available())
        return;
    framebuffer_clear(hal_color_from_rgb(color_rgb));
}

uint32_t hal_display_read_pixel(uint32_t x, uint32_t y)
{
    if (!framebuffer_available())
        return 0u;
    return hal_rgb_from_color(framebuffer_get_pixel(x, y));
}

void hal_display_present(void)
{
    /* Software-LFB renders directly into scanout memory; present is a no-op.
       The VirtIO-GPU backend will issue RESOURCE_FLUSH / atomic flip here. */
}
