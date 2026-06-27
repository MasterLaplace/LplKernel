/**
 * @file hal_virtio_gpu.c
 * @brief VirtIO-GPU PCI discovery for the engine HAL (P4 display hardening).
 *
 * Probe-only: locate a virtio-gpu PCI function (Red Hat / virtio vendor 0x1AF4,
 * GPU device id 0x1050 modern or 0x1010 transitional) and decode its first MMIO
 * BAR. This is the discovery seam the VirtIO-GPU display backend builds on; the
 * full 2D lifecycle (virtqueues, RESOURCE_CREATE_2D / ATTACH_BACKING /
 * SET_SCANOUT / TRANSFER_TO_HOST_2D / RESOURCE_FLUSH) is layered on top later.
 *
 * The kernel owns this driver (thin HAL): all renderer/scene logic stays
 * engine-side; the HAL only finds the device and hands back its MMIO window.
 */
#include <kernel/hal/hal.h>

#include <kernel/cpu/pci.h>
#include <kernel/cpu/paging.h>
#include <kernel/memory/vmm.h>
#include <kernel/memory/pinned_memory.h>

/* Red Hat / virtio PCI vendor id. */
#define VIRTIO_PCI_VENDOR_ID 0x1AF4u

/* virtio-gpu PCI device ids: modern is 0x1040 + virtio device type (16 = GPU)
 * = 0x1050; the transitional device exposes the legacy id 0x1010. */
#define VIRTIO_GPU_DEVICE_ID_MODERN 0x1050u
#define VIRTIO_GPU_DEVICE_ID_TRANSITIONAL 0x1010u

/* Modern virtio-pci devices use ids >= 0x1040. */
#define VIRTIO_PCI_MODERN_DEVICE_ID_BASE 0x1040u

/* PCI status-register bit 4 advertises a capability list; the list head is a
 * byte pointer at config offset 0x34. Each capability begins with an 8-bit id
 * followed by an 8-bit pointer to the next capability (0 terminates). */
#define PCI_STATUS_CAPABILITIES_LIST 0x0010u
#define PCI_REGISTER_CAPABILITIES_POINTER 0x34u
#define PCI_CAP_ID_VENDOR_SPECIFIC 0x09u

/* Layout of a virtio_pci_cap structure inside the capability list. */
#define VIRTIO_PCI_CAP_OFFSET_CFG_TYPE 3u  /* u8  cfg_type                */
#define VIRTIO_PCI_CAP_OFFSET_BAR      4u  /* u8  bar index               */
#define VIRTIO_PCI_CAP_OFFSET_OFFSET   8u  /* le32 offset within the BAR  */
#define VIRTIO_PCI_CAP_OFFSET_LENGTH   12u /* le32 length of the structure */
#define VIRTIO_PCI_NOTIFY_CAP_OFFSET_MULTIPLIER 16u /* le32 (notify only) */

/* Guard against a malformed / cyclic capability list. */
#define VIRTIO_PCI_CAP_WALK_LIMIT 48u

/* virtio_pci_common_cfg field offsets (virtio 1.x spec, little-endian). */
#define VIRTIO_PCI_COMMON_DEVICE_FEATURE_SELECT 0x00u /* le32 */
#define VIRTIO_PCI_COMMON_DEVICE_FEATURE        0x04u /* le32 */
#define VIRTIO_PCI_COMMON_DRIVER_FEATURE_SELECT 0x08u /* le32 */
#define VIRTIO_PCI_COMMON_DRIVER_FEATURE        0x0Cu /* le32 */
#define VIRTIO_PCI_COMMON_NUM_QUEUES            0x12u /* le16 */
#define VIRTIO_PCI_COMMON_DEVICE_STATUS         0x14u /* u8   */
#define VIRTIO_PCI_COMMON_QUEUE_SELECT          0x16u /* le16 */
#define VIRTIO_PCI_COMMON_QUEUE_SIZE            0x18u /* le16 */
#define VIRTIO_PCI_COMMON_QUEUE_ENABLE          0x1Cu /* le16 */
#define VIRTIO_PCI_COMMON_QUEUE_NOTIFY_OFF      0x1Eu /* le16 */
#define VIRTIO_PCI_COMMON_QUEUE_DESC            0x20u /* le64 */
#define VIRTIO_PCI_COMMON_QUEUE_DRIVER          0x28u /* le64 (avail ring)   */
#define VIRTIO_PCI_COMMON_QUEUE_DEVICE          0x30u /* le64 (used ring)    */

