#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/icmp.h>

#include <string.h>

bool kernel_internet_control_message_is_echo_request(const uint8_t *bytes, uint32_t length)
{
    return length >= KERNEL_INTERNET_CONTROL_MESSAGE_HEADER_BYTES &&
           bytes[0] == KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST && bytes[1] == 0u &&
           kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, bytes, length)) == 0u;
}

void kernel_internet_control_message_write_echo_reply(uint8_t *reply, const uint8_t *request, uint32_t length)
{
    memmove(reply, request, length);
    reply[0] = KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY;
    kernel_network_write_16(reply + 2u, 0u);
    kernel_network_write_16(reply + 2u,
                            kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, reply, length)));
}
