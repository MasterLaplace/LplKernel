#include <stdbool.h>
#include <stdint.h>

static inline const uint8_t *e1000_receive_bytes(const E1000Receive_t *receive, uint32_t slot)
{
    return receive->buffers + slot * E1000_BUFFER_BYTES;
}

static inline uint16_t e1000_receive_length(const E1000Receive_t *receive, uint32_t slot)
{
    return receive->descriptors[slot].length;
}

static inline uint8_t e1000_receive_status(const E1000Receive_t *receive, uint32_t slot)
{
    return (uint8_t) receive->descriptors[slot].status_errors_special;
}

static inline uint8_t e1000_receive_errors(const E1000Receive_t *receive, uint32_t slot)
{
    return (uint8_t) (receive->descriptors[slot].status_errors_special >> 8u);
}

static inline uint8_t *e1000_transmit_buffer(const E1000Transmit_t *transmit, uint32_t slot)
{
    return transmit->buffers + slot * E1000_BUFFER_BYTES;
}

static inline bool e1000_transmit_describe(E1000Transmit_t *transmit, uint32_t slot, uint32_t length)
{
    if (length - E1000_FRAME_LEAST_BYTES > E1000_FRAME_MOST_BYTES - E1000_FRAME_LEAST_BYTES)
        return false;
    transmit->descriptors[slot].length_and_command = length | E1000_TRANSMIT_FRAME_COMMAND;
    return true;
}