/* device_status bits. */
#define VIRTIO_STATUS_ACKNOWLEDGE 0x01u
#define VIRTIO_STATUS_DRIVER      0x02u
#define VIRTIO_STATUS_DRIVER_OK   0x04u
#define VIRTIO_STATUS_FEATURES_OK 0x08u
#define VIRTIO_STATUS_FAILED      0x80u

/* VIRTIO_F_VERSION_1 is feature bit 32 -> bit 0 of feature word 1. */
#define VIRTIO_FEATURE_WORD_VERSION_1 1u
#define VIRTIO_F_VERSION_1_BIT        0x00000001u

/* MMIO accessors over the cache-disabled BAR mapping. */
static inline uint8_t mmio_read8(uint32_t address) { return *(volatile uint8_t *) address; }
static inline uint16_t mmio_read16(uint32_t address) { return *(volatile uint16_t *) address; }
static inline uint32_t mmio_read32(uint32_t address) { return *(volatile uint32_t *) address; }
static inline void mmio_write8(uint32_t address, uint8_t value) { *(volatile uint8_t *) address = value; }
static inline void mmio_write16(uint32_t address, uint16_t value) { *(volatile uint16_t *) address = value; }
static inline void mmio_write32(uint32_t address, uint32_t value) { *(volatile uint32_t *) address = value; }

static bool is_virtio_gpu(const PeripheralComponentInterconnectDevice_t *dev)
{
    if (dev->vendor_id != VIRTIO_PCI_VENDOR_ID)
        return false;
    return dev->device_id == VIRTIO_GPU_DEVICE_ID_MODERN ||
           dev->device_id == VIRTIO_GPU_DEVICE_ID_TRANSITIONAL;
}

bool hal_virtio_gpu_probe(hal_virtio_gpu_info_t *out_info)
{
    if (out_info == (void *) 0)
        return false;

    /* Zero-initialise so present=false / all-zero on no match. */
    *out_info = (hal_virtio_gpu_info_t){0};

    const uint32_t count = peripheral_component_interconnect_get_device_count();
    for (uint32_t i = 0u; i < count; ++i)
    {
        const PeripheralComponentInterconnectDevice_t *dev =
            peripheral_component_interconnect_get_device(i);
        if (dev == (void *) 0 || !is_virtio_gpu(dev))
            continue;

        out_info->present = true;
        out_info->bus = dev->bus;
        out_info->device = dev->device;
        out_info->function = dev->function;
        out_info->device_id = dev->device_id;
        out_info->is_modern = (dev->device_id >= VIRTIO_PCI_MODERN_DEVICE_ID_BASE) ? 1u : 0u;

        /* Decode the first implemented MMIO BAR — modern virtio-pci puts its
         * notify/common/device config in memory space. */
        for (uint8_t index = 0u; index < PERIPHERAL_COMPONENT_INTERCONNECT_BASE_ADDRESS_REGISTER_COUNT; ++index)
        {
            PeripheralComponentInterconnectBaseAddressRegister_t bar;
            if (!peripheral_component_interconnect_read_base_address_register(dev->bus, dev->device, dev->function,
                                                                             index, &bar))
                continue;
            if (bar.is_io)
            {
                if (bar.is_64bit)
                    ++index; /* a 64-bit BAR consumes the next slot */
                continue;
            }
            out_info->mmio_base = (uint32_t) bar.base;
            out_info->mmio_size = (uint32_t) bar.size;
            out_info->mmio_bar_index = bar.index;
            break;
        }
        return true;
    }
    return false;
}

