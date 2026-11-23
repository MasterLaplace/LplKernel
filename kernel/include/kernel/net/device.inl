#include <stdbool.h>
#include <stdint.h>

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
