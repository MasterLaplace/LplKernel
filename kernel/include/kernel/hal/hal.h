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
 * contract, drain the input ring, write diagnostic text, and hand out pinned
 * graphics memory. All renderer/scene/ECS logic stays engine-side.
 *
 * Groups (one per driver family, each mirrored by an lpl::platform backend that
 * KernelPlatform aggregates):
 *
 *   hardware_abstraction_layer_display_*         -> IDisplayBackend
 *   hardware_abstraction_layer_clock_*           -> IClockBackend
 *   hardware_abstraction_layer_input_*           -> IInputBackend
 *   hardware_abstraction_layer_graphics_memory_* -> IGpuMemoryBackend
 *   hardware_abstraction_layer_console_*         -> core::ILogger (KernelLogger)
 *   hardware_abstraction_layer_virtio_gpu_*      -> internal to the display group
 *
 * Rules for anything added here:
 *   - Drivers only. If it can be computed engine-side, it does not belong.
 *     The console group writes text; it does not draw glyphs (the font and the
 *     text blitter live in lpl::image, because a Linux host has no HAL at all).
 *   - stdint/stdbool types only, so the engine never includes kernel headers.
 *   - No allocation policy. The engine reaches the kernel heap through
 *     lpl::pmr::malloc -> kmalloc, which currently bypasses this seam; a
 *     hardware_abstraction_layer_memory_* group + IMemoryBackend is the known
 *     gap, deliberately left open (the arena seam that would use it is a
 *     separate piece of work).
 *   - Identifiers spell acronyms out in full, per the project convention; the
 *     file names (hal.h, hal_*.c) and the include guards keep the short form.
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
} hardware_abstraction_layer_surface_descriptor_t;

/**
 * @brief Query the active display surface.
 * @return true if a framebuffer surface is available (descriptor filled).
 */
bool hardware_abstraction_layer_display_query_surface(hardware_abstraction_layer_surface_descriptor_t *out_descriptor);

/**
 * @brief True when any display backend (virtio-gpu scanout or software LFB) can
 *        present, i.e. hardware_abstraction_layer_display_query_surface() would succeed.
 */
bool hardware_abstraction_layer_display_available(void);

/** @brief Clear the whole surface to an 0x00RRGGBB color. */
void hardware_abstraction_layer_display_clear(uint32_t color_rgb);

/** @brief Read back one pixel as 0x00RRGGBB (0 if no surface / out of range). */
uint32_t hardware_abstraction_layer_display_read_pixel(uint32_t x, uint32_t y);

/**
 * @brief Present the back buffer (atomic flip / scanout).
 *
 * The software-LFB path renders straight into scanout memory, so present is a
 * no-op today; it exists so the VirtIO-GPU backend can slot in behind the same
 * contract without touching the engine.
 */
void hardware_abstraction_layer_display_present(void);

/* ----------------------------------------------------------------------------
 * Clock (tick contract + sub-tick timestamp)
 * ------------------------------------------------------------------------- */

/** @brief Monotonic tick count (wraps; consumers use modular deltas). */
uint32_t hardware_abstraction_layer_clock_tick_count(void);

/** @brief Tick frequency in Hz. */
uint32_t hardware_abstraction_layer_clock_tick_hertz(void);

/** @brief 64-bit CPU timestamp counter (rdtsc) for sub-tick timing. */
uint64_t hardware_abstraction_layer_clock_timestamp_counter(void);

/* ----------------------------------------------------------------------------
 * Input (decoded-character ring drained by the engine)
 * ------------------------------------------------------------------------- */

/**
 * @brief Pop one decoded character from the input ring.
 * @return true if a character was popped into @p out_character.
 */
bool hardware_abstraction_layer_input_try_pop_character(char *out_character);

/** @brief Number of decoded characters currently waiting in the ring. */
uint32_t hardware_abstraction_layer_input_pending_count(void);

/* ----------------------------------------------------------------------------
 * Console (diagnostic text sink)
 *
 * The engine's logger (lpl::core::ILogger) is routed here on the kernel target,
 * so engine-side log calls reach the same serial console the kernel writes to.
 * Text only: this is a diagnostic sink, not a rendering path — glyphs and
 * overlays stay engine-side.
 * ------------------------------------------------------------------------- */

/** @brief Write a NUL-terminated string to the kernel diagnostic console. */
void hardware_abstraction_layer_console_write_string(const char *text);

/* ----------------------------------------------------------------------------
 * Graphics memory (pinned, never-relocated; GPU-attach ready)
 * ------------------------------------------------------------------------- */

/** @brief Allocate pinned (never-relocated) graphics memory; NULL on failure. */
void *hardware_abstraction_layer_graphics_memory_allocate(uint32_t size_bytes);