/* Select the destination cap slot for a virtio cfg_type, or NULL to ignore. */
static hal_virtio_pci_cap_t *cap_slot_for_type(hal_virtio_gpu_mapping_t *mapping, uint8_t cfg_type)
{
    switch (cfg_type)
    {
        case HAL_VIRTIO_PCI_CAP_COMMON_CFG: return &mapping->common;
        case HAL_VIRTIO_PCI_CAP_NOTIFY_CFG: return &mapping->notify;
        case HAL_VIRTIO_PCI_CAP_ISR_CFG:    return &mapping->isr;
        case HAL_VIRTIO_PCI_CAP_DEVICE_CFG: return &mapping->device;
        default:                            return (void *) 0;
    }
}

/* Walk the PCI capability list, recording every virtio cfg structure. */
static void walk_virtio_capabilities(const hal_virtio_gpu_info_t *info, hal_virtio_gpu_mapping_t *mapping)
{
    const uint16_t status =
        peripheral_component_interconnect_config_read_word(info->bus, info->device, info->function,
                                                           PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_STATUS);
    if ((status & PCI_STATUS_CAPABILITIES_LIST) == 0u)
        return;

    uint8_t pointer = peripheral_component_interconnect_config_read_byte(info->bus, info->device, info->function,
                                                                        PCI_REGISTER_CAPABILITIES_POINTER);
    for (uint32_t guard = 0u; pointer != 0u && guard < VIRTIO_PCI_CAP_WALK_LIMIT; ++guard)
    {
        const uint8_t cap_id = peripheral_component_interconnect_config_read_byte(info->bus, info->device,
                                                                                 info->function, pointer);
        const uint8_t next = peripheral_component_interconnect_config_read_byte(
            info->bus, info->device, info->function, (uint8_t) (pointer + 1u));

        if (cap_id == PCI_CAP_ID_VENDOR_SPECIFIC)
        {
            const uint8_t cfg_type = peripheral_component_interconnect_config_read_byte(
                info->bus, info->device, info->function, (uint8_t) (pointer + VIRTIO_PCI_CAP_OFFSET_CFG_TYPE));
            hal_virtio_pci_cap_t *slot = cap_slot_for_type(mapping, cfg_type);
            if (slot != (void *) 0)
            {
                slot->present = 1u;
                slot->bar = peripheral_component_interconnect_config_read_byte(
                    info->bus, info->device, info->function, (uint8_t) (pointer + VIRTIO_PCI_CAP_OFFSET_BAR));
                slot->offset = peripheral_component_interconnect_config_read_dword(
                    info->bus, info->device, info->function, (uint8_t) (pointer + VIRTIO_PCI_CAP_OFFSET_OFFSET));
                slot->length = peripheral_component_interconnect_config_read_dword(
                    info->bus, info->device, info->function, (uint8_t) (pointer + VIRTIO_PCI_CAP_OFFSET_LENGTH));
                if (cfg_type == HAL_VIRTIO_PCI_CAP_NOTIFY_CFG)
                    mapping->notify_off_multiplier = peripheral_component_interconnect_config_read_dword(
                        info->bus, info->device, info->function,
                        (uint8_t) (pointer + VIRTIO_PCI_NOTIFY_CAP_OFFSET_MULTIPLIER));
            }
        }
        pointer = next;
    }
}

/* Map an MMIO BAR window into kernel virtual space (cache-disabled).
 * Returns the VA base of the BAR (page-aligned), or 0 on failure. */
static uint32_t map_mmio_window(uint32_t phys_base_raw, uint32_t size)
{
    if (phys_base_raw == 0u || size == 0u)
        return 0u;

    const uint32_t page_offset = phys_base_raw & 0xFFFu;
    const uint32_t phys_base = phys_base_raw & 0xFFFFF000u;
    const uint32_t span = page_offset + size;
    const uint32_t page_count = (span + 0xFFFu) >> 12;

    /* Let the VMM pick a free virtual range (collision-safe) and map each page
     * onto the device's physical BAR pages. */
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
    /* VA of the BAR base; caller adds each cap.offset to reach a structure. */
    return virt_base + page_offset;
}

