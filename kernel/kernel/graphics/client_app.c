#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/graphics/client_app.h>

#if !defined(LPL_PLUGIN_UNAVAILABLE)

#    include <kernel/cpu/clock.h>
#    include <kernel/drivers/keyboard.h>
#    include <kernel/graphics/font8x16.h>
#    include <kernel/hal/hal.h>

#    include <libengine/libengine.h>

/* --- Minimal text overlay (8x16 bitmap font, MSB = leftmost pixel). Drawn on
   the display surface only — non-authoritative, never folded. -------------- */
static void client_char(uint32_t *dst, uint32_t pitch_px, uint32_t x, uint32_t y, unsigned char ch, uint32_t rgb)
{
    const unsigned char *glyph = kernel_font8x16[ch];
    for (uint32_t row = 0u; row < 16u; ++row)
    {
        const unsigned char bits = glyph[row];
        uint32_t *drow = &dst[(y + row) * pitch_px + x];
        for (uint32_t col = 0u; col < 8u; ++col)
            if (bits & (0x80u >> col))
                drow[col] = rgb;
    }
}

static void client_text(uint32_t *dst, uint32_t pitch_px, uint32_t x, uint32_t y, const char *s, uint32_t rgb)
{
    for (uint32_t i = 0u; s[i] != '\0'; ++i)
        client_char(dst, pitch_px, x + i * 8u, y, (unsigned char) s[i], rgb);
}

/* Append unsigned decimal to buf at *pos (buf must have room), advancing *pos. */
static void client_append_u32(char *buf, uint32_t *pos, uint32_t v)
{
    char rev[10];
    uint32_t len = 0u;
    if (v == 0u)
        rev[len++] = '0';
    while (v != 0u)
    {
        rev[len++] = (char) ('0' + (v % 10u));
        v /= 10u;
    }
    while (len != 0u)
        buf[(*pos)++] = rev[--len];
}

static void client_append_str(char *buf, uint32_t *pos, const char *s)
{
    for (uint32_t i = 0u; s[i] != '\0'; ++i)
        buf[(*pos)++] = s[i];
}

void kernel_client_app_run(Serial_t *com1)
{
    hal_surface_descriptor_t surface;
    if (!hal_display_available() || !hal_display_query_surface(&surface) || surface.buffer == 0 ||
        surface.bits_per_pixel != 32u || surface.width < 64u || surface.height < 64u)
    {
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: client app: no usable 32bpp display, skipped\n");
        return;
    }

    const uint32_t W = surface.width;
    const uint32_t H = surface.height;
    const uint32_t pitch_px = surface.pitch / 4u;
    uint32_t *dst = surface.buffer;

    serial_write_string(com1,
                        "[" KERNEL_SYSTEM_STRING "]: client app live (WASD=cam Q/E=zoom P=possess N=next X=exit)\n");
    libengine_sim_init();
    const uint32_t entity_count = libengine_sim_entity_count();

    /* Orbit camera + possession state (non-authoritative). */
    float cam_yaw = 0.6f, cam_pitch = 0.35f, cam_dist = 12.0f;
    int32_t possess = -1;

    /* Fixed-timestep simulation (tick_hz steps/sec, real-time correct) with an
       uncapped render loop, so the on-screen FPS reflects raw throughput. */
    const uint32_t tick_hz = clock_get_tick_hz();
    uint32_t sim_tick = clock_get_tick_count();

    uint32_t fps = 0u, frames_in_window = 0u, window_start = sim_tick;
    int running = 1;

    while (running)
    {
        /* Drain input: adjust camera / possession; exit on X or ESC. */
        char key;
        while (keyboard_try_pop_char(&key))
        {
            switch (key)
            {
            case 'a': cam_yaw -= 0.08f; break;
            case 'd': cam_yaw += 0.08f; break;
            case 'w':
                cam_pitch += 0.06f;
                if (cam_pitch > 1.40f)
                    cam_pitch = 1.40f;
                break;
            case 's':
                cam_pitch -= 0.06f;
                if (cam_pitch < -1.40f)
                    cam_pitch = -1.40f;
                break;
            case 'q':
                cam_dist -= 0.6f;
                if (cam_dist < 2.0f)
                    cam_dist = 2.0f;
                break;
            case 'e':
                cam_dist += 0.6f;
                if (cam_dist > 40.0f)
                    cam_dist = 40.0f;
                break;
            case 'p': possess = (possess < 0) ? 0 : -1; break;
            case 'n':
                if (possess >= 0 && entity_count > 0u)
                    possess = (int32_t) (((uint32_t) possess + 1u) % entity_count);
                break;
            case 'x':
            case 27: running = 0; break;
            default: break;
            }
        }

        /* Advance simulation to catch up with wall-clock (fixed steps). */
        const uint32_t now = clock_get_tick_count();
        while ((int32_t) (now - sim_tick) > 0)
        {
            libengine_sim_step();
            ++sim_tick;
        }

        uint32_t sw = 0u, sh = 0u;
        const uint32_t *src = libengine_sim_render(cam_yaw, cam_pitch, cam_dist, possess, &sw, &sh);
        if (src == 0 || sw == 0u || sh == 0u)
            break;

        /* Nearest-neighbour scale the engine frame onto the display surface. */
        for (uint32_t dy = 0u; dy < H; ++dy)
        {
            const uint32_t syv = (dy * sh) / H;
            const uint32_t *srow = &src[syv * sw];
            uint32_t *drow = &dst[dy * pitch_px];
            for (uint32_t dx = 0u; dx < W; ++dx)
                drow[dx] = srow[(dx * sw) / W];
        }

        /* FPS over a 1-second wall-clock window. */
        ++frames_in_window;
        const uint32_t elapsed = now - window_start;
        if (tick_hz != 0u && elapsed >= tick_hz)
        {
            fps = (frames_in_window * tick_hz) / elapsed;
            frames_in_window = 0u;
            window_start = now;
        }

        /* HUD (non-authoritative overlay). */
        char line[64];
        uint32_t p = 0u;
        client_append_str(line, &p, "FPS: ");
        client_append_u32(line, &p, fps);
        line[p] = '\0';
        client_text(dst, pitch_px, 8u, 8u, line, 0x00FFFF66u);

        p = 0u;
        client_append_str(line, &p, "ENTITIES: ");
        client_append_u32(line, &p, entity_count);
        line[p] = '\0';
        client_text(dst, pitch_px, 8u, 26u, line, 0x00A0C0FFu);

        p = 0u;
        if (possess < 0)
            client_append_str(line, &p, "POSSESS: none");
        else
        {
            client_append_str(line, &p, "POSSESS: #");
            client_append_u32(line, &p, (uint32_t) possess);
        }
        line[p] = '\0';
        client_text(dst, pitch_px, 8u, 44u, line, 0x0060FF80u);

        client_text(dst, pitch_px, 8u, H - 20u, "WASD=cam Q/E=zoom P=possess N=next X=exit", 0x00808890u);

        hal_display_present();
    }

    serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: client app exited\n");
}

#else /* LPL_PLUGIN_UNAVAILABLE */

void kernel_client_app_run(Serial_t *com1)
{
    serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: client app unavailable (no engine module)\n");
}

#endif /* LPL_PLUGIN_UNAVAILABLE */
