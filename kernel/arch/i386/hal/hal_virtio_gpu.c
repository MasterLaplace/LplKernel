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

/* Red Hat / virtio PCI vendor id. */
#define VIRTIO_PCI_VENDOR_ID 0x1AF4u

/* virtio-gpu PCI device ids: modern is 0x1040 + virtio device type (16 = GPU)
 * = 0x1050; the transitional device exposes the legacy id 0x1010. */
#define VIRTIO_GPU_DEVICE_ID_MODERN 0x1050u
#define VIRTIO_GPU_DEVICE_ID_TRANSITIONAL 0x1010u

/* Modern virtio-pci devices use ids >= 0x1040. */
#define VIRTIO_PCI_MODERN_DEVICE_ID_BASE 0x1040u

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
