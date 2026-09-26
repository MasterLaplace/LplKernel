#include <kernel/hal/hal.h>

#include <kernel/cpu/paging.h>
#include <kernel/cpu/pci.h>
#include <kernel/memory/pinned_memory.h>
#include <kernel/memory/vmm.h>

#include <string.h>

/** Red Hat / virtio PCI vendor id. */
#define VIRTIO_PCI_VENDOR_ID 0x1AF4u

/**
 * @brief virtio-gpu PCI device ids: modern is 0x1040 + virtio device type (16 = GPU) = 0x1050; the
 * transitional device exposes the legacy id 0x1010.
 */
#define VIRTIO_GPU_DEVICE_ID_MODERN       0x1050u
#define VIRTIO_GPU_DEVICE_ID_TRANSITIONAL 0x1010u

/** Modern virtio-pci devices use ids >= 0x1040. */
#define VIRTIO_PCI_MODERN_DEVICE_ID_BASE 0x1040u

/**
 * @brief PCI status-register bit 4 advertises a capability list; the list head is a byte pointer at
 * config offset 0x34.
 *
 * Each capability begins with an 8-bit id followed by an 8-bit pointer to the next capability (0
 * terminates).
 */
#define PCI_STATUS_CAPABILITIES_LIST      0x0010u
#define PCI_REGISTER_CAPABILITIES_POINTER 0x34u
#define PCI_CAP_ID_VENDOR_SPECIFIC        0x09u

/**
 * @name Layout of a virtio_pci_cap structure inside the capability list
 * @{
 */
#define VIRTIO_PCI_CAP_OFFSET_CFG_TYPE          3u  /**< u8  cfg_type */
#define VIRTIO_PCI_CAP_OFFSET_BAR               4u  /**< u8  bar index */
#define VIRTIO_PCI_CAP_OFFSET_OFFSET            8u  /**< le32 offset within the BAR */
#define VIRTIO_PCI_CAP_OFFSET_LENGTH            12u /**< le32 length of the structure */
#define VIRTIO_PCI_NOTIFY_CAP_OFFSET_MULTIPLIER 16u /**< le32 (notify only) */
/** @} */

/** Guard against a malformed / cyclic capability list. */
#define VIRTIO_PCI_CAP_WALK_LIMIT 48u

/**
 * @name virtio_pci_common_cfg field offsets (virtio 1.x spec, little-endian)
 * @{
 */
#define VIRTIO_PCI_COMMON_DEVICE_FEATURE_SELECT 0x00u /**< le32 */
#define VIRTIO_PCI_COMMON_DEVICE_FEATURE        0x04u /**< le32 */
#define VIRTIO_PCI_COMMON_DRIVER_FEATURE_SELECT 0x08u /**< le32 */
#define VIRTIO_PCI_COMMON_DRIVER_FEATURE        0x0Cu /**< le32 */
#define VIRTIO_PCI_COMMON_NUM_QUEUES            0x12u /**< le16 */
#define VIRTIO_PCI_COMMON_DEVICE_STATUS         0x14u /**< u8 */
#define VIRTIO_PCI_COMMON_QUEUE_SELECT          0x16u /**< le16 */
#define VIRTIO_PCI_COMMON_QUEUE_SIZE            0x18u /**< le16 */
#define VIRTIO_PCI_COMMON_QUEUE_ENABLE          0x1Cu /**< le16 */
#define VIRTIO_PCI_COMMON_QUEUE_NOTIFY_OFF      0x1Eu /**< le16 */
#define VIRTIO_PCI_COMMON_QUEUE_DESC            0x20u /**< le64 */
#define VIRTIO_PCI_COMMON_QUEUE_DRIVER          0x28u /**< le64 (avail ring) */
#define VIRTIO_PCI_COMMON_QUEUE_DEVICE          0x30u /**< le64 (used ring) */
/** @} */

/**
 * @name device_status bits
 * @{
 */
#define VIRTIO_STATUS_ACKNOWLEDGE 0x01u
#define VIRTIO_STATUS_DRIVER      0x02u
#define VIRTIO_STATUS_DRIVER_OK   0x04u
#define VIRTIO_STATUS_FEATURES_OK 0x08u
#define VIRTIO_STATUS_FAILED      0x80u
/** @} */

/**
 * @name VIRTIO_F_VERSION_1 is feature bit 32 -> bit 0 of feature word 1
 * @{
 */
#define VIRTIO_FEATURE_WORD_VERSION_1 1u
#define VIRTIO_F_VERSION_1_BIT        0x00000001u
/** @} */

/**
 * @name Split-virtqueue ring sizes for a queue of @p size descriptors
 * @{
 */
#define VIRTQ_DESC_BYTES(size)  (16u * (uint32_t) (size))
#define VIRTQ_AVAIL_BYTES(size) (6u + 2u * (uint32_t) (size))
#define VIRTQ_USED_BYTES(size)  (6u + 8u * (uint32_t) (size))
/** @} */

/**
 * @name Split-virtqueue descriptor flags
 * @{
 */
#define VIRTQ_DESC_F_NEXT  0x0001u /**< buffer continues in the next descriptor */
#define VIRTQ_DESC_F_WRITE 0x0002u /**< device-writable (else device-readable) */
/** @} */

/**
 * @name virtio_gpu_ctrl_hdr layout (24 bytes) + the commands we use
 * @{
 */
#define VIRTIO_GPU_CTRL_HDR_BYTES       24u
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100u
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101u
/** @} */

/** virtio_gpu_resp_display_info: ctrl_hdr + 16 * virtio_gpu_display_one(24B). */
#define VIRTIO_GPU_MAX_SCANOUTS      16u
#define VIRTIO_GPU_DISPLAY_ONE_BYTES 24u
#define VIRTIO_GPU_DISPLAY_INFO_BYTES                                                                                  \
    (VIRTIO_GPU_CTRL_HDR_BYTES + VIRTIO_GPU_MAX_SCANOUTS * VIRTIO_GPU_DISPLAY_ONE_BYTES)

/** Bounded spin waiting for the device to populate the used ring. */
#define VIRTIO_GPU_POLL_LIMIT 100000000u

/**
 * @name 2D display lifecycle commands and responses
 * @{
 */
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      0x0101u
#define VIRTIO_GPU_CMD_SET_SCANOUT             0x0103u
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH          0x0104u
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     0x0105u
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define VIRTIO_GPU_RESP_OK_NODATA              0x1100u
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM       2u
/** @} */

/** The single scanout/resource this HAL drives (one display, software present). */
#define VIRTIO_GPU_SCANOUT_RESOURCE_ID 1u
#define VIRTIO_GPU_SCANOUT_ID          0u