/** @brief Release graphics memory obtained from hardware_abstraction_layer_graphics_memory_allocate. */
void hardware_abstraction_layer_graphics_memory_free(void *pointer, uint32_t size_bytes);

/**
 * @brief Resolve the physical address of a graphics-memory virtual address.
 * @return true if the translation succeeded (out_physical_address filled).
 */
bool hardware_abstraction_layer_graphics_memory_physical_address(const void *virtual_address,
                                                                 uint32_t *out_physical_address);

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
    bool present;           /* a virtio-gpu function was found              */
    uint8_t bus;            /* PCI bus of the found function                */
    uint8_t device;         /* PCI device (slot)                           */
    uint8_t function;       /* PCI function                                */
    uint16_t device_id;     /* PCI device id (0x1050 modern / 0x1010 xitnl) */
    uint8_t is_modern;      /* device_id >= 0x1040 (modern virtio-pci)     */
    uint32_t mmio_base;     /* decoded MMIO BAR base (0 when none)         */
    uint32_t mmio_size;     /* MMIO BAR size in bytes (0 when none)        */
    uint8_t mmio_bar_index; /* which BAR slot the MMIO region came from    */
} hardware_abstraction_layer_virtio_gpu_info_t;

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
bool hardware_abstraction_layer_virtio_gpu_probe(hardware_abstraction_layer_virtio_gpu_info_t *out_info);

/* virtio-pci capability cfg_type values (from the virtio 1.x spec). */
#define HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_COMMON_CFG 1u /* common configuration               */
#define HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_NOTIFY_CFG 2u /* notification area                  */
#define HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_ISR_CFG    3u /* ISR status                         */
#define HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_DEVICE_CFG 4u /* device-specific configuration      */
#define HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_PCI_CFG    5u /* alternate PCI-config access window  */

/** @brief One decoded virtio-pci capability structure location (within a BAR). */
typedef struct {
    uint8_t present; /* non-zero when this cfg_type capability was found */
    uint8_t bar;     /* which BAR holds the structure                   */
    uint32_t offset; /* byte offset of the structure within the BAR     */
    uint32_t length; /* length of the structure in bytes                */
} hardware_abstraction_layer_virtio_pci_cap_t;

/**
 * @brief Mapped virtio-pci configuration windows for a virtio-gpu function.
 *
 * The capability list is walked to locate the common/notify/isr/device cfg
 * structures, and the MMIO BAR backing them is mapped into kernel virtual
 * space (cache-disabled). Each structure is then reachable at
 * @ref mmio_virtual_base + cap.offset (when cap.bar == @ref mmio_bar_index).
 */
typedef struct {
    uint8_t mapped;                 /* non-zero when the MMIO window was mapped     */
    uint8_t mmio_bar_index;         /* BAR slot that was mapped                     */
    uint32_t mmio_virtual_base;     /* kernel VA of the mapped BAR window           */
    uint32_t mmio_physical_base;    /* physical base of the mapped BAR window       */
    uint32_t mmio_size;             /* size of the mapped window in bytes           */
    uint32_t notify_off_multiplier; /* notify capability multiplier            */
    hardware_abstraction_layer_virtio_pci_cap_t common;
    hardware_abstraction_layer_virtio_pci_cap_t notify;
    hardware_abstraction_layer_virtio_pci_cap_t isr;
    hardware_abstraction_layer_virtio_pci_cap_t device;
} hardware_abstraction_layer_virtio_gpu_mapping_t;

/**
 * @brief Walk the virtio-pci capability list and map the device MMIO window.
 *
 * Reads the PCI capability list for the function described by @p info, records
 * the common/notify/isr/device cfg structure locations, and maps the MMIO BAR
 * those structures live in into kernel virtual space. Requires @p info from a
 * successful hardware_abstraction_layer_virtio_gpu_probe().
 *
 * @param info Probe result identifying the virtio-gpu function.
 * @param out_mapping Destination for the decoded + mapped configuration.
 * @return true when the common cfg was found and the BAR was mapped.
 */
bool hardware_abstraction_layer_virtio_gpu_map(const hardware_abstraction_layer_virtio_gpu_info_t *info,
                                              hardware_abstraction_layer_virtio_gpu_mapping_t *out_mapping);

/* virtio-gpu always exposes exactly two virtqueues: controlq + cursorq. */
#define HARDWARE_ABSTRACTION_LAYER_VIRTIO_GPU_MAX_QUEUES 2u

