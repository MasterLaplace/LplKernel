/*
** EPITECH PROJECT, 2024
** Laplace-Project [WSL : Ubuntu]
** File description:
** framebuffer
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/framebuffer.h>

#include "vbe.h"

////////////////////////////////////////////////////////////
// Private members of the video module ! using VBE 0xA0000
////////////////////////////////////////////////////////////

// static uint16_t video_width = 0;
// static uint16_t video_height = 0;
// static uint8_t video_depth = 0;
// static uint16_t video_pitch = 0;
// static uint32_t video_framebuffer = 0;
// vbe_mode_info_t mode_info;

////////////////////////////////////////////////////////////
// Public functions of the video module
////////////////////////////////////////////////////////////

void framebuffer_initialize(void)
{
    vbe_initialize();

    if (vbe_mode_info.framebuffer == 0) {
        // Si le framebuffer n'est pas valide, afficher une erreur
        terminal_write_string("Error: Invalid framebuffer address.\n");
        for (;;); // Boucle infinie pour arrêter l'exécution
    }
}

void framebuffer_set_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t *framebuffer = (uint32_t *)vbe_mode_info.framebuffer;
    uint32_t pitch = vbe_mode_info.pitch / 4; // Convertir le pitch en pixels 32 bits

    if (x >= vbe_mode_info.width || y >= vbe_mode_info.height) {
        // Ne pas écrire en dehors des limites de l'écran
        return;
    }

    framebuffer[y * pitch + x] = color;
}

// void video_clear(void)
// {
//     uint32_t i = 0;
//     uint32_t j = 0;

//     for (i = 0; i < video_height; i++)
//     {
//         for (j = 0; j < video_width; j++)
//         {
//             video_putpixel(j, i, 0);
//         }
//     }
// }

// void video_putpixel(uint16_t x, uint16_t y, uint32_t color)
// {
//     uint32_t *pixel = (uint32_t *)(video_framebuffer + (x * 4) + (y * video_pitch));
//     *pixel = color;
// }

// uint32_t video_getpixel(uint16_t x, uint16_t y)
// {
//     uint32_t *pixel = (uint32_t *)(video_framebuffer + (x * 4) + (y * video_pitch));
//     return *pixel;
// }

// uint16_t video_get_width(void)
// {
//     return video_width;
// }

// uint16_t video_get_height(void)
// {
//     return video_height;
// }

// uint8_t video_get_depth(void)
// {
//     return video_depth;
// }

// uint16_t video_get_pitch(void)
// {
//     return video_pitch;
// }

// #define abs(x) ((x) < 0 ? -(x) : (x))

// void video_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
// {
//     int16_t dx = abs(x1 - x0);
//     int16_t sx = x0 < x1 ? 1 : -1;
//     int16_t dy = -abs(y1 - y0);
//     int16_t sy = y0 < y1 ? 1 : -1;
//     int16_t err = dx + dy;
//     int16_t e2;

//     while (true) {
//         video_putpixel(x0, y0, color);
//         if (x0 == x1 && y0 == y1)
//             break;
//         e2 = 2 * err;
//         if (e2 >= dy) {
//             err += dy;
//             x0 += sx;
//         }
//         if (e2 <= dx) {
//             err += dx;
//             y0 += sy;
//         }
//     }
// }

// void video_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
// {
//     uint16_t i = 0;
//     uint16_t j = 0;

//     for (i = 0; i < height; i++)
//     {
//         for (j = 0; j < width; j++)
//         {
//             video_putpixel(x + j, y + i, color);
//         }
//     }
// }

// void video_draw_fillrect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
// {
//     uint16_t i = 0;
//     uint16_t j = 0;

//     for (i = 0; i < height; i++)
//     {
//         for (j = 0; j < width; j++)
//         {
//             video_putpixel(x + j, y + i, color);
//         }
//     }
// }

// void video_draw_circle(uint16_t x0, uint16_t y0, uint16_t radius, uint32_t color)
// {
//     int16_t f = 1 - radius;
//     int16_t ddF_x = 1;
//     int16_t ddF_y = -2 * radius;
//     int16_t x = 0;
//     int16_t y = radius;

//     video_putpixel(x0, y0 + radius, color);
//     video_putpixel(x0, y0 - radius, color);
//     video_putpixel(x0 + radius, y0, color);
//     video_putpixel(x0 - radius, y0, color);

//     while (x < y) {
//         if (f >= 0) {
//             y--;
//             ddF_y += 2;
//             f += ddF_y;
//         }
//         x++;
//         ddF_x += 2;
//         f += ddF_x;
//         video_putpixel(x0 + x, y0 + y, color);
//         video_putpixel(x0 - x, y0 + y, color);
//         video_putpixel(x0 + x, y0 - y, color);
//         video_putpixel(x0 - x, y0 - y, color);
//         video_putpixel(x0 + y, y0 + x, color);
//         video_putpixel(x0 - y, y0 + x, color);
//         video_putpixel(x0 + y, y0 - x, color);
//         video_putpixel(x0 - y, y0 - x, color);
//     }
// }

// void video_draw_fillcircle(uint16_t x0, uint16_t y0, uint16_t radius, uint32_t color)
// {
//     int16_t f = 1 - radius;
//     int16_t ddF_x = 1;
//     int16_t ddF_y = -2 * radius;
//     int16_t x = 0;
//     int16_t y = radius;

//     video_draw_line(x0 - radius, y0, x0 + radius, y0, color);
//     video_draw_line(x0, y0 - radius, x0, y0 + radius, color);

//     while (x < y) {
//         if (f >= 0) {
//             y--;
//             ddF_y += 2;
//             f += ddF_y;
//         }
//         x++;
//         ddF_x += 2;
//         f += ddF_x;
//         video_draw_line(x0 - x, y0 + y, x0 + x, y0 + y, color);
//         video_draw_line(x0 + x, y0 - y, x0 - x, y0 - y, color);
//         video_draw_line(x0 + y, y0 + x, x0 - y, y0 + x, color);
//         video_draw_line(x0 + y, y0 - x, x0 - y, y0 - x, color);
//     }
// }
