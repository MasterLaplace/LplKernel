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

/* virtio-pci capability cfg_type values (from the virtio 1.x spec). */
#define HAL_VIRTIO_PCI_CAP_COMMON_CFG 1u /* common configuration               */
#define HAL_VIRTIO_PCI_CAP_NOTIFY_CFG 2u /* notification area                  */
#define HAL_VIRTIO_PCI_CAP_ISR_CFG    3u /* ISR status                         */
#define HAL_VIRTIO_PCI_CAP_DEVICE_CFG 4u /* device-specific configuration      */
#define HAL_VIRTIO_PCI_CAP_PCI_CFG    5u /* alternate PCI-config access window  */

/** @brief One decoded virtio-pci capability structure location (within a BAR). */
typedef struct {
    uint8_t present;  /* non-zero when this cfg_type capability was found */
    uint8_t bar;      /* which BAR holds the structure                   */
    uint32_t offset;  /* byte offset of the structure within the BAR     */
    uint32_t length;  /* length of the structure in bytes                */
} hal_virtio_pci_cap_t;

/**
 * @brief Mapped virtio-pci configuration windows for a virtio-gpu function.
 *
 * The capability list is walked to locate the common/notify/isr/device cfg
 * structures, and the MMIO BAR backing them is mapped into kernel virtual
 * space (cache-disabled). Each structure is then reachable at
 * @ref mmio_virtual_base + cap.offset (when cap.bar == @ref mmio_bar_index).
 */
typedef struct {
    uint8_t mapped;             /* non-zero when the MMIO window was mapped     */
    uint8_t mmio_bar_index;     /* BAR slot that was mapped                     */
    uint32_t mmio_virtual_base; /* kernel VA of the mapped BAR window           */
    uint32_t mmio_physical_base;/* physical base of the mapped BAR window       */
    uint32_t mmio_size;         /* size of the mapped window in bytes           */
    uint32_t notify_off_multiplier; /* notify capability multiplier            */
    hal_virtio_pci_cap_t common;
    hal_virtio_pci_cap_t notify;
    hal_virtio_pci_cap_t isr;
    hal_virtio_pci_cap_t device;
} hal_virtio_gpu_mapping_t;

/**
 * @brief Walk the virtio-pci capability list and map the device MMIO window.
 *
 * Reads the PCI capability list for the function described by @p info, records
 * the common/notify/isr/device cfg structure locations, and maps the MMIO BAR
 * those structures live in into kernel virtual space. Requires @p info from a
 * successful hal_virtio_gpu_probe().
 *
 * @param info Probe result identifying the virtio-gpu function.
 * @param out_mapping Destination for the decoded + mapped configuration.
 * @return true when the common cfg was found and the BAR was mapped.
 */
bool hal_virtio_gpu_map(const hal_virtio_gpu_info_t *info, hal_virtio_gpu_mapping_t *out_mapping);

/* virtio-gpu always exposes exactly two virtqueues: controlq + cursorq. */
#define HAL_VIRTIO_GPU_MAX_QUEUES 2u

/** @brief Result of the virtio device bring-up handshake. */
typedef struct {
    uint8_t ready;          /* non-zero when FEATURES_OK stuck (device usable) */
    uint8_t device_status;  /* final device_status register value             */
    uint16_t num_queues;    /* number of virtqueues the device exposes        */
    uint32_t mmio_virtual_base;     /* echoed from the mapping                 */
    uint32_t common_cfg_address;    /* VA of the virtio_pci_common_cfg         */
    uint16_t queue_size[HAL_VIRTIO_GPU_MAX_QUEUES]; /* size of queues 0..1     */
} hal_virtio_gpu_device_t;

/**
 * @brief Run the virtio device-initialisation handshake on a mapped device.
 *
 * Resets the device, sets ACKNOWLEDGE | DRIVER, negotiates features (accepting
 * VIRTIO_F_VERSION_1 only), commits FEATURES_OK and verifies it stuck, then
 * reads num_queues and the size of each virtqueue. DRIVER_OK is deferred until
 * the virtqueues are allocated and programmed (a later slice).
 *
 * @param mapping A mapped device from hal_virtio_gpu_map().
 * @param out_device Destination for the bring-up result.
 * @return true when the device accepted the driver and FEATURES_OK stuck.
 */
bool hal_virtio_gpu_bringup(const hal_virtio_gpu_mapping_t *mapping, hal_virtio_gpu_device_t *out_device);

/** @brief A programmed split virtqueue (descriptor table + avail + used ring). */
typedef struct {
    uint8_t ready;             /* non-zero once enabled on the device         */
    uint16_t queue_index;      /* which virtqueue this is                     */
    uint16_t queue_size;       /* number of descriptors                       */
    uint16_t last_used_index;  /* consumer cursor into the used ring          */
    uint16_t free_head;        /* next free descriptor index                  */
    uint32_t desc_address;     /* VA of the descriptor table                  */
    uint32_t avail_address;    /* VA of the available ring                    */
    uint32_t used_address;     /* VA of the used ring                         */
    uint32_t notify_address;   /* VA to write queue_index into to notify      */
    uint32_t ring_physical_base; /* physical base of the backing page         */
    void *ring_backing;        /* VA of the backing page (for free)           */
} hal_virtio_virtqueue_t;

/**
 * @brief Allocate and program a split virtqueue, enabling it on the device.
 *
 * Selects @p queue_index, backs the descriptor/avail/used rings with one pinned
 * (physically contiguous) zeroed page, programs queue_desc/driver/device with
 * their physical addresses, and sets queue_enable. The whole ring set must fit
 * in a single 4 KiB page (true for the virtio-gpu controlq/cursorq sizes).
 *
 * @param device A device brought up by hal_virtio_gpu_bringup().
 * @param mapping The mapping the device was brought up from (for the notify BAR).
 * @param queue_index Virtqueue to program (0 = controlq, 1 = cursorq).
 * @param out_queue Destination for the programmed virtqueue.
 * @return true when the queue was allocated, programmed and enabled.
 */
