/**
 * @file hal.h
 * @brief Thin C ABI hardware-abstraction layer for the freestanding engine.
 *
 * This is the kernel-side platform seam consumed by libengine's kernel
 * platform backends (lpl::platform). It is the mirror of libengine.h: where
 * libengine.h is the engine->kernel surface, hal.h is the kernel->engine
 * surface — the "GLFW + DRM/KMS + clock" equivalent collapsed into a stable
 * C contract. It exposes ONLY stdint/stdbool types so the engine never has to
 * include kernel-internal headers; the kernel implements these shims over its
 * framebuffer / clock / keyboard / pinned-memory drivers.
 *
 * The HAL is deliberately tiny: present a surface, read the tick/timestamp
 * contract, drain the input ring, and hand out pinned graphics memory. All
 * renderer/scene/ECS logic stays engine-side.
 */
#ifndef KERNEL_HAL_HAL_H
#define KERNEL_HAL_HAL_H

#ifndef __cplusplus
#    include <stdbool.h>
#endif
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Display (surface / present)
 * ------------------------------------------------------------------------- */

/** @brief Linear-framebuffer surface description (KMS-like, no ownership). */
typedef struct {
    uint32_t *buffer;          /* virtual address of the framebuffer        */
    uint32_t physical_address; /* physical address (for GPU attach later)   */
    uint32_t width;            /* visible width in pixels                   */
    uint32_t height;           /* visible height in pixels                  */
    uint32_t pitch;            /* bytes per scanline                        */
    uint8_t bits_per_pixel;    /* bits per pixel                            */
} hal_surface_descriptor_t;

/**
 * @brief Query the active display surface.
 * @return true if a framebuffer surface is available (descriptor filled).
 */
bool hal_display_query_surface(hal_surface_descriptor_t *out_descriptor);

/** @brief Clear the whole surface to an 0x00RRGGBB color. */
void hal_display_clear(uint32_t color_rgb);

/** @brief Read back one pixel as 0x00RRGGBB (0 if no surface / out of range). */
uint32_t hal_display_read_pixel(uint32_t x, uint32_t y);

/**
 * @brief Present the back buffer (atomic flip / scanout).
 *
 * The software-LFB path renders straight into scanout memory, so present is a
 * no-op today; it exists so the VirtIO-GPU backend can slot in behind the same
 * contract without touching the engine.
 */
void hal_display_present(void);

/* ----------------------------------------------------------------------------
 * Clock (tick contract + sub-tick timestamp)
 * ------------------------------------------------------------------------- */

/** @brief Monotonic tick count (wraps; consumers use modular deltas). */
uint32_t hal_clock_tick_count(void);

/** @brief Tick frequency in Hz. */
uint32_t hal_clock_tick_hertz(void);

/** @brief 64-bit CPU timestamp counter (rdtsc) for sub-tick timing. */
uint64_t hal_clock_timestamp_counter(void);

/* ----------------------------------------------------------------------------
 * Input (decoded-character ring drained by the engine)
 * ------------------------------------------------------------------------- */

/**
 * @brief Pop one decoded character from the input ring.
 * @return true if a character was popped into @p out_character.
 */
bool hal_input_try_pop_character(char *out_character);

/** @brief Number of decoded characters currently waiting in the ring. */
uint32_t hal_input_pending_count(void);

/* ----------------------------------------------------------------------------
 * Graphics memory (pinned, never-relocated; GPU-attach ready)
 * ------------------------------------------------------------------------- */

/** @brief Allocate pinned (never-relocated) graphics memory; NULL on failure. */
void *hal_graphics_memory_allocate(uint32_t size_bytes);

/** @brief Release graphics memory obtained from hal_graphics_memory_allocate. */
void hal_graphics_memory_free(void *pointer, uint32_t size_bytes);

/**
 * @brief Resolve the physical address of a graphics-memory virtual address.
 * @return true if the translation succeeded (out_physical_address filled).
 */
bool hal_graphics_memory_physical_address(const void *virtual_address, uint32_t *out_physical_address);

/* ----------------------------------------------------------------------------
 * VirtIO-GPU discovery (P4 display-backend hardening)
 *
 * Probe-only for now: locate a virtio-gpu PCI function and decode its MMIO BAR
 * so a later display backend can map the device and run the 2D lifecycle. The
 * full driver (virtqueues, RESOURCE_CREATE_2D / ATTACH_BACKING / SET_SCANOUT /
 * TRANSFER_TO_HOST / RESOURCE_FLUSH) lands on top of this discovery seam.
 * ------------------------------------------------------------------------- */

/** @brief Result of a virtio-gpu PCI probe. */
typedef struct {
    bool present;            /* a virtio-gpu function was found              */
    uint8_t bus;             /* PCI bus of the found function                */
    uint8_t device;          /* PCI device (slot)                           */
    uint8_t function;        /* PCI function                                */
    uint16_t device_id;      /* PCI device id (0x1050 modern / 0x1010 xitnl) */
    uint8_t is_modern;       /* device_id >= 0x1040 (modern virtio-pci)     */
    uint32_t mmio_base;      /* decoded MMIO BAR base (0 when none)         */
    uint32_t mmio_size;      /* MMIO BAR size in bytes (0 when none)        */
    uint8_t mmio_bar_index;  /* which BAR slot the MMIO region came from    */
} hal_virtio_gpu_info_t;

/**
 * @brief Scan the enumerated PCI devices for a virtio-gpu function.
 *
 * Matches Red Hat / virtio vendor id 0x1AF4 with the GPU device ids (modern
 * 0x1050 or transitional 0x1010), and decodes the first MMIO BAR. Requires a
 * prior peripheral_component_interconnect_scan().
 *
 * @param out_info Destination for the probe result (present=false when none).
 * @return true when a virtio-gpu function was found.
 */
bool hal_virtio_gpu_probe(hal_virtio_gpu_info_t *out_info);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_HAL_HAL_H */
