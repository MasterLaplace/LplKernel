#include <stdbool.h>
#include <stdint.h>

static inline void kernel_network_station_copy(uint8_t *destination, const uint8_t *source)
{
    __builtin_memcpy(destination, source, KERNEL_NETWORK_ADDRESS_BYTES);
}

static inline bool kernel_network_station_equals(const uint8_t *first, const uint8_t *second)
{
    uint32_t first_head = 0u;
    uint32_t second_head = 0u;
    uint16_t first_tail = 0u;
    uint16_t second_tail = 0u;

    __builtin_memcpy(&first_head, first, sizeof(first_head));
    __builtin_memcpy(&second_head, second, sizeof(second_head));
    __builtin_memcpy(&first_tail, first + sizeof(first_head), sizeof(first_tail));
    __builtin_memcpy(&second_tail, second + sizeof(second_head), sizeof(second_tail));
    return first_head == second_head && first_tail == second_tail;
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
