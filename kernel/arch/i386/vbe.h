#ifndef ARCH_I386_VBE_H
#define ARCH_I386_VBE_H

#include <stdint.h>

/**
 * VBE mode numbers and their corresponding resolutions and color depths.
 * Reference: https://wiki.osdev.org/User:Omarrx024/VESA_Tutorial
 *
 * MODE    RESOLUTION  BITS PER PIXEL  MAXIMUM COLORS
 * 0x0100  640x400     8               256
 * 0x0101  640x480     8               256
 * 0x0102  800x600     4               16
 * 0x0103  800x600     8               256
 * 0x010D  320x200     15              32k
 * 0x010E  320x200     16              64k
 * 0x010F  320x200     24/32*          16m
 * 0x0110  640x480     15              32k
 * 0x0111  640x480     16              64k
 * 0x0112  640x480     24/32*          16m
 * 0x0113  800x600     15              32k
 * 0x0114  800x600     16              64k
 * 0x0115  800x600     24/32*          16m
 * 0x0116  1024x768    15              32k
 * 0x0117  1024x768    16              64k
 * 0x0118  1024x768    24/32*          16m
*/

typedef struct vbe_info_block {
    uint8_t VbeSignature[4]; // == "VESA"
    uint16_t VbeVersion; // VBE version
    uint16_t OemStringPtr[2]; // isa vbeFarPtr
    uint8_t  Capabilities[4]; // bitfield
    uint16_t VideoModePtr[2]; // isa vbeFarPtr
    uint16_t TotalMemory; // in 64KB blocks
    uint16_t OemSoftwareRev; // VBE implementation Software revision
    uint32_t OemVendorNamePtr; // isa vbeFarPtr
    uint32_t OemProductNamePtr; // isa vbeFarPtr
    uint32_t OemProductRevPtr; // isa vbeFarPtr
    uint8_t Reserved[222]; // padding to 256 bytes
    uint8_t OemData[256]; // OEM String
} __attribute__((packed)) vbe_info_block;

typedef struct vbe_mode_info {
    uint16_t attributes;
    uint8_t window_a;
    uint8_t window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr; // ptr to driver functions
    uint16_t pitch; // bytes per scanline

    uint16_t width;
    uint16_t height;
    uint8_t w_char;
    uint8_t y_char;
    uint8_t planes;
    uint8_t bpp;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved0;

    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t reserved_mask;
    uint8_t reserved_position;
    uint8_t direct_color_attributes;

    uint32_t framebuffer;
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_size;
    uint8_t reserved1[206];
} __attribute__((packed)) vbe_mode_info_t;

static vbe_mode_info_t vbe_mode_info;

void vbe_initialize(void) {
    __asm__ __volatile__ (
        "int $0x10"
        :
        : "a"(0x4F02), "b"(0x117) /* Mode graphique 1024x768x16 bits */
        : "memory"
    );
}

void vbe_set_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t *framebuffer = (uint32_t *)vbe_mode_info.framebuffer;
    uint32_t pitch = vbe_mode_info.pitch / 4; // pitch is in bytes, convert to 32-bit pixels
    framebuffer[y * pitch + x] = color;
}

// /**
//  * @brief Find the mode with the given resolution and depth in the list of available modes.
//  *
//  * @param x  The horizontal resolution in pixels.
//  * @param y  The vertical resolution in pixels.
//  * @param d  The color depth in bits per pixel.
//  * @return uint16_t  The mode number, or 0 if no mode was found.
//  */
// uint16_t findMode(int x, int y, int d)
// {
//     vbe_info_block *vbeInfo = (vbe_info_block *)0x8000;
//     vbe_mode_info *modeInfo = (vbe_mode_info *)0x8000;
//     uint16_t *modes = (uint16_t *)vbeInfo->VideoModePtr;

//     while (*modes != 0xFFFF)
//     {
//         modeInfo = (vbe_mode_info *)(0x8000 + *modes);
//         if (modeInfo->width == x && modeInfo->height == y && modeInfo->bpp == d)
//             return *modes;
//         modes++;
//     }
//     return 0;
// }

// /**
//  * @brief Get the framebuffer address.
//  *
//  * @return uint32_t  The framebuffer address.
//  */
// uint32_t getFramebuffer()
// {
//     vbe_info_block *vbeInfo = (vbe_info_block *)0xA0000;
//     return ((vbe_mode_info *)(0xA0000 + vbeInfo->VideoModePtr[0]))->framebuffer;
// }

// /**
//  * @brief Get the framebuffer pitch.
//  *
//  * @return uint16_t  The framebuffer pitch.
//  */
// uint16_t getPitch()
// {
//     vbe_info_block *vbeInfo = (vbe_info_block *)0xA0000;
//     return ((vbe_mode_info *)(0xA0000 + vbeInfo->VideoModePtr[0]))->pitch;
// }

// /**
//  * @brief Get the framebuffer width.
//  *
//  * @return uint16_t  The framebuffer width.
//  */
// uint16_t getWidth()
// {
//     vbe_info_block *vbeInfo = (vbe_info_block *)0xA0000;
//     return ((vbe_mode_info *)(0xA0000 + vbeInfo->VideoModePtr[0]))->width;
// }

// /**
//  * @brief Get the framebuffer height.
//  *
//  * @return uint16_t  The framebuffer height.
//  */
// uint16_t getHeight()
// {
//     vbe_info_block *vbeInfo = (vbe_info_block *)0xA0000;
//     return ((vbe_mode_info *)(0xA0000 + vbeInfo->VideoModePtr[0]))->height;
// }

// /**
//  * @brief Get the framebuffer color depth.
//  *
//  * @return uint8_t  The framebuffer color depth.
//  */
// uint8_t getDepth()
// {
//     vbe_info_block *vbeInfo = (vbe_info_block *)0xA0000;
//     return ((vbe_mode_info *)(0xA0000 + vbeInfo->VideoModePtr[0]))->bpp;
// }

#endif // ARCH_I386_VBE_H
