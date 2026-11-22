#include <kernel/net/byte_order.h>

#include <stdbool.h>
#include <stdint.h>

static inline void kernel_network_station_copy(uint8_t *destination, const uint8_t *source)
{
    __builtin_memcpy(destination, source, KERNEL_NETWORK_ADDRESS_BYTES);
}

static inline bool kernel_network_station_equals(const uint8_t *first, const uint8_t *second)
{
    return kernel_network_load_32(first) == kernel_network_load_32(second) &&
           kernel_network_load_16(first + 4u) == kernel_network_load_16(second + 4u);
}

static inline const uint8_t *kernel_network_receive_bytes(const KernelNetworkDevice_t *device,
                                                          const KernelNetworkQueue_t *queue, uint32_t slot)
{
    return queue->receive_buffers + slot * device->buffer_bytes;
}

static inline uint16_t kernel_network_receive_length(const KernelNetworkQueue_t *queue, uint32_t slot)
{
    return queue->receive_lengths[slot * queue->receive_length_stride];
}

static inline uint8_t *kernel_network_transmit_buffer(const KernelNetworkDevice_t *device,
                                                      const KernelNetworkQueue_t *queue, uint32_t slot)
{
    return queue->transmit_buffers + slot * device->buffer_bytes;
}

static inline bool kernel_network_transmit_describe(const KernelNetworkDevice_t *device,
                                                    const KernelNetworkQueue_t *queue, uint32_t slot, uint32_t length)
{
    if (length - device->frame_least_bytes > device->frame_most_bytes - device->frame_least_bytes)
        return false;
    queue->transmit_words[slot * queue->transmit_word_stride] = length | queue->transmit_command;
    return true;
}
