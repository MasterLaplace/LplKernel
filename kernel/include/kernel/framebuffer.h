#ifndef KERNEL_FRAMEBUFFER_H_
    #define KERNEL_FRAMEBUFFER_H_

#include <stddef.h>
#include <stdint.h>

////////////////////////////////////////////////////////////
// Public functions of the terminal module API
////////////////////////////////////////////////////////////

void framebuffer_initialize(void);

void framebuffer_set_pixel(uint32_t x, uint32_t y, uint32_t color);

// void video_clear(void);

// void video_putpixel(uint16_t x, uint16_t y, uint32_t color);

// uint32_t video_getpixel(uint16_t x, uint16_t y);

// uint16_t video_get_width(void);

// uint16_t video_get_height(void);

// uint8_t video_get_depth(void);

// uint16_t video_get_pitch(void);

// void video_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color);

// void video_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color);

// void video_draw_fillrect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color);

// void video_draw_circle(uint16_t x0, uint16_t y0, uint16_t radius, uint32_t color);

// void video_draw_fillcircle(uint16_t x0, uint16_t y0, uint16_t radius, uint32_t color);

#endif /* !KERNEL_FRAMEBUFFER_H_ */
