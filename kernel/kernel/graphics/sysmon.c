#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/graphics/sysmon.h>

#include <kernel/graphics/font8x16.h>

#include <kernel/cpu/clock.h>
#include <kernel/cpu/pci.h>
#include <kernel/cpu/pmm.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/hal/hal.h>
#include <kernel/memory/frame_arena.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/pinned_memory.h>
#include <kernel/memory/pool_allocator.h>
#include <kernel/memory/ring_buffer.h>
#include <kernel/memory/stack_allocator.h>

/* ------------------------------------------------------------------------- */
/* Tunables                                                                  */
/* ------------------------------------------------------------------------- */
#define SYSMON_BUDDY_ORDERS   11u
#define SYSMON_SIZE_CLASSES   12u
#define SYSMON_FLOW_LANES     4u
#define SYSMON_FLOW_PARTICLES 96u
#define SYSMON_SPIN_CAP       4000000u /* watchdog on the per-frame pacing spin */

/* ------------------------------------------------------------------------- */
/* Surface drawing layer — renders into the HAL display buffer (virtio-gpu or */
/* software-LFB), then hal_display_present() flips it. Colors are 0x00RRGGBB. */
/* ------------------------------------------------------------------------- */
static uint32_t *g_buf;     /* active draw target (back buffer if any)   */
static uint32_t g_pitch_px; /* pixels per scanline of the draw target    */
static uint32_t g_w, g_h;   /* visible dimensions                        */

/* Double buffering: render into g_back (tightly packed, pitch == width),
   then blit the whole frame into the scanout in one pass and present. This
   removes the tearing of drawing straight into live scanout memory (the
   software-LFB path) and gives the virtio-gpu path one clean transfer. If the
   back buffer cannot be allocated we fall back to drawing into the scanout. */
static uint32_t *g_surface; /* scanout virtual address                  */
static uint32_t g_surf_pitch_px;
static uint32_t *g_back; /* back buffer (NULL => direct/no double-buf) */

static void sysmon_blit_present(void)
{
    if (g_back != 0)
    {
        for (uint32_t y = 0u; y < g_h; ++y)
        {
            const uint32_t *src = &g_back[y * g_w];
            uint32_t *dst = &g_surface[y * g_surf_pitch_px];
            for (uint32_t x = 0u; x < g_w; ++x)
                dst[x] = src[x];
        }
    }
    hal_display_present();
}

static inline uint32_t sm_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
}

static inline void sm_pixel(uint32_t x, uint32_t y, uint32_t rgb)
{
    if (x < g_w && y < g_h)
        g_buf[y * g_pitch_px + x] = rgb;
}

static void sm_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb)
{
    for (uint32_t yy = 0u; yy < h && (y + yy) < g_h; ++yy)
    {
        uint32_t *row = &g_buf[(y + yy) * g_pitch_px];
        for (uint32_t xx = 0u; xx < w && (x + xx) < g_w; ++xx)
            row[x + xx] = rgb;
    }
}

static void sm_hline(uint32_t x1, uint32_t x2, uint32_t y, uint32_t rgb)
{
    for (uint32_t x = x1; x <= x2; ++x)
        sm_pixel(x, y, rgb);
}

static void sm_vline(uint32_t x, uint32_t y1, uint32_t y2, uint32_t rgb)
{
    for (uint32_t y = y1; y <= y2; ++y)
        sm_pixel(x, y, rgb);
}

static void sm_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb)
{
    if (w == 0u || h == 0u)
        return;
    sm_hline(x, x + w - 1u, y, rgb);
    sm_hline(x, x + w - 1u, y + h - 1u, rgb);
    sm_vline(x, y, y + h - 1u, rgb);
    sm_vline(x + w - 1u, y, y + h - 1u, rgb);
}