bool hal_virtio_gpu_setup_queue(const hal_virtio_gpu_device_t *device, const hal_virtio_gpu_mapping_t *mapping,
                                uint16_t queue_index, hal_virtio_virtqueue_t *out_queue);

/**
 * @brief Set DRIVER_OK, completing device initialisation (queues must be set).
 * @return the device_status read back after the write.
 */
uint8_t hal_virtio_gpu_driver_ok(const hal_virtio_gpu_device_t *device);

/** @brief Decoded VIRTIO_GPU_CMD_GET_DISPLAY_INFO response (scanout 0). */
typedef struct {
    uint32_t response_type; /* control-header type (0x1101 = OK_DISPLAY_INFO) */
    uint32_t enabled;       /* non-zero when scanout 0 is enabled             */
    uint32_t width;         /* scanout 0 preferred width                      */
    uint32_t height;        /* scanout 0 preferred height                     */
} hal_virtio_gpu_display_info_t;

/**
 * @brief Issue VIRTIO_GPU_CMD_GET_DISPLAY_INFO and read back scanout 0.
 *
 * Builds a request/response descriptor chain on @p queue, publishes it to the
 * available ring, rings the notify doorbell, polls the used ring for
 * completion, and decodes the first scanout's geometry. This is the first full
 * round-trip over the virtqueue and the template the 2D lifecycle reuses.
 *
 * @param queue A programmed controlq from hal_virtio_gpu_setup_queue().
 * @param out_info Destination for the decoded display info.
 * @return true when the command completed and the response was OK_DISPLAY_INFO.
 */
bool hal_virtio_gpu_get_display_info(hal_virtio_virtqueue_t *queue, hal_virtio_gpu_display_info_t *out_info);

/**
 * @brief A live virtio-gpu scanout: a host 2D resource bound to a display.
 *
 * @ref framebuffer is a guest-owned BGRX (32bpp) surface the caller draws into;
 * hal_virtio_gpu_flush() pushes its contents to the host and presents them.
 */
typedef struct {
    uint8_t ready;            /* non-zero when the scanout is bound + presentable */
    uint32_t resource_id;     /* host resource id                                 */
    uint32_t scanout_id;      /* display index this resource is bound to          */
    uint32_t width;           /* surface width in pixels                          */
    uint32_t height;          /* surface height in pixels                         */
    uint32_t *framebuffer;    /* guest BGRX surface (width*height pixels)          */
    uint32_t framebuffer_size;/* surface size in bytes                            */
    hal_virtio_virtqueue_t *queue; /* controlq used for present commands          */
    void *command_buffer;     /* internal scratch (request/response + SG entries) */
} hal_virtio_gpu_scanout_t;

/**
 * @brief Create a 2D host resource, back it with guest memory, and bind it to a
 *        scanout (RESOURCE_CREATE_2D + RESOURCE_ATTACH_BACKING + SET_SCANOUT).
 *
 * Allocates a guest BGRX framebuffer, attaches it to the host resource via a
 * coalesced scatter-gather page list, and binds the resource to scanout 0. On
 * success the caller draws into @ref hal_virtio_gpu_scanout_t::framebuffer and
 * calls hal_virtio_gpu_flush() to present.
 *
 * @param queue A programmed controlq from hal_virtio_gpu_setup_queue().
 * @param width Surface width in pixels.
 * @param height Surface height in pixels.
 * @param out_scanout Destination for the created scanout.
 * @return true when the resource was created, backed and bound.
 */
bool hal_virtio_gpu_create_scanout(hal_virtio_virtqueue_t *queue, uint32_t width, uint32_t height,
                                   hal_virtio_gpu_scanout_t *out_scanout);

/**
 * @brief Present the current framebuffer contents (TRANSFER_TO_HOST_2D +
 *        RESOURCE_FLUSH) for the whole surface.
 * @return true when both commands completed with OK_NODATA.
 */
bool hal_virtio_gpu_flush(hal_virtio_gpu_scanout_t *scanout);

/* ----------------------------------------------------------------------------
 * Persistent virtio-gpu display routing (kernel-internal)
 *
 * hal_virtio_gpu_display_init() runs the whole probe -> bring-up -> queue ->
 * scanout setup once and stashes the live scanout in static state. hal_display
 * routes its surface/clear/read/present through these when a scanout is active,
 * and falls back to the software-LFB framebuffer otherwise. The engine sees
 * only the stable hal_display_* contract and never knows which backend won.
 * ------------------------------------------------------------------------- */

/** @brief Bring up a virtio-gpu scanout for hal_display; false if unavailable. */
bool hal_virtio_gpu_display_init(void);

/** @brief True when a virtio-gpu scanout is live (hal_display should route here). */
bool hal_virtio_gpu_display_active(void);

/** @brief Fill @p out_descriptor from the active scanout surface. */
bool hal_virtio_gpu_display_query(hal_surface_descriptor_t *out_descriptor);

/** @brief Clear the scanout surface to a packed 0x00RRGGBB color (no present). */
void hal_virtio_gpu_display_clear(uint32_t color_rgb);

/** @brief Read back one scanout pixel as 0x00RRGGBB. */
uint32_t hal_virtio_gpu_display_read_pixel(uint32_t x, uint32_t y);

/** @brief Present the scanout (TRANSFER_TO_HOST_2D + RESOURCE_FLUSH). */
void hal_virtio_gpu_display_present(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_HAL_HAL_H */
