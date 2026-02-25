/**
 * @file plasma.h
 * @brief Plasma shader effect - converted from GLSL/C++
 *
 * Original shader: https://x.com/XorDev/status/1894123951401378051
 * This is a software implementation that doesn't require any graphics library.
 */

#ifndef _KERNEL_PLASMA_H
#define _KERNEL_PLASMA_H

#include <kernel/drivers/framebuffer.h>
#include <stdint.h>

/* ============================================================================
 * Vector types - simple C structs to replace C++ vec2/vec4
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    float x, y;
} vec2_t;

typedef struct __attribute__((packed)) {
    float x, y, z, w;
} vec4_t;

/* ============================================================================
 * vec2 constructors
 * ============================================================================ */

static inline vec2_t vec2(float x, float y) { return (vec2_t){x, y}; }

static inline vec2_t vec2_scalar(float s) { return (vec2_t){s, s}; }

/* ============================================================================
 * vec4 constructors
 * ============================================================================ */

static inline vec4_t vec4(float x, float y, float z, float w) { return (vec4_t){x, y, z, w}; }

static inline vec4_t vec4_scalar(float s) { return (vec4_t){s, s, s, s}; }

/* ============================================================================
 * vec2 operations
 * ============================================================================ */

/* Addition */
static inline vec2_t vec2_add(vec2_t a, vec2_t b) { return (vec2_t){a.x + b.x, a.y + b.y}; }

static inline vec2_t vec2_add_s(vec2_t a, float s) { return (vec2_t){a.x + s, a.y + s}; }

/* Subtraction */
static inline vec2_t vec2_sub(vec2_t a, vec2_t b) { return (vec2_t){a.x - b.x, a.y - b.y}; }

/* Multiplication */
static inline vec2_t vec2_mul(vec2_t a, vec2_t b) { return (vec2_t){a.x * b.x, a.y * b.y}; }

static inline vec2_t vec2_mul_s(vec2_t a, float s) { return (vec2_t){a.x * s, a.y * s}; }

/* Division */
static inline vec2_t vec2_div_s(vec2_t a, float s) { return (vec2_t){a.x / s, a.y / s}; }

/* Dot product */
static inline float vec2_dot(vec2_t a, vec2_t b) { return a.x * b.x + a.y * b.y; }

/* Swizzle: yx */
static inline vec2_t vec2_yx(vec2_t a) { return (vec2_t){a.y, a.x}; }

/* Swizzle: xyyx -> vec4 */
static inline vec4_t vec2_xyyx(vec2_t a) { return (vec4_t){a.x, a.y, a.y, a.x}; }

/* ============================================================================
 * vec4 operations
 * ============================================================================ */

/* Addition */
static inline vec4_t vec4_add(vec4_t a, vec4_t b) { return (vec4_t){a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }

static inline vec4_t vec4_add_s(vec4_t a, float s) { return (vec4_t){a.x + s, a.y + s, a.z + s, a.w + s}; }

/* Subtraction */
static inline vec4_t vec4_sub_sf(float s, vec4_t a) { return (vec4_t){s - a.x, s - a.y, s - a.z, s - a.w}; }

/* Multiplication */
static inline vec4_t vec4_mul(vec4_t a, vec4_t b) { return (vec4_t){a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }

static inline vec4_t vec4_mul_s(vec4_t a, float s) { return (vec4_t){a.x * s, a.y * s, a.z * s, a.w * s}; }

/* Division */
static inline vec4_t vec4_div(vec4_t a, vec4_t b) { return (vec4_t){a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w}; }

/* ============================================================================
 * Math functions for vectors (require math.h)
 * ============================================================================ */

#include <math.h>

static inline vec2_t vec2_abs(vec2_t a) { return (vec2_t){fabsf(a.x), fabsf(a.y)}; }

static inline vec2_t vec2_cos(vec2_t a) { return (vec2_t){cosf(a.x), cosf(a.y)}; }

static inline vec4_t vec4_sin(vec4_t a) { return (vec4_t){sinf(a.x), sinf(a.y), sinf(a.z), sinf(a.w)}; }

static inline vec4_t vec4_exp(vec4_t a) { return (vec4_t){expf(a.x), expf(a.y), expf(a.z), expf(a.w)}; }

static inline vec4_t vec4_tanh(vec4_t a) { return (vec4_t){tanhf(a.x), tanhf(a.y), tanhf(a.z), tanhf(a.w)}; }

/* ============================================================================
 * Plasma effect functions
 * ============================================================================ */

/**
 * @brief Compute plasma color for a single pixel
 *
 * @param x Pixel X coordinate
 * @param y Pixel Y coordinate
 * @param width Screen width
 * @param height Screen height
 * @param time Animation time (in radians, 0 to 2*PI for one cycle)
 * @return RGB color
 */
color_t plasma_pixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float time);

/**
 * @brief Render one frame of the plasma effect to the framebuffer
 *
 * @param time Animation time (in radians)
 */
void plasma_render_frame(float time);

/**
 * @brief Run the plasma animation loop
 *
 * This function runs an animated plasma effect. It will loop indefinitely
 * until interrupted.
 */
void plasma_run(void);

#endif /* _KERNEL_PLASMA_H */
