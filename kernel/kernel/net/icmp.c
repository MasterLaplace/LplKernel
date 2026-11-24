#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/icmp.h>

#include <string.h>

bool kernel_internet_control_message_parse(const uint8_t *bytes, uint32_t length, KernelInternetControlMessage_t *out)
{
    if (length < KERNEL_INTERNET_CONTROL_MESSAGE_HEADER_BYTES)
        return false;

    const uint32_t sum = kernel_internet_checksum_add(0u, bytes, length);

    if (kernel_internet_checksum_finish(sum) != 0u)
        return false;
    out->type = bytes[0];
    out->code = bytes[1];
    out->checksum = kernel_network_read_16(bytes + 2u);
    out->sum = sum;
    out->bytes = bytes;
    out->length = length;
    return true;
}

bool kernel_internet_control_message_is_echo_request(const KernelInternetControlMessage_t *message)
{
    return message->type == KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST && message->code == 0u;
}

void kernel_internet_control_message_write_echo_reply(uint8_t *reply, const KernelInternetControlMessage_t *request)
{
    const uint32_t reply_sum = request->sum - request->checksum - ((uint32_t) request->type << 8) +
                               ((uint32_t) KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY << 8);

    memmove(reply, request->bytes, request->length);
    reply[0] = KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY;
    kernel_network_write_16(reply + 2u, kernel_internet_checksum_finish(reply_sum));
}
