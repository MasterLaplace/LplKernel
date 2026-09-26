#include <kernel/hal/hal.h>

#include <kernel/drivers/framebuffer.h>

#include <stddef.h>

static inline color_t hardware_abstraction_layer_color_from_rgb(uint32_t color_rgb)
{
    color_t color;
    color.r = (uint8_t) ((color_rgb >> 16) & 0xFFu);
    color.g = (uint8_t) ((color_rgb >> 8) & 0xFFu);
    color.b = (uint8_t) (color_rgb & 0xFFu);
    color.a = 0xFFu;
    return color;
}

static inline uint32_t hardware_abstraction_layer_rgb_from_color(color_t color)
{
    return ((uint32_t) color.r << 16) | ((uint32_t) color.g << 8) | (uint32_t) color.b;
}

bool hardware_abstraction_layer_display_query_surface(hardware_abstraction_layer_surface_descriptor_t *out_descriptor)
{
    if (out_descriptor == NULL)
        return false;

    if (hardware_abstraction_layer_virtio_gpu_display_active())
        return hardware_abstraction_layer_virtio_gpu_display_query(out_descriptor);

    if (!framebuffer_available())
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

bool hardware_abstraction_layer_display_available(void)
{
    return hardware_abstraction_layer_virtio_gpu_display_active() || framebuffer_available();
}

void hardware_abstraction_layer_display_clear(uint32_t color_rgb)
{
    if (hardware_abstraction_layer_virtio_gpu_display_active())
    {
        hardware_abstraction_layer_virtio_gpu_display_clear(color_rgb);
        return;
    }
    if (!framebuffer_available())
        return;
    framebuffer_clear(hardware_abstraction_layer_color_from_rgb(color_rgb));
}

uint32_t hardware_abstraction_layer_display_read_pixel(uint32_t x, uint32_t y)
{
    if (hardware_abstraction_layer_virtio_gpu_display_active())
        return hardware_abstraction_layer_virtio_gpu_display_read_pixel(x, y);
    if (!framebuffer_available())
        return 0u;
    return hardware_abstraction_layer_rgb_from_color(framebuffer_get_pixel(x, y));
}

void hardware_abstraction_layer_display_present(void)
{
    if (hardware_abstraction_layer_virtio_gpu_display_active())
        hardware_abstraction_layer_virtio_gpu_display_present();
}