/**
 * @brief command_buffer page layout: request header and response (entries live in a separate,
 * possibly multi-page, pinned buffer chained as its own descriptors).
 */
#define CMD_REQUEST_OFFSET  0u
#define CMD_RESPONSE_OFFSET 256u /**< requests stay well under 256 bytes */

/**
 * @brief Upper bound on chained descriptors per command (request + entry pages + resp).
 *
 * 16 segments => up to 14 entry pages => 14*256 = 3584 SG entries.
 */
#define VIRTIO_GPU_MAX_CHAIN_SEGMENTS 16u

/**
 * @name Default geometry when the device reports no preferred mode
 * @{
 */
#define VIRTIO_GPU_DEFAULT_WIDTH  1024u
#define VIRTIO_GPU_DEFAULT_HEIGHT 768u
/** @} */

/** One buffer in a descriptor chain. */
typedef struct {
    uint32_t physical;       /**< Physical base of the buffer. */
    uint32_t length;         /**< Buffer length in bytes. */
    uint8_t device_writable; /**< Non-zero when the device writes it (a response). */
} virtq_segment_t;

static hardware_abstraction_layer_virtio_gpu_mapping_t g_mapping;
static hardware_abstraction_layer_virtio_gpu_device_t g_device;
static hardware_abstraction_layer_virtio_virtqueue_t g_controlq;
static hardware_abstraction_layer_virtio_gpu_scanout_t g_scanout;
static bool g_display_ready = false;

/**
 * @brief Reads a byte from the cache-disabled BAR mapping.
 * @param address Virtual address of the register.
 * @return The value.
 */
static inline uint8_t mmio_read8(uint32_t address) { return *(volatile uint8_t *) address; }

/**
 * @brief Reads a 16-bit register from the cache-disabled BAR mapping.
 * @param address Virtual address of the register.
 * @return The value.
 */
static inline uint16_t mmio_read16(uint32_t address) { return *(volatile uint16_t *) address; }

/**
 * @brief Writes a byte to the cache-disabled BAR mapping.
 * @param address Virtual address of the register.
 * @param value   The value.
 */
static inline void mmio_write8(uint32_t address, uint8_t value) { *(volatile uint8_t *) address = value; }

/**
 * @brief Writes a 16-bit register in the cache-disabled BAR mapping.
 * @param address Virtual address of the register.
 * @param value   The value.
 */
static inline void mmio_write16(uint32_t address, uint16_t value) { *(volatile uint16_t *) address = value; }

/**
 * @brief Writes a 32-bit register in the cache-disabled BAR mapping.
 * @param address Virtual address of the register.
 * @param value   The value.
 */
static inline void mmio_write32(uint32_t address, uint32_t value) { *(volatile uint32_t *) address = value; }

/**
 * @brief Reads a 16-bit ring field; volatile so a polling loop is never hoisted.
 * @param address Virtual address in the ring.
 * @return The value.
 */
static inline uint16_t ring_read16(uint32_t address) { return *(volatile uint16_t *) address; }

/**
 * @brief Reads a 32-bit ring field; volatile so a polling loop is never hoisted.
 * @param address Virtual address in the ring.
 * @return The value.
 */
static inline uint32_t ring_read32(uint32_t address) { return *(volatile uint32_t *) address; }

/**
 * @brief Writes a 16-bit ring field.
 * @param address Virtual address in the ring.
 * @param value   The value.
 */
static inline void ring_write16(uint32_t address, uint16_t value) { *(volatile uint16_t *) address = value; }

/**
 * @brief Writes a 32-bit ring field.
 * @param address Virtual address in the ring.
 * @param value   The value.
 */
static inline void ring_write32(uint32_t address, uint32_t value) { *(volatile uint32_t *) address = value; }

/**
 * @brief Writes a 64-bit little-endian field from a 32-bit value.
 *
 * @note The high word is always zero: physical addresses are 32-bit on i686.
 *
 * @param address Virtual address of the field.
 * @param low     The low 32 bits.
 */
static void write64(uint32_t address, uint32_t low)
{
    ring_write32(address + 0u, low);
    ring_write32(address + 4u, 0u);
}

/**
 * @brief Rounds @p value up to the next multiple of @p align.
 * @param value The value.
 * @param align A power of two.
 * @return The rounded value.
 */
static inline uint32_t align_up(uint32_t value, uint32_t align) { return (value + (align - 1u)) & ~(align - 1u); }

/**
 * @brief Is this PCI function a virtio-gpu?
 * @param dev An enumerated PCI function.
 * @return true for the virtio vendor with a modern or transitional GPU device id.
 */
static bool is_virtio_gpu(const PeripheralComponentInterconnectDevice_t *dev)
{
    if (dev->vendor_id != VIRTIO_PCI_VENDOR_ID)
        return false;
    return dev->device_id == VIRTIO_GPU_DEVICE_ID_MODERN || dev->device_id == VIRTIO_GPU_DEVICE_ID_TRANSITIONAL;
}

/**
 * @brief Records the first implemented memory BAR of a function in the probe result.
 *
 * @details Modern virtio-pci puts its notify, common and device configuration in memory space.
 *
 * @note An I/O BAR flagged 64-bit skips the slot after it, which such a BAR consumes.
 *
 * @param dev      The virtio-gpu function.
 * @param out_info Receives the BAR's base, size and index.
 */
static void decode_first_memory_bar(const PeripheralComponentInterconnectDevice_t *dev,
                                    hardware_abstraction_layer_virtio_gpu_info_t *out_info)
{
    for (uint8_t index = 0u; index < PERIPHERAL_COMPONENT_INTERCONNECT_BASE_ADDRESS_REGISTER_COUNT; ++index)
    {
        PeripheralComponentInterconnectBaseAddressRegister_t bar;
        if (!peripheral_component_interconnect_read_base_address_register(dev->bus, dev->device, dev->function, index,
                                                                          &bar))
            continue;
        if (bar.is_io)
        {
            if (bar.is_64bit)
                ++index;
            continue;
        }
        out_info->mmio_base = (uint32_t) bar.base;
        out_info->mmio_size = (uint32_t) bar.size;
        out_info->mmio_bar_index = bar.index;
        return;
    }
}

/**
 * @brief The capability slot a virtio cfg_type is recorded in.
 * @param mapping  The mapping being filled.
 * @param cfg_type The capability's cfg_type.
 * @return The slot, or NULL for a cfg_type this HAL ignores.
 */