/* ------------------------------------------------------------------------- */
/* Telemetry snapshot                                                        */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint32_t free_pages;
    uint32_t buddy[SYSMON_BUDDY_ORDERS];
    uint32_t uaf_anomalies;
    uint32_t double_free;

    uint32_t heap_small_free_blocks;
    uint32_t heap_small_free_bytes;
    uint32_t heap_large_allocs;
    uint32_t heap_hot_loop_depth;
    uint32_t heap_size_class[SYSMON_SIZE_CLASSES];

    uint32_t arena_used, arena_cap, arena_peak;
    uint32_t pool_free, pool_cap, pool_peak;
    uint32_t stack_used, stack_cap, stack_peak;

    uint32_t pinned_allocated;
    uint32_t pinned_released;

    uint32_t ring_count, ring_cap, ring_high;
    uint32_t ring_enqueue, ring_dequeue, ring_failed;

    uint32_t pci_devices;
} sysmon_snapshot_t;

static sysmon_snapshot_t g_now;
static sysmon_snapshot_t g_prev;

/* Animated data particles per flow lane (x in 0..1000). */
typedef struct {
    uint16_t active;
    uint16_t x_milli;
    uint8_t speed;
} sysmon_particle_t;

static sysmon_particle_t g_flow[SYSMON_FLOW_LANES][SYSMON_FLOW_PARTICLES];
static uint32_t g_baseline_pages;

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */
static uint32_t sm_clampu(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint32_t sm_lerp(uint32_t a, uint32_t b, uint32_t t_milli)
{
    const uint32_t t = sm_clampu(t_milli, 0u, 1000u);
    const uint8_t ar = (a >> 16) & 0xFFu, ag = (a >> 8) & 0xFFu, ab = a & 0xFFu;
    const uint8_t br = (b >> 16) & 0xFFu, bg = (b >> 8) & 0xFFu, bb = b & 0xFFu;
    return sm_rgb((uint8_t) ((ar * (1000u - t) + br * t) / 1000u), (uint8_t) ((ag * (1000u - t) + bg * t) / 1000u),
                  (uint8_t) ((ab * (1000u - t) + bb * t) / 1000u));
}

/* Green -> amber -> red ramp for an occupancy ratio (0..1000). */
static uint32_t sm_heat(uint32_t ratio_milli)
{
    const uint32_t green = sm_rgb(40u, 210u, 120u);
    const uint32_t amber = sm_rgb(235u, 200u, 50u);
    const uint32_t red = sm_rgb(230u, 60u, 60u);
    if (ratio_milli < 500u)
        return sm_lerp(green, amber, ratio_milli * 2u);
    return sm_lerp(amber, red, (ratio_milli - 500u) * 2u);
}

static uint32_t sm_log_ratio(uint32_t value, uint32_t reference)
{
    if (reference == 0u)
        return 0u;
    return (value >= reference) ? 1000u : (uint32_t) (((uint64_t) value * 1000u) / reference);
}

static void sm_gauge_h(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t ratio_milli, uint32_t peak_milli)
{
    sm_fill(x, y, w, h, sm_rgb(24u, 28u, 44u));
    sm_rect(x, y, w, h, sm_rgb(60u, 70u, 100u));
    const uint32_t fill = (w >= 2u) ? ((w - 2u) * sm_clampu(ratio_milli, 0u, 1000u)) / 1000u : 0u;
    if (fill > 0u)
        sm_fill(x + 1u, y + 1u, fill, h - 2u, sm_heat(ratio_milli));
    if (peak_milli > 0u && w >= 2u)
    {
        const uint32_t px = x + 1u + ((w - 2u) * sm_clampu(peak_milli, 0u, 1000u)) / 1000u;
        sm_vline(px, y + 1u, y + h - 2u, sm_rgb(240u, 240u, 255u));
    }
}

static void sm_bar_v(uint32_t x, uint32_t base_y, uint32_t w, uint32_t height, uint32_t rgb)
{
    if (height == 0u || base_y < height)
        return;
    sm_fill(x, base_y - height, w, height, rgb);
}

static void sm_panel(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t accent)
{
    sm_fill(x, y, w, h, sm_rgb(16u, 18u, 30u));
    sm_hline(x, x + w - 1u, y, accent);
}

/* --- Text overlay (8x16 bitmap font, MSB = leftmost pixel), optional scale. - */
static void sm_char(uint32_t x, uint32_t y, unsigned char ch, uint32_t rgb, uint32_t scale)
{
    const unsigned char *glyph = kernel_font8x16[ch];
    for (uint32_t row = 0u; row < 16u; ++row)
    {
        const unsigned char bits = glyph[row];
        for (uint32_t col = 0u; col < 8u; ++col)
        {
            if (bits & (0x80u >> col))
            {
                if (scale <= 1u)
                    sm_pixel(x + col, y + row, rgb);
                else
                    sm_fill(x + col * scale, y + row * scale, scale, scale, rgb);
            }
        }
    }
}

static void sm_text(uint32_t x, uint32_t y, const char *s, uint32_t rgb, uint32_t scale)
{
    const uint32_t adv = 8u * (scale ? scale : 1u);
    for (uint32_t i = 0u; s[i] != '\0'; ++i)
        sm_char(x + i * adv, y, (unsigned char) s[i], rgb, scale);
}

static const char *sm_utoa(uint32_t v, char *buf16)
{
    char *p = buf16 + 15;
    *p = '\0';
    if (v == 0u)
        *--p = '0';
    while (v != 0u)
    {
        *--p = (char) ('0' + (v % 10u));
        v /= 10u;
    }
    return p;
}

static void sm_label_value(uint32_t x, uint32_t y, const char *label, uint32_t value, uint32_t rgb)
{
    char buf[16];
    uint32_t len = 0u;
    while (label[len] != '\0')
        ++len;
    sm_text(x, y, label, rgb, 1u);
    sm_text(x + (len + 1u) * 8u, y, sm_utoa(value, buf), sm_rgb(230u, 235u, 245u), 1u);
}

/* ------------------------------------------------------------------------- */
/* Telemetry sampling                                                        */
/* ------------------------------------------------------------------------- */
static void sysmon_sample(sysmon_snapshot_t *s)
{
    s->free_pages = physical_memory_manager_get_free_page_count();
    for (uint32_t o = 0u; o < SYSMON_BUDDY_ORDERS; ++o)
        s->buddy[o] = physical_memory_manager_debug_get_free_block_count((uint8_t) o);
    s->uaf_anomalies = physical_memory_manager_get_uaf_anomaly_count();
    s->double_free = physical_memory_manager_debug_get_double_free_count();

    s->heap_small_free_blocks = kernel_heap_get_small_free_block_count();
    s->heap_small_free_bytes = kernel_heap_get_small_free_bytes();
    s->heap_large_allocs = kernel_heap_get_large_allocation_count();
    s->heap_hot_loop_depth = kernel_heap_get_hot_loop_depth();
    for (uint32_t c = 0u; c < SYSMON_SIZE_CLASSES; ++c)
        s->heap_size_class[c] = kernel_heap_get_size_class_free_count(c);

    s->arena_used = kernel_frame_arena_is_initialized() ? kernel_frame_arena_get_used_bytes() : 0u;
    s->arena_cap = kernel_frame_arena_is_initialized() ? kernel_frame_arena_get_capacity_bytes() : 0u;
    s->arena_peak = kernel_frame_arena_is_initialized() ? kernel_frame_arena_get_peak_used_bytes() : 0u;

    s->pool_cap = kernel_pool_allocator_is_initialized() ? kernel_pool_get_capacity() : 0u;
    s->pool_free = kernel_pool_allocator_is_initialized() ? kernel_pool_get_free_count() : 0u;
    s->pool_peak = kernel_pool_allocator_is_initialized() ? kernel_pool_get_peak_used_count() : 0u;

    s->stack_used = kernel_stack_allocator_is_initialized() ? kernel_stack_allocator_get_used() : 0u;
    s->stack_cap = kernel_stack_allocator_is_initialized() ? kernel_stack_allocator_get_capacity() : 0u;
    s->stack_peak = kernel_stack_allocator_is_initialized() ? kernel_stack_allocator_get_peak_used() : 0u;

    s->pinned_allocated = kernel_pinned_get_allocated_pages();
    s->pinned_released = kernel_pinned_get_released_pages();

    s->ring_cap = kernel_ring_buffer_is_initialized() ? kernel_ring_buffer_get_capacity() : 0u;
    s->ring_count = kernel_ring_buffer_is_initialized() ? kernel_ring_buffer_get_count() : 0u;
    s->ring_high = kernel_ring_buffer_is_initialized() ? kernel_ring_buffer_get_high_watermark() : 0u;
    s->ring_enqueue = kernel_ring_buffer_is_initialized() ? kernel_ring_buffer_get_enqueue_count() : 0u;
    s->ring_dequeue = kernel_ring_buffer_is_initialized() ? kernel_ring_buffer_get_dequeue_count() : 0u;
    s->ring_failed = kernel_ring_buffer_is_initialized() ? kernel_ring_buffer_get_failed_enqueue_count() : 0u;

    s->pci_devices = peripheral_component_interconnect_get_device_count();
}

/* ------------------------------------------------------------------------- */
/* Flow particles                                                            */
/* ------------------------------------------------------------------------- */
static void sysmon_flow_spawn(uint32_t lane, uint32_t amount, uint8_t speed)
{
    uint32_t budget = sm_clampu(amount, 0u, 8u);
    for (uint32_t i = 0u; i < SYSMON_FLOW_PARTICLES && budget > 0u; ++i)
    {
        if (!g_flow[lane][i].active)
        {
            g_flow[lane][i].active = 1u;
            g_flow[lane][i].x_milli = 0u;
            g_flow[lane][i].speed = (uint8_t) (speed + (i & 7u));
            --budget;
        }
    }
}

static void sysmon_flow_advance_and_draw(uint32_t lane, uint32_t x, uint32_t y, uint32_t w, uint32_t rgb)
{
    for (uint32_t i = 0u; i < SYSMON_FLOW_PARTICLES; ++i)
    {
        if (!g_flow[lane][i].active)
            continue;
        uint32_t nx = (uint32_t) g_flow[lane][i].x_milli + g_flow[lane][i].speed;
        if (nx >= 1000u)
        {
            g_flow[lane][i].active = 0u;
            continue;
        }
        g_flow[lane][i].x_milli = (uint16_t) nx;
        sm_fill(x + (w * nx) / 1000u, y, 4u, 4u, rgb);
    }
}

/* ------------------------------------------------------------------------- */
/* One frame                                                                 */
/* ------------------------------------------------------------------------- */
static void sysmon_draw_frame(uint32_t frame)
{
    const uint32_t W = g_w, H = g_h;
    const uint32_t accent = sm_lerp(sm_rgb(80u, 140u, 255u), sm_rgb(180u, 90u, 255u), (frame * 12u) % 1000u);

    sm_fill(0u, 0u, W, H, sm_rgb(8u, 10u, 18u)); /* clear back buffer */
    sm_fill(0u, 0u, W, 6u, accent);              /* animated title strip */

    const uint32_t margin = W / 40u;
    const uint32_t col_w = (W - margin * 3u) / 2u;
    const uint32_t left_x = margin;
    const uint32_t right_x = margin * 2u + col_w;
    const uint32_t y0 = 26u;
    const uint32_t bus_y = H - 24u;
    const uint32_t gap = 12u;
    const uint32_t row_h = (bus_y - 12u - y0 - gap) / 2u; /* two stacked rows fill to the bus */
    const uint32_t y2 = y0 + row_h + gap;

    /* --- PMM occupancy + buddy heatmap (left) --- */
    const uint32_t pmm_h = row_h;
    sm_panel(left_x, y0, col_w, pmm_h, sm_rgb(60u, 200u, 140u));
    {
        const uint32_t used = (g_baseline_pages > g_now.free_pages) ? (g_baseline_pages - g_now.free_pages) : 0u;
        sm_gauge_h(left_x + 8u, y0 + 22u, col_w - 16u, 18u,
                   sm_log_ratio(used, g_baseline_pages ? g_baseline_pages : 1u), 0u);

        const uint32_t base_y = y0 + pmm_h - 14u;
        const uint32_t bw = (col_w - 16u) / SYSMON_BUDDY_ORDERS;
        uint32_t maxb = 1u;
        for (uint32_t o = 0u; o < SYSMON_BUDDY_ORDERS; ++o)
            if (g_now.buddy[o] > maxb)
                maxb = g_now.buddy[o];
        for (uint32_t o = 0u; o < SYSMON_BUDDY_ORDERS; ++o)
        {
            const uint32_t bh = ((pmm_h - 60u) * sm_log_ratio(g_now.buddy[o], maxb)) / 1000u;
            sm_bar_v(left_x + 8u + o * bw, base_y, bw - 2u, bh,
                     sm_lerp(sm_rgb(60u, 120u, 240u), sm_rgb(120u, 240u, 180u), (o * 1000u) / SYSMON_BUDDY_ORDERS));
        }
        if ((g_now.uaf_anomalies | g_now.double_free) && (frame & 8u))
            sm_fill(left_x + col_w - 18u, y0 + 8u, 10u, 10u, sm_rgb(255u, 40u, 40u));
    }

    /* --- Heap size-classes (right top) --- */
    const uint32_t heap_h = row_h;
    sm_panel(right_x, y0, col_w, heap_h, sm_rgb(200u, 160u, 60u));
    {
        const uint32_t base_y = y0 + heap_h - 14u;
        const uint32_t bw = (col_w - 16u) / SYSMON_SIZE_CLASSES;
        uint32_t maxc = 1u;
        for (uint32_t c = 0u; c < SYSMON_SIZE_CLASSES; ++c)
            if (g_now.heap_size_class[c] > maxc)
                maxc = g_now.heap_size_class[c];
        for (uint32_t c = 0u; c < SYSMON_SIZE_CLASSES; ++c)
        {
            const uint32_t bh = ((heap_h - 40u) * sm_log_ratio(g_now.heap_size_class[c], maxc)) / 1000u;
            sm_bar_v(right_x + 8u + c * bw, base_y, bw - 2u, bh, sm_rgb(235u, 190u, 70u));
        }
        for (uint32_t d = 0u; d < sm_clampu(g_now.heap_hot_loop_depth, 0u, 8u); ++d)
            sm_fill(right_x + col_w - 18u, y0 + 8u + d * 6u, 10u, 4u,
                    (frame & 4u) ? sm_rgb(255u, 120u, 40u) : sm_rgb(160u, 70u, 20u));
    }

    /* --- Allocator occupancy gauges (left), spread to fill the tall panel --- */
    const uint32_t gauge_h = 16u;
    const uint32_t gx = left_x + 8u;
    const uint32_t gw = col_w - 16u;
    const uint32_t row_step = (row_h - 30u) / 4u; /* shared vertical step for row-2 panels */
    sm_panel(left_x, y2, col_w, row_h, sm_rgb(120u, 160u, 240u));
    {
        uint32_t gy = y2 + 22u;
        sm_gauge_h(gx, gy, gw, gauge_h, sm_log_ratio(g_now.arena_used, g_now.arena_cap ? g_now.arena_cap : 1u),
                   sm_log_ratio(g_now.arena_peak, g_now.arena_cap ? g_now.arena_cap : 1u));
        gy += row_step;
        const uint32_t pool_used = (g_now.pool_cap > g_now.pool_free) ? (g_now.pool_cap - g_now.pool_free) : 0u;
        sm_gauge_h(gx, gy, gw, gauge_h, sm_log_ratio(pool_used, g_now.pool_cap ? g_now.pool_cap : 1u),
                   sm_log_ratio(g_now.pool_peak, g_now.pool_cap ? g_now.pool_cap : 1u));
        gy += row_step;
        sm_gauge_h(gx, gy, gw, gauge_h, sm_log_ratio(g_now.stack_used, g_now.stack_cap ? g_now.stack_cap : 1u),
                   sm_log_ratio(g_now.stack_peak, g_now.stack_cap ? g_now.stack_cap : 1u));
        gy += row_step;
        const uint32_t live_pinned =
            (g_now.pinned_allocated > g_now.pinned_released) ? (g_now.pinned_allocated - g_now.pinned_released) : 0u;
        sm_gauge_h(gx, gy, gw, gauge_h, sm_log_ratio(live_pinned, 64u), 0u);
    }

    /* --- Flow lanes: 4 animated data-transfer pipes (right) --- */
    sm_panel(right_x, y2, col_w, row_h, sm_rgb(120u, 240u, 200u));
    {
        const uint32_t lane_x = right_x + 8u;
        const uint32_t lane_w = col_w - 16u;
        const uint32_t lane0 = y2 + 26u;

        /* ring fill level above the lanes */
        sm_gauge_h(lane_x, y2 + 10u, lane_w, 6u, sm_log_ratio(g_now.ring_count, g_now.ring_cap ? g_now.ring_cap : 1u),
                   sm_log_ratio(g_now.ring_high, g_now.ring_cap ? g_now.ring_cap : 1u));

        const uint32_t enq_delta = (g_now.ring_enqueue - g_prev.ring_enqueue);
        const uint32_t deq_delta = (g_now.ring_dequeue - g_prev.ring_dequeue);
        const uint32_t gpu_delta = (g_now.pinned_allocated - g_prev.pinned_allocated);
        const uint32_t heap_delta = (g_now.heap_small_free_blocks > g_prev.heap_small_free_blocks) ?
                                        (g_now.heap_small_free_blocks - g_prev.heap_small_free_blocks) :
                                        (g_prev.heap_small_free_blocks - g_now.heap_small_free_blocks);
        sysmon_flow_spawn(0u, enq_delta ? enq_delta : ((frame % 20u == 0u) ? 1u : 0u), 14u);
        sysmon_flow_spawn(1u, deq_delta ? deq_delta : ((frame % 26u == 0u) ? 1u : 0u), 12u);
        sysmon_flow_spawn(2u, gpu_delta ? gpu_delta : ((frame % 45u == 0u) ? 1u : 0u), 9u);
        sysmon_flow_spawn(3u, heap_delta ? heap_delta : ((frame % 33u == 0u) ? 1u : 0u), 7u);

        const uint32_t lane_color[SYSMON_FLOW_LANES] = {sm_rgb(80u, 220u, 255u), sm_rgb(80u, 160u, 230u),
                                                        sm_rgb(120u, 255u, 150u), sm_rgb(235u, 190u, 70u)};
        for (uint32_t l = 0u; l < SYSMON_FLOW_LANES; ++l)
        {
            const uint32_t ly = lane0 + l * row_step;
            sm_hline(lane_x, lane_x + lane_w - 1u, ly + 6u, sm_rgb(36u, 50u, 64u));
            sysmon_flow_advance_and_draw(l, lane_x, ly, lane_w, lane_color[l]);
        }
    }

    /* --- PCI bus --- */
    sm_hline(margin, W - margin, bus_y, accent);
    {
        const uint32_t n = sm_clampu(g_now.pci_devices, 0u, 48u);
        for (uint32_t i = 0u; i < n; ++i)
        {
            const PeripheralComponentInterconnectDevice_t *d = peripheral_component_interconnect_get_device(i);
            const uint32_t nx = margin + ((W - margin * 2u) * i) / (n ? n : 1u);
            const uint8_t cls = (d != 0) ? d->class_code : 0u;
            const uint32_t c = sm_lerp(sm_rgb(90u, 160u, 255u), sm_rgb(255u, 140u, 90u), (cls * 1000u) / 256u);
            const uint32_t pulse = 4u + ((frame + i * 7u) % 6u);
            sm_vline(nx, bus_y - pulse, bus_y, c);
            sm_fill(nx - 2u, bus_y - pulse - 4u, 5u, 5u, c);
        }
    }

    /* --- Text HUD (always on top) --- */
    {
        const uint32_t hud = sm_rgb(180u, 200u, 235u);
        const uint32_t used_pages = (g_baseline_pages > g_now.free_pages) ? (g_baseline_pages - g_now.free_pages) : 0u;
        const uint32_t live_pinned =
            (g_now.pinned_allocated > g_now.pinned_released) ? (g_now.pinned_allocated - g_now.pinned_released) : 0u;

        sm_text(margin, 9u, "LPLKERNEL SYSMON", sm_rgb(255u, 255u, 255u), 1u);
        sm_text(W - margin - 8u * 13u, 9u, "PRESS ANY KEY", sm_rgb(120u, 140u, 180u), 1u);

        sm_label_value(left_x + 8u, y0 + 3u, "PMM USED", used_pages, hud);
        sm_label_value(right_x + 8u, y0 + 3u, "HEAP BLK", g_now.heap_small_free_blocks, hud);
        sm_label_value(left_x + 8u, y2 - 4u, "ARENA  B", g_now.arena_used, hud);
        sm_label_value(right_x + 8u, y2 - 4u, "RING/GPU", g_now.ring_count + live_pinned, hud);
        sm_label_value(margin, bus_y - 22u, "PCI DEV ", g_now.pci_devices, hud);
    }
}

/* Frame pacing — spin until the kernel tick advances (watchdog-bounded). */
static void sysmon_pace(void)
{
    const uint32_t start = clock_get_tick_count();
    for (volatile uint32_t i = 0u; i < SYSMON_SPIN_CAP; ++i)
        if (clock_get_tick_count() != start)
            return;
}

void kernel_sysmon_run(Serial_t *com1)
{
    hal_surface_descriptor_t surface;
    if (!hal_display_available() || !hal_display_query_surface(&surface) || surface.buffer == 0 ||
        surface.bits_per_pixel != 32u || surface.width < 320u || surface.height < 240u)
    {
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: sysmon: no usable 32bpp display, skipped\n");
        return;
    }

    g_surface = surface.buffer;
    g_surf_pitch_px = surface.pitch / 4u;
    g_w = surface.width;
    g_h = surface.height;

    /* Try to acquire a tightly-packed back buffer for double buffering; fall
       back to drawing straight into the scanout if the allocation fails. */
    g_back = (uint32_t *) kmalloc((size_t) g_w * (size_t) g_h * sizeof(uint32_t));
    if (g_back != 0)
    {
        g_buf = g_back;
        g_pitch_px = g_w;
    }
    else
    {
        g_buf = g_surface;
        g_pitch_px = g_surf_pitch_px;
    }

    serial_write_string(com1, g_back ? "[" KERNEL_SYSTEM_STRING "]: sysmon live (double-buffered, press any key)\n" :
                                       "[" KERNEL_SYSTEM_STRING "]: sysmon live (direct, press any key)\n");

    for (uint32_t lane = 0u; lane < SYSMON_FLOW_LANES; ++lane)
        for (uint32_t i = 0u; i < SYSMON_FLOW_PARTICLES; ++i)
            g_flow[lane][i].active = 0u;

    sysmon_sample(&g_now);
    g_prev = g_now;
    g_baseline_pages = g_now.free_pages ? g_now.free_pages : 1u;

    char key;
    uint32_t frame = 0u;
    while (!keyboard_try_pop_char(&key))
    {
        g_prev = g_now;
        sysmon_sample(&g_now);
        if (g_now.free_pages > g_baseline_pages)
            g_baseline_pages = g_now.free_pages;
        sysmon_draw_frame(frame);
        sysmon_blit_present();
        ++frame;
        sysmon_pace();
    }

    if (g_back != 0)
    {
        kfree(g_back);
        g_back = 0;
    }
    serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: sysmon exited\n");
}