/** @brief Result of the virtio device bring-up handshake. */
typedef struct {
    uint8_t ready;                                  /* non-zero when FEATURES_OK stuck (device usable) */
    uint8_t device_status;                          /* final device_status register value             */
    uint16_t num_queues;                            /* number of virtqueues the device exposes        */
    uint32_t mmio_virtual_base;                     /* echoed from the mapping                 */
    uint32_t common_cfg_address;                    /* VA of the virtio_pci_common_cfg         */
    uint16_t queue_size[HARDWARE_ABSTRACTION_LAYER_VIRTIO_GPU_MAX_QUEUES]; /* size of queues 0..1     */
} hardware_abstraction_layer_virtio_gpu_device_t;

/**
 * @brief Run the virtio device-initialisation handshake on a mapped device.
 *
 * Resets the device, sets ACKNOWLEDGE | DRIVER, negotiates features (accepting
 * VIRTIO_F_VERSION_1 only), commits FEATURES_OK and verifies it stuck, then
 * reads num_queues and the size of each virtqueue. DRIVER_OK is deferred until
 * the virtqueues are allocated and programmed (a later slice).
 *
 * @param mapping A mapped device from hardware_abstraction_layer_virtio_gpu_map().
 * @param out_device Destination for the bring-up result.
 * @return true when the device accepted the driver and FEATURES_OK stuck.
 */
bool hardware_abstraction_layer_virtio_gpu_bringup(const hardware_abstraction_layer_virtio_gpu_mapping_t *mapping,
                                                  hardware_abstraction_layer_virtio_gpu_device_t *out_device);

/** @brief A programmed split virtqueue (descriptor table + avail + used ring). */
typedef struct {
    uint8_t ready;               /* non-zero once enabled on the device         */
    uint16_t queue_index;        /* which virtqueue this is                     */
    uint16_t queue_size;         /* number of descriptors                       */
    uint16_t last_used_index;    /* consumer cursor into the used ring          */
    uint16_t free_head;          /* next free descriptor index                  */
    uint32_t desc_address;       /* VA of the descriptor table                  */
    uint32_t avail_address;      /* VA of the available ring                    */
    uint32_t used_address;       /* VA of the used ring                         */
    uint32_t notify_address;     /* VA to write queue_index into to notify      */
    uint32_t ring_physical_base; /* physical base of the backing page         */
    void *ring_backing;          /* VA of the backing page (for free)           */
} hardware_abstraction_layer_virtio_virtqueue_t;

/**
 * @brief Allocate and program a split virtqueue, enabling it on the device.
 *
 * Selects @p queue_index, backs the descriptor/avail/used rings with one pinned
 * (physically contiguous) zeroed page, programs queue_desc/driver/device with
 * their physical addresses, and sets queue_enable. The whole ring set must fit
 * in a single 4 KiB page (true for the virtio-gpu controlq/cursorq sizes).
 *
 * @param device A device brought up by hardware_abstraction_layer_virtio_gpu_bringup().
 * @param mapping The mapping the device was brought up from (for the notify BAR).
 * @param queue_index Virtqueue to program (0 = controlq, 1 = cursorq).
 * @param out_queue Destination for the programmed virtqueue.
 * @return true when the queue was allocated, programmed and enabled.
 */
bool hardware_abstraction_layer_virtio_gpu_setup_queue(const hardware_abstraction_layer_virtio_gpu_device_t *device,
                                                      const hardware_abstraction_layer_virtio_gpu_mapping_t *mapping,
                                uint16_t queue_index, hardware_abstraction_layer_virtio_virtqueue_t *out_queue);

/**
 * @brief Set DRIVER_OK, completing device initialisation (queues must be set).
 * @return the device_status read back after the write.
 */
uint8_t hardware_abstraction_layer_virtio_gpu_driver_ok(const hardware_abstraction_layer_virtio_gpu_device_t *device);

/** @brief Decoded VIRTIO_GPU_CMD_GET_DISPLAY_INFO response (scanout 0). */
typedef struct {
    uint32_t response_type; /* control-header type (0x1101 = OK_DISPLAY_INFO) */
    uint32_t enabled;       /* non-zero when scanout 0 is enabled             */
    uint32_t width;         /* scanout 0 preferred width                      */
    uint32_t height;        /* scanout 0 preferred height                     */
} hardware_abstraction_layer_virtio_gpu_display_info_t;

/**
 * @brief Issue VIRTIO_GPU_CMD_GET_DISPLAY_INFO and read back scanout 0.
 *
 * Builds a request/response descriptor chain on @p queue, publishes it to the
 * available ring, rings the notify doorbell, polls the used ring for
 * completion, and decodes the first scanout's geometry. This is the first full
 * round-trip over the virtqueue and the template the 2D lifecycle reuses.
 *
 * @param queue A programmed controlq from hardware_abstraction_layer_virtio_gpu_setup_queue().
 * @param out_info Destination for the decoded display info.
 * @return true when the command completed and the response was OK_DISPLAY_INFO.
 */