static hardware_abstraction_layer_virtio_pci_cap_t *
cap_slot_for_type(hardware_abstraction_layer_virtio_gpu_mapping_t *mapping, uint8_t cfg_type)
{
    switch (cfg_type)
    {
    case HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_COMMON_CFG: return &mapping->common;
    case HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_NOTIFY_CFG: return &mapping->notify;
    case HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_ISR_CFG: return &mapping->isr;
    case HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_DEVICE_CFG: return &mapping->device;
    default: return (void *) 0;
    }
}

/**
 * @brief Walks the PCI capability list, recording every virtio cfg structure.
 * @param info    The probed function.
 * @param mapping Receives the structures' locations and the notify multiplier.
 */
static void walk_virtio_capabilities(const hardware_abstraction_layer_virtio_gpu_info_t *info,
                                     hardware_abstraction_layer_virtio_gpu_mapping_t *mapping)
{
    const uint16_t status = peripheral_component_interconnect_config_read_word(
        info->bus, info->device, info->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_STATUS);
    if ((status & PCI_STATUS_CAPABILITIES_LIST) == 0u)
        return;

    uint8_t pointer = peripheral_component_interconnect_config_read_byte(info->bus, info->device, info->function,
                                                                         PCI_REGISTER_CAPABILITIES_POINTER);
    for (uint32_t guard = 0u; pointer != 0u && guard < VIRTIO_PCI_CAP_WALK_LIMIT; ++guard)
    {
        const uint8_t cap_id =
            peripheral_component_interconnect_config_read_byte(info->bus, info->device, info->function, pointer);
        const uint8_t next = peripheral_component_interconnect_config_read_byte(info->bus, info->device, info->function,
                                                                                (uint8_t) (pointer + 1u));

        if (cap_id == PCI_CAP_ID_VENDOR_SPECIFIC)
        {
            const uint8_t cfg_type = peripheral_component_interconnect_config_read_byte(
                info->bus, info->device, info->function, (uint8_t) (pointer + VIRTIO_PCI_CAP_OFFSET_CFG_TYPE));
            hardware_abstraction_layer_virtio_pci_cap_t *slot = cap_slot_for_type(mapping, cfg_type);
            if (slot != (void *) 0)
            {
                slot->present = 1u;
                slot->bar = peripheral_component_interconnect_config_read_byte(
                    info->bus, info->device, info->function, (uint8_t) (pointer + VIRTIO_PCI_CAP_OFFSET_BAR));
                slot->offset = peripheral_component_interconnect_config_read_dword(
                    info->bus, info->device, info->function, (uint8_t) (pointer + VIRTIO_PCI_CAP_OFFSET_OFFSET));
                slot->length = peripheral_component_interconnect_config_read_dword(
                    info->bus, info->device, info->function, (uint8_t) (pointer + VIRTIO_PCI_CAP_OFFSET_LENGTH));
                if (cfg_type == HARDWARE_ABSTRACTION_LAYER_VIRTIO_PCI_CAP_NOTIFY_CFG)
                    mapping->notify_off_multiplier = peripheral_component_interconnect_config_read_dword(
                        info->bus, info->device, info->function,
                        (uint8_t) (pointer + VIRTIO_PCI_NOTIFY_CAP_OFFSET_MULTIPLIER));
            }
        }
        pointer = next;
    }
}

/**
 * @brief Maps an MMIO BAR window into kernel virtual space, cache-disabled.
 *
 * @details The VMM picks a free virtual range, which is collision-safe, and each page is
 *          mapped onto the device's physical BAR pages.
 *
 * @param phys_base_raw Physical base, as read from the base address register.
 * @param size          Window length in bytes.
 * @return The virtual address of the BAR base — a caller adds each cap.offset to reach a
 *         structure — or 0 on failure.
 */
static uint32_t map_mmio_window(uint32_t phys_base_raw, uint32_t size)
{
    if (phys_base_raw == 0u || size == 0u)
        return 0u;

    const uint32_t page_offset = phys_base_raw & 0xFFFu;
    const uint32_t phys_base = phys_base_raw & 0xFFFFF000u;
    const uint32_t span = page_offset + size;
    const uint32_t page_count = (span + 0xFFFu) >> 12;

    void *virt = kernel_vmm_reserve_pages(page_count);
    if (virt == (void *) 0)
        return 0u;

    const PageDirectoryEntry_t pde_flags = {.present = 1u, .read_write = 1u};
    const PageTableEntry_t pte_flags = {.present = 1u, .read_write = 1u, .cache_disable = 1u, .write_through = 1u};

    const uint32_t virt_base = (uint32_t) virt;
    for (uint32_t i = 0u; i < page_count; ++i)
    {
        if (!paging_map_page(virt_base + (i << 12), phys_base + (i << 12), pde_flags, pte_flags))
        {
            kernel_vmm_free_pages(virt, page_count);
            return 0u;
        }
    }
    return virt_base + page_offset;
}

/**
 * @brief Resets the device: writes zero to device_status and reads it back to flush the write.
 * @param status_reg Virtual address of device_status.
 */
static void virtio_gpu_reset(uint32_t status_reg)
{
    mmio_write8(status_reg, 0u);
    (void) mmio_read8(status_reg);
}

/**
 * @brief Acknowledges the device, then signals that a driver is present.
 * @param status_reg Virtual address of device_status.
 */
