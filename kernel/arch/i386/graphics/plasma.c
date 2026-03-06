/**
 * @file plasma.c
 * @brief Plasma shader effect implementation
 *
 * Original GLSL shader by @XorDev: https://x.com/XorDev/status/1894123951401378051
 * Converted from C++ to C for kernel space execution.
 *
 * The original obfuscated shader code:
 *   vec2 p=(FC*2.-r)/r.y,l,i,v=p*(l+=4.-4.*abs(.7-dot(p,p)));
 *   for(;i.y++<8.;o+=(sin(v.xyyx())+1.)*abs(v.x-v.y))v+=cos(v.yx()*i.y+i+t)/i.y+.7;
 *   o=tanh(5.*exp(l.x-4.-p.y*vec4(-1,1,2,0))/o);
 */

#include <kernel/drivers/framebuffer.h>
#include <kernel/graphics/plasma.h>
#include <kernel/lib/asmutils.h>
#include <math.h>

/**
 * @brief Compute plasma color for a single pixel
 *
 * This is a direct translation of the GLSL shader to C.
 */
color_t plasma_pixel(uint32_t px, uint32_t py, uint32_t width, uint32_t height, float t)
{
    vec4_t o = vec4(0, 0, 0, 0);
    vec2_t r = vec2((float) width, (float) height);
    vec2_t FC = vec2((float) px, (float) height - 1 - py);

    /*
     * Original C++:
     * vec2 p=(FC*2.-r)/r.y,l,i,v=p*(l+=4.-4.*abs(.7-dot(p,p)));
     *
     * Breaking it down:
     * - p = (FC*2 - r) / r.y
     * - l = {0, 0} initially, then l += 4 - 4*abs(0.7 - dot(p,p))
     * - v = p * l
     */
    vec2_t p = vec2_div_s(vec2_sub(vec2_mul_s(FC, 2.0f), r), r.y);

    vec2_t l = vec2(0, 0);
    vec2_t i = vec2(0, 0);

    /* l += 4 - 4*abs(0.7 - dot(p,p)) */
    float dot_pp = vec2_dot(p, p);
    float l_val = 4.0f - 4.0f * fabsf(0.7f - dot_pp);
    l = vec2_add_s(l, l_val);

    /* v = p * l (component-wise, but l.x == l.y here so it's like p * scalar) */
    vec2_t v = vec2_mul(p, l);

    /*
     * Original C++ loop:
     * for(;i.y++<8.;o+=(sin(v.xyyx())+1.)*abs(v.x-v.y))
     *     v+=cos(v.yx()*i.y+i+t)/i.y+.7;
     */
    while (i.y < 8.0f)
    {
        i.y += 1.0f;

        /* v += cos(v.yx() * i.y + i + t) / i.y + 0.7 */
        vec2_t v_yx = vec2_yx(v);
        vec2_t temp = vec2_mul_s(v_yx, i.y);
        temp = vec2_add(temp, i);
        temp = vec2_add_s(temp, t);
        temp = vec2_cos(temp);
        temp = vec2_div_s(temp, i.y);
        temp = vec2_add_s(temp, 0.7f);
        v = vec2_add(v, temp);

        /* o += (sin(v.xyyx()) + 1) * abs(v.x - v.y) */
        vec4_t v_xyyx = vec2_xyyx(v);
        vec4_t sin_val = vec4_sin(v_xyyx);
        sin_val = vec4_add_s(sin_val, 1.0f);
        float abs_diff = fabsf(v.x - v.y);
        o = vec4_add(o, vec4_mul_s(sin_val, abs_diff));
    }

    /*
     * Original C++:
     * o = tanh(5.*exp(l.x-4.-p.y*vec4(-1,1,2,0))/o);
     *
     * Breaking it down:
     * - inner = l.x - 4 - p.y * vec4(-1, 1, 2, 0)
     * - o = tanh(5 * exp(inner) / o)
     */
    vec4_t py_vec = vec4_mul_s(vec4(-1.0f, 1.0f, 2.0f, 0.0f), p.y);
    vec4_t inner = vec4_sub_sf(l.x - 4.0f, py_vec);
    vec4_t exp_val = vec4_exp(inner);
    vec4_t scaled = vec4_mul_s(exp_val, 5.0f);
    vec4_t divided = vec4_div(scaled, o);
    o = vec4_tanh(divided);

    /* Convert to color (clamp to 0-255) */
    uint8_t cr = (uint8_t) (o.x * 255.0f);
    uint8_t cg = (uint8_t) (o.y * 255.0f);
    uint8_t cb = (uint8_t) (o.z * 255.0f);

    return framebuffer_rgb(cr, cg, cb);
}

/**
 * @brief Render one frame of the plasma effect
 */
void plasma_render_frame(float time)
{
    if (!framebuffer_available())
        return;

    const framebuffer_info_t *fb = framebuffer_get_info();
    uint32_t width = fb->width;
    uint32_t height = fb->height;

    /* Render each pixel */
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            color_t color = plasma_pixel(x, y, width, height, time);
            framebuffer_put_pixel(x, y, color);
        }
    }
}

/**
 * @brief Simple delay loop (busy wait)
 *
 * This is a crude timing mechanism. In a real kernel, you'd use
 * a proper timer/interrupt-based delay.
 */
static void delay_loop(uint32_t iterations)
{
    volatile uint32_t i;
    for (i = 0; i < iterations; i++)
    cpu_no_operation();
}

/**
 * @brief Run the plasma animation
 */
void plasma_run(void)
{
    if (!framebuffer_available())
        return;

    float time = 0.0f;
    const float time_step = 0.05f; /* Animation speed */

    /* Run animation loop */
    while (1)
    {
        plasma_render_frame(time);

        time += time_step;
        if (time > 2.0f * M_PI)
        {
            time -= 2.0f * M_PI;
        }

        /* Small delay to control frame rate */
        /* In a real kernel, you'd use proper timing here */
        delay_loop(100000);
    }
}