bool hardware_abstraction_layer_virtio_gpu_get_display_info(
    hardware_abstraction_layer_virtio_virtqueue_t *queue,
    hardware_abstraction_layer_virtio_gpu_display_info_t *out_info);

/**
 * @brief A live virtio-gpu scanout: a host 2D resource bound to a display.
 *
 * @ref framebuffer is a guest-owned BGRX (32bpp) surface the caller draws into;
 * hardware_abstraction_layer_virtio_gpu_flush() pushes its contents to the host and presents them.
 */
typedef struct {
    uint8_t ready;                    /* non-zero when the scanout is bound + presentable */
    uint32_t resource_id;             /* host resource id                                 */
    uint32_t scanout_id;              /* display index this resource is bound to          */
    uint32_t width;                   /* surface width in pixels                          */
    uint32_t height;                  /* surface height in pixels                         */
    uint32_t *framebuffer;            /* guest BGRX surface (width*height pixels)          */
    uint32_t framebuffer_size;        /* surface size in bytes                            */
    hardware_abstraction_layer_virtio_virtqueue_t *queue;    /* controlq used for present commands          */
    void *command_buffer;             /* internal scratch (request/response)              */
    uint32_t command_buffer_physical; /* cached physical base of command_buffer    */
} hardware_abstraction_layer_virtio_gpu_scanout_t;

/**
 * @brief Create a 2D host resource, back it with guest memory, and bind it to a
 *        scanout (RESOURCE_CREATE_2D + RESOURCE_ATTACH_BACKING + SET_SCANOUT).
 *
 * Allocates a guest BGRX framebuffer, attaches it to the host resource via a
 * coalesced scatter-gather page list, and binds the resource to scanout 0. On
 * success the caller draws into @ref hardware_abstraction_layer_virtio_gpu_scanout_t::framebuffer and
 * calls hardware_abstraction_layer_virtio_gpu_flush() to present.
 *
 * @param queue A programmed controlq from hardware_abstraction_layer_virtio_gpu_setup_queue().
 * @param width Surface width in pixels.
 * @param height Surface height in pixels.
 * @param out_scanout Destination for the created scanout.
 * @return true when the resource was created, backed and bound.
 */
bool hardware_abstraction_layer_virtio_gpu_create_scanout(hardware_abstraction_layer_virtio_virtqueue_t *queue,
                                                         uint32_t width, uint32_t height,
                                   hardware_abstraction_layer_virtio_gpu_scanout_t *out_scanout);

/**
 * @brief Present the current framebuffer contents (TRANSFER_TO_HOST_2D +
 *        RESOURCE_FLUSH) for the whole surface.
 * @return true when both commands completed with OK_NODATA.
 */
bool hardware_abstraction_layer_virtio_gpu_flush(hardware_abstraction_layer_virtio_gpu_scanout_t *scanout);

/* ----------------------------------------------------------------------------
 * Persistent virtio-gpu display routing (kernel-internal)
 *
 * hardware_abstraction_layer_virtio_gpu_display_init() runs the whole probe -> bring-up -> virtqueue ->
 * scanout setup once and stashes the live scanout in static state. hardware_abstraction_layer_display
 * routes its surface/clear/read/present through these when a scanout is active,
 * and falls back to the software-LFB framebuffer otherwise. The engine sees
 * only the stable hardware_abstraction_layer_display_* contract and never knows which backend won.
 * ------------------------------------------------------------------------- */

/** @brief Bring up a virtio-gpu scanout for hardware_abstraction_layer_display; false if unavailable. */
bool hardware_abstraction_layer_virtio_gpu_display_init(void);

/** @brief True when a virtio-gpu scanout is live (hardware_abstraction_layer_display should route here). */
bool hardware_abstraction_layer_virtio_gpu_display_active(void);

/** @brief Fill @p out_descriptor from the active scanout surface. */
bool hardware_abstraction_layer_virtio_gpu_display_query(
    hardware_abstraction_layer_surface_descriptor_t *out_descriptor);

/** @brief Clear the scanout surface to a packed 0x00RRGGBB color (no present). */
void hardware_abstraction_layer_virtio_gpu_display_clear(uint32_t color_rgb);

/** @brief Read back one scanout pixel as 0x00RRGGBB. */
uint32_t hardware_abstraction_layer_virtio_gpu_display_read_pixel(uint32_t x, uint32_t y);

/** @brief Present the scanout (TRANSFER_TO_HOST_2D + RESOURCE_FLUSH). */
void hardware_abstraction_layer_virtio_gpu_display_present(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_HAL_HAL_H */