static void virtio_gpu_announce_driver(uint32_t status_reg)
{
    mmio_write8(status_reg, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write8(status_reg, (uint8_t) (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER));
}

/**
 * @brief Negotiates features, accepting VIRTIO_F_VERSION_1 (a modern device) and nothing else.
 * @param common Virtual address of the common configuration.
 */
static void virtio_gpu_accept_version_1_only(uint32_t common)
{
    mmio_write32(common + VIRTIO_PCI_COMMON_DRIVER_FEATURE_SELECT, VIRTIO_FEATURE_WORD_VERSION_1);
    mmio_write32(common + VIRTIO_PCI_COMMON_DRIVER_FEATURE, VIRTIO_F_VERSION_1_BIT);
    mmio_write32(common + VIRTIO_PCI_COMMON_DRIVER_FEATURE_SELECT, 0u);
    mmio_write32(common + VIRTIO_PCI_COMMON_DRIVER_FEATURE, 0u);
}

/**
 * @brief Commits FEATURES_OK and reads back what the device made of it.
 * @param status_reg Virtual address of device_status.
 * @return The device_status after the commit.
 */
static uint8_t virtio_gpu_commit_features(uint32_t status_reg)
{
    mmio_write8(status_reg, (uint8_t) (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK));
    return mmio_read8(status_reg);
}

/**
 * @brief Did the device keep FEATURES_OK set, and not fail?
 * @param status device_status read back after the commit.
 * @return false when the device rejected the feature set.
 */
static bool virtio_gpu_features_accepted(uint8_t status)
{
    return (status & VIRTIO_STATUS_FEATURES_OK) != 0u && (status & VIRTIO_STATUS_FAILED) == 0u;
}

/**
 * @brief Reads the size of each virtqueue the device exposes, up to the HAL's maximum.
 * @param common     Virtual address of the common configuration.
 * @param out_device Holds the queue count; receives each queue's size.
 */
static void virtio_gpu_read_queue_sizes(uint32_t common, hardware_abstraction_layer_virtio_gpu_device_t *out_device)
{
    const uint16_t queue_count = (out_device->num_queues < HARDWARE_ABSTRACTION_LAYER_VIRTIO_GPU_MAX_QUEUES) ?
                                     out_device->num_queues :
                                     HARDWARE_ABSTRACTION_LAYER_VIRTIO_GPU_MAX_QUEUES;
    for (uint16_t queue = 0u; queue < queue_count; ++queue)
    {
        mmio_write16(common + VIRTIO_PCI_COMMON_QUEUE_SELECT, queue);
        out_device->queue_size[queue] = mmio_read16(common + VIRTIO_PCI_COMMON_QUEUE_SIZE);
    }
}

/**
 * @brief Programs a 64-bit common-cfg field from a 32-bit physical address.
 * @param common       Virtual address of the common configuration.
 * @param field_offset Offset of the field.
 * @param physical     The address; the high word is written as zero.
 */
static void write_queue_address64(uint32_t common, uint32_t field_offset, uint32_t physical)
{
    mmio_write32(common + field_offset, physical);
    mmio_write32(common + field_offset + 4u, 0u);
}

/**
 * @brief Where the selected queue's doorbell is.
 * @param mapping The device's mapping, for the notify structure and its multiplier.
 * @param common  Virtual address of the common configuration, with the queue selected.
 * @return The notify structure's base plus queue_notify_off times the multiplier.
 */
static uint32_t virtio_gpu_notify_address(const hardware_abstraction_layer_virtio_gpu_mapping_t *mapping,
                                          uint32_t common)
{
    const uint16_t notify_off = mmio_read16(common + VIRTIO_PCI_COMMON_QUEUE_NOTIFY_OFF);
    return mapping->mmio_virtual_base + mapping->notify.offset + (uint32_t) notify_off * mapping->notify_off_multiplier;
}

/**
 * @brief Programs the selected queue's three ring addresses and enables it.
 * @param common        Virtual address of the common configuration, with the queue selected.
 * @param ring_physical Physical base of the descriptor table.
 * @param avail_offset  Offset of the available ring from that base.
 * @param used_offset   Offset of the used ring from that base.
 */
static void virtio_gpu_enable_queue(uint32_t common, uint32_t ring_physical, uint32_t avail_offset,
                                    uint32_t used_offset)
{
    write_queue_address64(common, VIRTIO_PCI_COMMON_QUEUE_DESC, ring_physical);
    write_queue_address64(common, VIRTIO_PCI_COMMON_QUEUE_DRIVER, ring_physical + avail_offset);
    write_queue_address64(common, VIRTIO_PCI_COMMON_QUEUE_DEVICE, ring_physical + used_offset);
    mmio_write16(common + VIRTIO_PCI_COMMON_QUEUE_ENABLE, 1u);
}

/**
 * @brief Writes one 16-byte split-virtqueue descriptor.
 *
 * @details The layout is the 64-bit buffer address, whose high word is zero on i686, then
 *          the length, the flags and the index of the next descriptor.
 *
 * @param queue           The virtqueue.
 * @param slot            Descriptor index.
 * @param buffer_physical Physical address of the buffer.
 * @param length          Buffer length in bytes.
 * @param flags           VIRTQ_DESC_F_* flags.
 * @param next            Index of the next descriptor in the chain.
 */
static void write_descriptor(const hardware_abstraction_layer_virtio_virtqueue_t *queue, uint16_t slot,
                             uint32_t buffer_physical, uint32_t length, uint16_t flags, uint16_t next)
{
    const uint32_t desc = queue->desc_address + (uint32_t) slot * 16u;
    write64(desc + 0u, buffer_physical);
    ring_write32(desc + 8u, length);
    ring_write16(desc + 12u, flags);
    ring_write16(desc + 14u, next);
}

/**
 * @brief Publishes descriptor head 0 into the available ring.
 * @param queue The virtqueue.
 */
static void publish_head_descriptor(const hardware_abstraction_layer_virtio_virtqueue_t *queue)
{
    const uint16_t avail_idx = ring_read16(queue->avail_address + 2u);
    ring_write16(queue->avail_address + 4u + (uint32_t) (avail_idx % queue->queue_size) * 2u, 0u);
    __sync_synchronize();
    ring_write16(queue->avail_address + 2u, (uint16_t) (avail_idx + 1u));
    __sync_synchronize();
}

/**
 * @brief Rings the queue's doorbell.
 * @param queue The virtqueue.
 */
static void ring_doorbell(const hardware_abstraction_layer_virtio_virtqueue_t *queue)
{
    mmio_write16(queue->notify_address, queue->queue_index);
}

/**
 * @brief Waits, boundedly, for the device to advance the used ring.
 * @param queue The virtqueue; its consumer cursor advances on completion.
 * @return false when the device never completed the request.
 */
static bool wait_for_used_ring(hardware_abstraction_layer_virtio_virtqueue_t *queue)
{
    for (uint32_t spin = 0u; spin < VIRTIO_GPU_POLL_LIMIT; ++spin)
    {
        if (ring_read16(queue->used_address + 2u) != queue->last_used_index)
        {
            __sync_synchronize();
            queue->last_used_index = (uint16_t) (queue->last_used_index + 1u);
            return true;
        }
    }
    return false;
}

/**
 * @brief Submits a descriptor chain, notifies the device and polls for completion.
 *
 * @details Each segment becomes one chained descriptor, the head being descriptor 0.
 *
 * @param queue    The virtqueue.
 * @param segments The buffers, in chain order.
 * @param count    How many; at most the queue size.
 * @return true on completion.
 */
static bool submit_chain(hardware_abstraction_layer_virtio_virtqueue_t *queue, const virtq_segment_t *segments,
                         uint16_t count)
{
    if (count == 0u || count > queue->queue_size)
        return false;

    for (uint16_t i = 0u; i < count; ++i)
    {
        const uint16_t flags = (uint16_t) ((segments[i].device_writable ? VIRTQ_DESC_F_WRITE : 0u) |
                                           ((i + 1u < count) ? VIRTQ_DESC_F_NEXT : 0u));
        write_descriptor(queue, i, segments[i].physical, segments[i].length, flags, (uint16_t) (i + 1u));
    }

    publish_head_descriptor(queue);
    ring_doorbell(queue);
    return wait_for_used_ring(queue);
}

/**
 * @brief Decodes scanout 0 out of a GET_DISPLAY_INFO response.
 *
 * @details pmodes[0] follows the response header: rect {x, y, width, height}, then enabled.
 *
 * @param response_va Virtual address of the response.
 * @param out_info    Receives the response type and scanout 0's geometry.
 */
static void read_first_scanout(uint32_t response_va, hardware_abstraction_layer_virtio_gpu_display_info_t *out_info)
{
    out_info->response_type = ring_read32(response_va + 0u);
    const uint32_t scanout0 = response_va + VIRTIO_GPU_CTRL_HDR_BYTES;
    out_info->width = ring_read32(scanout0 + 8u);
    out_info->height = ring_read32(scanout0 + 12u);
    out_info->enabled = ring_read32(scanout0 + 16u);
}

/**
 * @brief Writes a command control header and the virtio_gpu_rect that follows it.
 *
 * @details The type at offset 0 and {x, y, width, height} at 24: the shared prefix of
 *          SET_SCANOUT, TRANSFER_TO_HOST_2D and RESOURCE_FLUSH. Command-specific fields follow
 *          at offset 40.
 *
 * @param cmd_va  Virtual address of the request.
 * @param command The command type.
 * @param x       Rectangle origin, horizontal.
 * @param y       Rectangle origin, vertical.
 * @param width   Rectangle width.
 * @param height  Rectangle height.
 */
static void write_command_rect(uint32_t cmd_va, uint32_t command, uint32_t x, uint32_t y, uint32_t width,
                               uint32_t height)
{
    ring_write32(cmd_va + 0u, command);
    ring_write32(cmd_va + 24u, x);
    ring_write32(cmd_va + 28u, y);
    ring_write32(cmd_va + 32u, width);
    ring_write32(cmd_va + 36u, height);
}

/**
 * @brief Sends a request pre-filled at the command buffer and returns the response type.
 *
 * @details The response type is cleared first, so a stale one from the previous command can
 *          never be read as this command's answer. Extra segments are chained between the
 *          request and the response.
 *
 * @param scanout              The scanout whose command buffer holds the request.
 * @param request_length       Bytes of the request.
 * @param extra_segment_count  How many extra device-readable segments follow the request.
 * @param extra_segments       The extra segments; may be NULL when there are none.
 * @return The response type, or 0 when the chain would overflow or the command never completed.
 */
static uint32_t send_nodata_command(const hardware_abstraction_layer_virtio_gpu_scanout_t *scanout,
                                    uint32_t request_length, uint16_t extra_segment_count,
                                    const virtq_segment_t *extra_segments)
{
    const uint32_t cmd_physical = scanout->command_buffer_physical;
    const uint32_t cmd_va = (uint32_t) scanout->command_buffer;
    ring_write32(cmd_va + CMD_RESPONSE_OFFSET, 0u);

    virtq_segment_t segments[VIRTIO_GPU_MAX_CHAIN_SEGMENTS];
    uint16_t count = 0u;
    segments[count++] = (virtq_segment_t){cmd_physical + CMD_REQUEST_OFFSET, request_length, 0u};
    for (uint16_t i = 0u; i < extra_segment_count; ++i)
    {
        if (count + 1u >= VIRTIO_GPU_MAX_CHAIN_SEGMENTS)
            return 0u;
        segments[count++] = extra_segments[i];
    }
    segments[count++] = (virtq_segment_t){cmd_physical + CMD_RESPONSE_OFFSET, VIRTIO_GPU_CTRL_HDR_BYTES, 1u};

    if (!submit_chain(scanout->queue, segments, count))
        return 0u;
    return ring_read32(cmd_va + CMD_RESPONSE_OFFSET);
}

/**
 * @brief Grows the last scatter-gather entry by one chunk of a contiguous run.
 * @param entries_va Virtual address of the entry list.
 * @param count      Entries written so far; at least one.
 * @param chunk      Bytes to add.
 */
static void extend_last_backing_entry(uint32_t entries_va, uint32_t count, uint32_t chunk)
{
    const uint32_t last = entries_va + (count - 1u) * 16u;
    ring_write32(last + 8u, ring_read32(last + 8u) + chunk);
}

/**
 * @brief Appends one scatter-gather entry: a 64-bit address, a length, and padding.
 * @param entries_va Virtual address of the entry list.
 * @param index      Index of the entry.
 * @param physical   Physical address of the run.
 * @param length     Bytes in the run.
 */
static void append_backing_entry(uint32_t entries_va, uint32_t index, uint32_t physical, uint32_t length)
{
    const uint32_t entry = entries_va + index * 16u;
    write64(entry + 0u, physical);
    ring_write32(entry + 8u, length);
    ring_write32(entry + 12u, 0u);
}

/**
 * @brief Writes a coalesced scatter-gather list of the framebuffer's physical pages.
 *
 * @details Pinned pages are allocated frame by frame, so this typically yields one entry per
 *          page; physically contiguous pages are merged into one entry.
 *
 * @param scanout     The scanout whose framebuffer is described.
 * @param entries_va  Virtual address of the entry list.
 * @param max_entries Room in the list.
 * @return The entry count, or 0 when a page has no physical address or the list is full.
 */
static uint32_t build_backing_entries(const hardware_abstraction_layer_virtio_gpu_scanout_t *scanout,
                                      uint32_t entries_va, uint32_t max_entries)
{
    uint32_t remaining = scanout->framebuffer_size;
    uint32_t page_va = (uint32_t) scanout->framebuffer;
    uint32_t count = 0u;
    uint32_t previous_end = 0u;
    bool have_previous = false;

    while (remaining != 0u)
    {
        const uint32_t chunk = (remaining < 4096u) ? remaining : 4096u;
        uint32_t page_physical = 0u;
        if (!hardware_abstraction_layer_graphics_memory_physical_address((const void *) page_va, &page_physical))
            return 0u;

        if (have_previous && page_physical == previous_end)
        {
            extend_last_backing_entry(entries_va, count, chunk);
        }
        else
        {
            if (count >= max_entries)
                return 0u;
            append_backing_entry(entries_va, count, page_physical, chunk);
            ++count;
        }

        previous_end = page_physical + chunk;
        have_previous = true;
        page_va += 4096u;
        remaining -= chunk;
    }
    return count;
}

/**
 * @brief RESOURCE_CREATE_2D: creates the host resource the scanout will show.
 * @param scanout The scanout being built.
 * @return true on OK_NODATA.
 */
static bool create_resource_2d(const hardware_abstraction_layer_virtio_gpu_scanout_t *scanout)
{
    const uint32_t cmd_va = (uint32_t) scanout->command_buffer;
    ring_write32(cmd_va + 0u, VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
    ring_write32(cmd_va + 24u, scanout->resource_id);
    ring_write32(cmd_va + 28u, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM);
    ring_write32(cmd_va + 32u, scanout->width);
    ring_write32(cmd_va + 36u, scanout->height);
    return send_nodata_command(scanout, 40u, 0u, (void *) 0) == VIRTIO_GPU_RESP_OK_NODATA;
}

/**
 * @brief Describes each page of the entry list as one device-readable segment.
 *
 * @details The entry list can span several pages, and each page is contiguous on its own, so
 *          each becomes its own descriptor in the chain.
 *
 * @param entries_buffer The pinned entry list.
 * @param used_bytes     Bytes of the list actually written.
 * @param entry_pages    Pages those bytes span.
 * @param out_segments   Receives one segment per page.
 * @return false when the list needs more segments than a chain allows, or a page has no
 *         physical address.
 */
static bool describe_entry_pages(const uint32_t *entries_buffer, uint32_t used_bytes, uint32_t entry_pages,
                                 virtq_segment_t *out_segments)
{
    if (entry_pages > VIRTIO_GPU_MAX_CHAIN_SEGMENTS - 2u)
        return false;

    for (uint32_t p = 0u; p < entry_pages; ++p)
    {
        uint32_t page_physical = 0u;
        if (!hardware_abstraction_layer_graphics_memory_physical_address(
                (const void *) ((uint32_t) entries_buffer + p * 4096u), &page_physical))
            return false;
        const uint32_t span = used_bytes - p * 4096u;
        out_segments[p] = (virtq_segment_t){page_physical, (span < 4096u) ? span : 4096u, 0u};
    }
    return true;
}

/**
 * @brief RESOURCE_ATTACH_BACKING: hands the framebuffer's pages to the host resource.
 *
 * @details The scatter-gather list is backed by its own pinned buffer, freed once the command
 *          has completed.
 *
 * @param scanout The scanout being built.
 * @return true on OK_NODATA.
 */
static bool attach_backing(const hardware_abstraction_layer_virtio_gpu_scanout_t *scanout)
{
    const uint32_t fb_pages = (scanout->framebuffer_size + 4095u) / 4096u;
    const uint32_t entries_bytes = fb_pages * 16u;
    uint32_t *entries_buffer = (uint32_t *) kernel_pinned_alloc(entries_bytes);
    if (entries_buffer == (void *) 0)
        return false;

    const uint32_t entry_count = build_backing_entries(scanout, (uint32_t) entries_buffer, fb_pages);
    if (entry_count == 0u)
    {
        kernel_pinned_free(entries_buffer, entries_bytes);
        return false;
    }

    const uint32_t used_bytes = entry_count * 16u;
    const uint32_t entry_pages = (used_bytes + 4095u) / 4096u;
    virtq_segment_t entry_segments[VIRTIO_GPU_MAX_CHAIN_SEGMENTS];
    const bool entries_ok = describe_entry_pages(entries_buffer, used_bytes, entry_pages, entry_segments);

    const uint32_t cmd_va = (uint32_t) scanout->command_buffer;
    ring_write32(cmd_va + 0u, VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
    ring_write32(cmd_va + 24u, scanout->resource_id);
    ring_write32(cmd_va + 28u, entry_count);
    const bool attach_ok = entries_ok && send_nodata_command(scanout, 32u, (uint16_t) entry_pages, entry_segments) ==
                                             VIRTIO_GPU_RESP_OK_NODATA;
    kernel_pinned_free(entries_buffer, entries_bytes);
    return attach_ok;
}

/**
 * @brief SET_SCANOUT: binds the resource to the display.
 * @param scanout The scanout being built.
 * @return true on OK_NODATA.
 */
static bool set_scanout(const hardware_abstraction_layer_virtio_gpu_scanout_t *scanout)
{
    const uint32_t cmd_va = (uint32_t) scanout->command_buffer;
    write_command_rect(cmd_va, VIRTIO_GPU_CMD_SET_SCANOUT, 0u, 0u, scanout->width, scanout->height);
    ring_write32(cmd_va + 40u, scanout->scanout_id);
    ring_write32(cmd_va + 44u, scanout->resource_id);
    return send_nodata_command(scanout, 48u, 0u, (void *) 0) == VIRTIO_GPU_RESP_OK_NODATA;
}

/**
 * @brief TRANSFER_TO_HOST_2D: pushes the whole surface to the host resource.
 *
 * @details After the rectangle come a 64-bit offset into the resource, the resource id and
 *          padding.
 *
 * @param scanout A live scanout.
 * @return true on OK_NODATA.
 */
static bool transfer_to_host(const hardware_abstraction_layer_virtio_gpu_scanout_t *scanout)
{
    const uint32_t cmd_va = (uint32_t) scanout->command_buffer;
    write_command_rect(cmd_va, VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D, 0u, 0u, scanout->width, scanout->height);
    write64(cmd_va + 40u, 0u);
    ring_write32(cmd_va + 48u, scanout->resource_id);
    ring_write32(cmd_va + 52u, 0u);
    return send_nodata_command(scanout, 56u, 0u, (void *) 0) == VIRTIO_GPU_RESP_OK_NODATA;
}

/**
 * @brief RESOURCE_FLUSH: presents the transferred contents.
 *
 * @details After the rectangle come the resource id and padding.
 *
 * @param scanout A live scanout.
 * @return true on OK_NODATA.
 */
static bool flush_resource(const hardware_abstraction_layer_virtio_gpu_scanout_t *scanout)
{
    const uint32_t cmd_va = (uint32_t) scanout->command_buffer;
    write_command_rect(cmd_va, VIRTIO_GPU_CMD_RESOURCE_FLUSH, 0u, 0u, scanout->width, scanout->height);
    ring_write32(cmd_va + 40u, scanout->resource_id);
    ring_write32(cmd_va + 44u, 0u);
    return send_nodata_command(scanout, 48u, 0u, (void *) 0) == VIRTIO_GPU_RESP_OK_NODATA;
}

/**
 * @brief The geometry the device prefers, or a safe default when it reports none.
 * @param out_width  Receives the width in pixels.
 * @param out_height Receives the height in pixels.
 */
static void preferred_geometry(uint32_t *out_width, uint32_t *out_height)
{
    *out_width = VIRTIO_GPU_DEFAULT_WIDTH;
    *out_height = VIRTIO_GPU_DEFAULT_HEIGHT;

    hardware_abstraction_layer_virtio_gpu_display_info_t display;
    if (!hardware_abstraction_layer_virtio_gpu_get_display_info(&g_controlq, &display) || display.width == 0u ||
        display.height == 0u)
        return;

    *out_width = display.width;
    *out_height = display.height;
}

bool hardware_abstraction_layer_virtio_gpu_probe(hardware_abstraction_layer_virtio_gpu_info_t *out_info)
{
    if (out_info == (void *) 0)
        return false;

    *out_info = (hardware_abstraction_layer_virtio_gpu_info_t){0};

    const uint32_t count = peripheral_component_interconnect_get_device_count();
    for (uint32_t i = 0u; i < count; ++i)
    {
        const PeripheralComponentInterconnectDevice_t *dev = peripheral_component_interconnect_get_device(i);
        if (dev == (void *) 0 || !is_virtio_gpu(dev))
            continue;

        out_info->present = true;
        out_info->bus = dev->bus;
        out_info->device = dev->device;
        out_info->function = dev->function;
        out_info->device_id = dev->device_id;
        out_info->is_modern = (dev->device_id >= VIRTIO_PCI_MODERN_DEVICE_ID_BASE) ? 1u : 0u;
        decode_first_memory_bar(dev, out_info);
        return true;
    }
    return false;
}

bool hardware_abstraction_layer_virtio_gpu_map(const hardware_abstraction_layer_virtio_gpu_info_t *info,
                                               hardware_abstraction_layer_virtio_gpu_mapping_t *out_mapping)
{
    if (info == (void *) 0 || out_mapping == (void *) 0 || !info->present)
        return false;

    *out_mapping = (hardware_abstraction_layer_virtio_gpu_mapping_t){0};

    walk_virtio_capabilities(info, out_mapping);
    if (!out_mapping->common.present)
        return false;

    PeripheralComponentInterconnectBaseAddressRegister_t bar;
    if (!peripheral_component_interconnect_read_base_address_register(info->bus, info->device, info->function,
                                                                      out_mapping->common.bar, &bar) ||
        bar.is_io)
        return false;

    const uint32_t mmio_virtual_base = map_mmio_window((uint32_t) bar.base, (uint32_t) bar.size);
    if (mmio_virtual_base == 0u)
        return false;

    out_mapping->mapped = 1u;
    out_mapping->mmio_bar_index = out_mapping->common.bar;
    out_mapping->mmio_virtual_base = mmio_virtual_base;
    out_mapping->mmio_physical_base = (uint32_t) bar.base;
    out_mapping->mmio_size = (uint32_t) bar.size;
    return true;
}

bool hardware_abstraction_layer_virtio_gpu_bringup(const hardware_abstraction_layer_virtio_gpu_mapping_t *mapping,
                                                   hardware_abstraction_layer_virtio_gpu_device_t *out_device)
{
    if (mapping == (void *) 0 || out_device == (void *) 0 || !mapping->mapped || !mapping->common.present)
        return false;

    *out_device = (hardware_abstraction_layer_virtio_gpu_device_t){0};

    const uint32_t common = mapping->mmio_virtual_base + mapping->common.offset;
    const uint32_t status_reg = common + VIRTIO_PCI_COMMON_DEVICE_STATUS;

    virtio_gpu_reset(status_reg);
    virtio_gpu_announce_driver(status_reg);
    virtio_gpu_accept_version_1_only(common);
    const uint8_t status = virtio_gpu_commit_features(status_reg);

    out_device->device_status = status;
    out_device->mmio_virtual_base = mapping->mmio_virtual_base;
    out_device->common_cfg_address = common;
    out_device->num_queues = mmio_read16(common + VIRTIO_PCI_COMMON_NUM_QUEUES);

    if (!virtio_gpu_features_accepted(status))
        return false;

    virtio_gpu_read_queue_sizes(common, out_device);
    out_device->ready = 1u;
    return true;
}

bool hardware_abstraction_layer_virtio_gpu_setup_queue(const hardware_abstraction_layer_virtio_gpu_device_t *device,
                                                       const hardware_abstraction_layer_virtio_gpu_mapping_t *mapping,
                                                       uint16_t queue_index,
                                                       hardware_abstraction_layer_virtio_virtqueue_t *out_queue)
{
    if (device == (void *) 0 || mapping == (void *) 0 || out_queue == (void *) 0 || !device->ready)
        return false;

    *out_queue = (hardware_abstraction_layer_virtio_virtqueue_t){0};

    const uint32_t common = device->common_cfg_address;
    mmio_write16(common + VIRTIO_PCI_COMMON_QUEUE_SELECT, queue_index);

    const uint16_t queue_size = mmio_read16(common + VIRTIO_PCI_COMMON_QUEUE_SIZE);
    if (queue_size == 0u)
        return false;

    const uint32_t avail_offset = VIRTQ_DESC_BYTES(queue_size);
    const uint32_t used_offset = align_up(avail_offset + VIRTQ_AVAIL_BYTES(queue_size), 16u);
    const uint32_t total = used_offset + VIRTQ_USED_BYTES(queue_size);
    if (total > 4096u)
        return false;

    void *backing = kernel_pinned_alloc(4096u);
    if (backing == (void *) 0)
        return false;
    memset(backing, 0, 4096u);

    uint32_t ring_physical = 0u;
    if (!hardware_abstraction_layer_graphics_memory_physical_address(backing, &ring_physical))
    {
        kernel_pinned_free(backing, 4096u);
        return false;
    }

    const uint32_t backing_va = (uint32_t) backing;
    const uint32_t notify_address = virtio_gpu_notify_address(mapping, common);
    virtio_gpu_enable_queue(common, ring_physical, avail_offset, used_offset);

    out_queue->ready = 1u;
    out_queue->queue_index = queue_index;
    out_queue->queue_size = queue_size;
    out_queue->desc_address = backing_va;
    out_queue->avail_address = backing_va + avail_offset;
    out_queue->used_address = backing_va + used_offset;
    out_queue->notify_address = notify_address;
    out_queue->ring_physical_base = ring_physical;
    out_queue->ring_backing = backing;
    return true;
}

uint8_t hardware_abstraction_layer_virtio_gpu_driver_ok(const hardware_abstraction_layer_virtio_gpu_device_t *device)
{
    if (device == (void *) 0 || !device->ready)
        return 0u;

    const uint32_t status_reg = device->common_cfg_address + VIRTIO_PCI_COMMON_DEVICE_STATUS;
    const uint8_t status = (uint8_t) (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK |
                                      VIRTIO_STATUS_DRIVER_OK);
    mmio_write8(status_reg, status);
    return mmio_read8(status_reg);
}

bool hardware_abstraction_layer_virtio_gpu_get_display_info(
    hardware_abstraction_layer_virtio_virtqueue_t *queue,
    hardware_abstraction_layer_virtio_gpu_display_info_t *out_info)
{
    if (queue == (void *) 0 || out_info == (void *) 0 || !queue->ready)
        return false;

    *out_info = (hardware_abstraction_layer_virtio_gpu_display_info_t){0};

    void *buffer = kernel_pinned_alloc(4096u);
    if (buffer == (void *) 0)
        return false;
    memset(buffer, 0, 4096u);

    uint32_t buffer_physical = 0u;
    if (!hardware_abstraction_layer_graphics_memory_physical_address(buffer, &buffer_physical))
    {
        kernel_pinned_free(buffer, 4096u);
        return false;
    }

    const uint32_t request_va = (uint32_t) buffer;
    const uint32_t response_va = request_va + 2048u;
    const uint32_t request_physical = buffer_physical;
    const uint32_t response_physical = buffer_physical + 2048u;

    ring_write32(request_va + 0u, VIRTIO_GPU_CMD_GET_DISPLAY_INFO);

    const virtq_segment_t segments[] = {
        {request_physical,  VIRTIO_GPU_CTRL_HDR_BYTES,     0u},
        {response_physical, VIRTIO_GPU_DISPLAY_INFO_BYTES, 1u},
    };
    bool ok = submit_chain(queue, segments, 2u);
    if (ok)
    {
        read_first_scanout(response_va, out_info);
        ok = (out_info->response_type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
    }

    kernel_pinned_free(buffer, 4096u);
    return ok;
}

bool hardware_abstraction_layer_virtio_gpu_create_scanout(hardware_abstraction_layer_virtio_virtqueue_t *queue,
                                                          uint32_t width, uint32_t height,
                                                          hardware_abstraction_layer_virtio_gpu_scanout_t *out_scanout)
{
    if (queue == (void *) 0 || out_scanout == (void *) 0 || !queue->ready || width == 0u || height == 0u)
        return false;

    *out_scanout = (hardware_abstraction_layer_virtio_gpu_scanout_t){0};
    out_scanout->queue = queue;
    out_scanout->width = width;
    out_scanout->height = height;
    out_scanout->resource_id = VIRTIO_GPU_SCANOUT_RESOURCE_ID;
    out_scanout->scanout_id = VIRTIO_GPU_SCANOUT_ID;
    out_scanout->framebuffer_size = width * height * 4u;

    out_scanout->command_buffer = kernel_pinned_alloc(4096u);
    out_scanout->framebuffer = (uint32_t *) kernel_pinned_alloc(out_scanout->framebuffer_size);
    if (out_scanout->command_buffer == (void *) 0 || out_scanout->framebuffer == (void *) 0)
        goto fail;

    if (!hardware_abstraction_layer_graphics_memory_physical_address(out_scanout->command_buffer,
                                                                     &out_scanout->command_buffer_physical))
        goto fail;

    memset(out_scanout->framebuffer, 0, out_scanout->framebuffer_size);

    if (!create_resource_2d(out_scanout) || !attach_backing(out_scanout) || !set_scanout(out_scanout))
        goto fail;

    out_scanout->ready = 1u;
    return true;

fail:
    if (out_scanout->framebuffer != (void *) 0)
        kernel_pinned_free(out_scanout->framebuffer, out_scanout->framebuffer_size);
    if (out_scanout->command_buffer != (void *) 0)
        kernel_pinned_free(out_scanout->command_buffer, 4096u);
    *out_scanout = (hardware_abstraction_layer_virtio_gpu_scanout_t){0};
    return false;
}

bool hardware_abstraction_layer_virtio_gpu_flush(hardware_abstraction_layer_virtio_gpu_scanout_t *scanout)
{
    if (scanout == (void *) 0 || !scanout->ready)
        return false;

    return transfer_to_host(scanout) && flush_resource(scanout);
}

bool hardware_abstraction_layer_virtio_gpu_display_init(void)
{
    if (g_display_ready)
        return true;

    hardware_abstraction_layer_virtio_gpu_info_t info;
    if (!hardware_abstraction_layer_virtio_gpu_probe(&info))
        return false;
    if (!hardware_abstraction_layer_virtio_gpu_map(&info, &g_mapping))
        return false;
    if (!hardware_abstraction_layer_virtio_gpu_bringup(&g_mapping, &g_device))
        return false;
    if (!hardware_abstraction_layer_virtio_gpu_setup_queue(&g_device, &g_mapping, 0u, &g_controlq))
        return false;
    if (hardware_abstraction_layer_virtio_gpu_driver_ok(&g_device) == 0u)
        return false;

    uint32_t width = 0u;
    uint32_t height = 0u;
    preferred_geometry(&width, &height);

    if (!hardware_abstraction_layer_virtio_gpu_create_scanout(&g_controlq, width, height, &g_scanout))
        return false;

    g_display_ready = true;
    return true;
}

bool hardware_abstraction_layer_virtio_gpu_display_active(void) { return g_display_ready; }

bool hardware_abstraction_layer_virtio_gpu_display_query(
    hardware_abstraction_layer_surface_descriptor_t *out_descriptor)
{
    if (out_descriptor == (void *) 0 || !g_display_ready)
        return false;

    out_descriptor->buffer = g_scanout.framebuffer;
    out_descriptor->physical_address = 0u;
    out_descriptor->width = g_scanout.width;
    out_descriptor->height = g_scanout.height;
    out_descriptor->pitch = g_scanout.width * 4u;
    out_descriptor->bits_per_pixel = 32u;
    return true;
}

void hardware_abstraction_layer_virtio_gpu_display_clear(uint32_t color_rgb)
{
    if (!g_display_ready)
        return;

    const uint32_t pixels = g_scanout.width * g_scanout.height;
    for (uint32_t i = 0u; i < pixels; ++i)
        g_scanout.framebuffer[i] = color_rgb;
}

uint32_t hardware_abstraction_layer_virtio_gpu_display_read_pixel(uint32_t x, uint32_t y)
{
    if (!g_display_ready || x >= g_scanout.width || y >= g_scanout.height)
        return 0u;
    return g_scanout.framebuffer[y * g_scanout.width + x] & 0x00FFFFFFu;
}

void hardware_abstraction_layer_virtio_gpu_display_present(void)
{
    if (g_display_ready)
        (void) hardware_abstraction_layer_virtio_gpu_flush(&g_scanout);
}
