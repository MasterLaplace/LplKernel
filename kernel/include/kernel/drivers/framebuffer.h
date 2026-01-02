/**
 * @file framebuffer.h
 * @brief Linear framebuffer driver for graphics mode
 *
 * This driver provides basic pixel plotting capabilities using the
 * linear framebuffer provided by the Multiboot bootloader.
 */

#ifndef _KERNEL_FRAMEBUFFER_H
#define _KERNEL_FRAMEBUFFER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Color structure for 32-bit RGBA colors
 */
typedef struct {
    uint8_t r; /**< Red component (0-255) */
    uint8_t g; /**< Green component (0-255) */
    uint8_t b; /**< Blue component (0-255) */
    uint8_t a; /**< Alpha component (0-255, usually ignored) */
} color_t;

/**
 * @brief Framebuffer information structure
 */
typedef struct {
    uint32_t *buffer;       /**< Pointer to framebuffer memory (virtual address) */
    uint32_t physical_addr; /**< Physical address of framebuffer */
    uint32_t width;         /**< Width in pixels */
    uint32_t height;        /**< Height in pixels */
    uint32_t pitch;         /**< Bytes per scanline */
    uint8_t bpp;            /**< Bits per pixel */
    uint8_t red_pos;        /**< Red field position */
    uint8_t red_mask;       /**< Red field mask size */
    uint8_t green_pos;      /**< Green field position */
    uint8_t green_mask;     /**< Green field mask size */
    uint8_t blue_pos;       /**< Blue field position */
    uint8_t blue_mask;      /**< Blue field mask size */
    bool initialized;       /**< Whether the framebuffer is initialized */
} framebuffer_info_t;

/* Predefined colors */
#define COLOR_BLACK   ((color_t){0, 0, 0, 255})
#define COLOR_WHITE   ((color_t){255, 255, 255, 255})
#define COLOR_RED     ((color_t){255, 0, 0, 255})
#define COLOR_GREEN   ((color_t){0, 255, 0, 255})
#define COLOR_BLUE    ((color_t){0, 0, 255, 255})
#define COLOR_YELLOW  ((color_t){255, 255, 0, 255})
#define COLOR_CYAN    ((color_t){0, 255, 255, 255})
#define COLOR_MAGENTA ((color_t){255, 0, 255, 255})
#define COLOR_GRAY    ((color_t){128, 128, 128, 255})
#define COLOR_ORANGE  ((color_t){255, 165, 0, 255})

/**
 * @brief Initialize the framebuffer driver
 *
 * This function sets up the framebuffer driver using information
 * provided by the Multiboot info structure. It also maps the
 * framebuffer memory into the kernel's virtual address space.
 *
 * @return true on success, false on failure
 */
bool framebuffer_init(void);

/**
 * @brief Check if framebuffer is available
 *
 * @return true if a linear framebuffer is available, false otherwise
 */
bool framebuffer_available(void);

/**
 * @brief Get framebuffer information
 *
 * @return Pointer to the framebuffer info structure
 */
const framebuffer_info_t *framebuffer_get_info(void);

/**
 * @brief Clear the entire screen with a color
 *
 * @param color The color to fill the screen with
 */
void framebuffer_clear(color_t color);

/**
 * @brief Put a single pixel on the screen
 *
 * @param x X coordinate (0 = left edge)
 * @param y Y coordinate (0 = top edge)
 * @param color The color of the pixel
 */
void framebuffer_put_pixel(uint32_t x, uint32_t y, color_t color);

/**
 * @brief Get the color of a pixel
 *
 * @param x X coordinate
 * @param y Y coordinate
 * @return The color at the specified position
 */
color_t framebuffer_get_pixel(uint32_t x, uint32_t y);

/**
 * @brief Draw a horizontal line
 *
 * @param x1 Starting X coordinate
 * @param x2 Ending X coordinate
 * @param y Y coordinate
 * @param color Line color
 */
void framebuffer_draw_hline(uint32_t x1, uint32_t x2, uint32_t y, color_t color);

/**
 * @brief Draw a vertical line
 *
 * @param x X coordinate
 * @param y1 Starting Y coordinate
 * @param y2 Ending Y coordinate
 * @param color Line color
 */
void framebuffer_draw_vline(uint32_t x, uint32_t y1, uint32_t y2, color_t color);

/**
 * @brief Draw a rectangle outline
 *
 * @param x Top-left X coordinate
 * @param y Top-left Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color Rectangle color
 */
void framebuffer_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color);

/**
 * @brief Draw a filled rectangle
 *
 * @param x Top-left X coordinate
 * @param y Top-left Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color Fill color
 */
void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color);

/**
 * @brief Convert a color_t to a raw pixel value
 *
 * This function converts an RGB color to the format expected by
 * the framebuffer hardware.
 *
 * @param color The color to convert
 * @return The raw pixel value
 */
uint32_t framebuffer_color_to_pixel(color_t color);

/**
 * @brief Create a color from RGB values
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return The color structure
 */
static inline color_t framebuffer_rgb(uint8_t r, uint8_t g, uint8_t b) { return (color_t){r, g, b, 255}; }

#endif /* _KERNEL_FRAMEBUFFER_H */