bool hal_virtio_gpu_map(const hal_virtio_gpu_info_t *info, hal_virtio_gpu_mapping_t *out_mapping)
{
    if (info == (void *) 0 || out_mapping == (void *) 0 || !info->present)
        return false;

    *out_mapping = (hal_virtio_gpu_mapping_t){0};

    walk_virtio_capabilities(info, out_mapping);

    /* The common configuration structure is mandatory for a usable device. */
    if (!out_mapping->common.present)
        return false;

    /* The cfg structures live in the BAR named by the capability (typically a
     * single shared BAR), NOT necessarily the first MMIO BAR the probe found.
     * Decode that BAR's real base + size and map the whole window. */
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

bool hal_virtio_gpu_bringup(const hal_virtio_gpu_mapping_t *mapping, hal_virtio_gpu_device_t *out_device)
{
    if (mapping == (void *) 0 || out_device == (void *) 0 || !mapping->mapped || !mapping->common.present)
        return false;

    *out_device = (hal_virtio_gpu_device_t){0};

    /* The common cfg lives at the mapped BAR base + the capability offset. */
    const uint32_t common = mapping->mmio_virtual_base + mapping->common.offset;
    const uint32_t status_reg = common + VIRTIO_PCI_COMMON_DEVICE_STATUS;

    /* 1. Reset: write 0, then re-read until the device clears its status. */
    mmio_write8(status_reg, 0u);
    (void) mmio_read8(status_reg); /* flush the reset */

    /* 2. Acknowledge the device and signal that a driver is present. */
    mmio_write8(status_reg, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write8(status_reg, (uint8_t) (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER));

    /* 3. Negotiate features: accept only VIRTIO_F_VERSION_1 (modern device). */
    mmio_write32(common + VIRTIO_PCI_COMMON_DRIVER_FEATURE_SELECT, VIRTIO_FEATURE_WORD_VERSION_1);
    mmio_write32(common + VIRTIO_PCI_COMMON_DRIVER_FEATURE, VIRTIO_F_VERSION_1_BIT);
    mmio_write32(common + VIRTIO_PCI_COMMON_DRIVER_FEATURE_SELECT, 0u);
    mmio_write32(common + VIRTIO_PCI_COMMON_DRIVER_FEATURE, 0u);

    /* 4. Commit FEATURES_OK and verify the device kept it set. */
    mmio_write8(status_reg,
                (uint8_t) (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK));
    const uint8_t status = mmio_read8(status_reg);

    out_device->device_status = status;
    out_device->mmio_virtual_base = mapping->mmio_virtual_base;
    out_device->common_cfg_address = common;
    out_device->num_queues = mmio_read16(common + VIRTIO_PCI_COMMON_NUM_QUEUES);

    if ((status & VIRTIO_STATUS_FEATURES_OK) == 0u || (status & VIRTIO_STATUS_FAILED) != 0u)
        return false; /* device rejected our feature set */

    /* 5. Read the size of each virtqueue (DRIVER_OK deferred until rings exist). */
    const uint16_t queue_count =
        (out_device->num_queues < HAL_VIRTIO_GPU_MAX_QUEUES) ? out_device->num_queues : HAL_VIRTIO_GPU_MAX_QUEUES;
    for (uint16_t queue = 0u; queue < queue_count; ++queue)
    {
        mmio_write16(common + VIRTIO_PCI_COMMON_QUEUE_SELECT, queue);
        out_device->queue_size[queue] = mmio_read16(common + VIRTIO_PCI_COMMON_QUEUE_SIZE);
    }

    out_device->ready = 1u;
    return true;
}

/* Split-virtqueue ring sizes for a queue of @p size descriptors. */
#define VIRTQ_DESC_BYTES(size)  (16u * (uint32_t) (size))
#define VIRTQ_AVAIL_BYTES(size) (6u + 2u * (uint32_t) (size))
#define VIRTQ_USED_BYTES(size)  (6u + 8u * (uint32_t) (size))

/* Round @p value up to the next multiple of @p align (a power of two). */
static inline uint32_t align_up(uint32_t value, uint32_t align) { return (value + (align - 1u)) & ~(align - 1u); }

/* Program a 64-bit common-cfg field from a 32-bit physical address (hi = 0). */
static void write_queue_address64(uint32_t common, uint32_t field_offset, uint32_t physical)
{
    mmio_write32(common + field_offset, physical);
    mmio_write32(common + field_offset + 4u, 0u);
}

bool hal_virtio_gpu_setup_queue(const hal_virtio_gpu_device_t *device, const hal_virtio_gpu_mapping_t *mapping,
                                uint16_t queue_index, hal_virtio_virtqueue_t *out_queue)
{
    if (device == (void *) 0 || mapping == (void *) 0 || out_queue == (void *) 0 || !device->ready)
        return false;

    *out_queue = (hal_virtio_virtqueue_t){0};

    const uint32_t common = device->common_cfg_address;
    mmio_write16(common + VIRTIO_PCI_COMMON_QUEUE_SELECT, queue_index);

    const uint16_t queue_size = mmio_read16(common + VIRTIO_PCI_COMMON_QUEUE_SIZE);
    if (queue_size == 0u)
        return false; /* queue not available */

    /* Lay the descriptor table, avail ring and used ring out inside one page. */
    const uint32_t avail_offset = VIRTQ_DESC_BYTES(queue_size);
    const uint32_t used_offset = align_up(avail_offset + VIRTQ_AVAIL_BYTES(queue_size), 16u);
    const uint32_t total = used_offset + VIRTQ_USED_BYTES(queue_size);
    if (total > 4096u)
        return false; /* would need a multi-page (non-contiguous) backing */

    void *backing = kernel_pinned_alloc(4096u);
    if (backing == (void *) 0)
        return false;

    /* Zero the rings (avail/used idx must start at 0). */
    for (uint32_t i = 0u; i < 4096u / 4u; ++i)
        ((volatile uint32_t *) backing)[i] = 0u;

    uint32_t ring_physical = 0u;
    if (!hal_graphics_memory_physical_address(backing, &ring_physical))
    {
        kernel_pinned_free(backing, 4096u);
        return false;
    }

    const uint32_t backing_va = (uint32_t) backing;

    /* Notify address: notify BAR offset + queue_notify_off * multiplier. */
    const uint16_t notify_off = mmio_read16(common + VIRTIO_PCI_COMMON_QUEUE_NOTIFY_OFF);
    const uint32_t notify_address =
        mapping->mmio_virtual_base + mapping->notify.offset + (uint32_t) notify_off * mapping->notify_off_multiplier;

    /* Program the ring physical addresses and enable the queue. */
    write_queue_address64(common, VIRTIO_PCI_COMMON_QUEUE_DESC, ring_physical);
    write_queue_address64(common, VIRTIO_PCI_COMMON_QUEUE_DRIVER, ring_physical + avail_offset);
    write_queue_address64(common, VIRTIO_PCI_COMMON_QUEUE_DEVICE, ring_physical + used_offset);
    mmio_write16(common + VIRTIO_PCI_COMMON_QUEUE_ENABLE, 1u);

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

uint8_t hal_virtio_gpu_driver_ok(const hal_virtio_gpu_device_t *device)
{
    if (device == (void *) 0 || !device->ready)
        return 0u;

    const uint32_t status_reg = device->common_cfg_address + VIRTIO_PCI_COMMON_DEVICE_STATUS;
    const uint8_t status =
        (uint8_t) (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    mmio_write8(status_reg, status);
    return mmio_read8(status_reg);
}
