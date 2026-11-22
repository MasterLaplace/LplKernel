#include <kernel/net/byte_order.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>

#include <stddef.h>
#include <string.h>

/**
 * @brief The broadcast station address, every bit set.
 */
static const uint8_t kernel_network_interface_broadcast[KERNEL_NETWORK_ADDRESS_BYTES] = {0xFFu, 0xFFu, 0xFFu,
                                                                                         0xFFu, 0xFFu, 0xFFu};

/**
 * @brief The queue pair the interface uses: the card's first.
 */
static KernelNetworkQueue_t *kernel_network_interface_queue(const KernelNetworkInterface_t *interface)
{
    return &interface->device->queues[0];
}

/**
 * @brief Reserves one transmit slot, kicking the card once to take back what it sent when none is free.
 *
 * @return The slot's buffer, or NULL, counted, when the ring stays full.
 */
static uint8_t *kernel_network_interface_reserve_frame(KernelNetworkInterface_t *interface, uint32_t *out_slot)
{
    KernelNetworkDevice_t *const device = interface->device;
    KernelNetworkQueue_t *const queue = kernel_network_interface_queue(interface);

    if (kernel_spsc_ring_reserve(queue->transmit, 1u, out_slot) == 0u)
    {
        (void) device->operations->kick(device, 0u);
        if (kernel_spsc_ring_reserve(queue->transmit, 1u, out_slot) == 0u)
        {
            ++interface->counters.transmit_ring_full;
            return NULL;
        }
    }
    return kernel_network_transmit_buffer(device, queue, *out_slot);
}

/**
 * @brief Describes and publishes the frame of @p length bytes written into transmit slot @p slot.
 */
static void kernel_network_interface_publish_frame(KernelNetworkInterface_t *interface, uint32_t slot, uint32_t length)
{
    KernelNetworkQueue_t *const queue = kernel_network_interface_queue(interface);

    if (kernel_network_transmit_describe(interface->device, queue, slot, length))
        kernel_spsc_ring_publish(queue->transmit, 1u);
}

/**
 * @brief Sends the ARP packet @p packet to @p destination.
 */
static bool kernel_network_interface_send_address_resolution(KernelNetworkInterface_t *interface,
                                                             const uint8_t *destination,
                                                             const KernelAddressResolutionPacket_t *packet)
{
    uint32_t slot = 0u;
    uint8_t *const frame = kernel_network_interface_reserve_frame(interface, &slot);

    if (frame == NULL)
        return false;
    kernel_ethernet_write_header(frame, destination, interface->device->address, KERNEL_ETHERNET_TYPE_ARP);
    kernel_address_resolution_write(frame + KERNEL_ETHERNET_HEADER_BYTES, packet);
    kernel_network_interface_publish_frame(interface, slot,
                                           KERNEL_ETHERNET_HEADER_BYTES + KERNEL_ADDRESS_RESOLUTION_BYTES);
    return true;
}

/**
 * @brief Learns the sender of an ARP packet about the interface's address, and answers it when it asks.
 */
static void kernel_network_interface_take_address_resolution(KernelNetworkInterface_t *interface,
                                                             const KernelEthernetFrame_t *frame, uint32_t now)
{
    KernelAddressResolutionPacket_t packet;

    if (!kernel_address_resolution_parse(frame->payload, frame->payload_bytes, &packet))
    {
        ++interface->counters.address_resolution_malformed;
        return;
    }
    if (packet.target_address != interface->address)
    {
        ++interface->counters.address_resolution_for_others;
        return;
    }
    kernel_address_resolution_cache_learn(&interface->cache, packet.sender_address, packet.sender_station, now);
    if (packet.operation != KERNEL_ADDRESS_RESOLUTION_REQUEST)
    {
        interface->counters.address_resolution_replies_learned +=
            packet.operation == KERNEL_ADDRESS_RESOLUTION_REPLY ? 1u : 0u;
        return;
    }

    KernelAddressResolutionPacket_t reply = {.operation = KERNEL_ADDRESS_RESOLUTION_REPLY,
                                             .sender_address = interface->address,
                                             .target_address = packet.sender_address};

    memcpy(reply.sender_station, interface->device->address, KERNEL_NETWORK_ADDRESS_BYTES);
    memcpy(reply.target_station, packet.sender_station, KERNEL_NETWORK_ADDRESS_BYTES);
    if (kernel_network_interface_send_address_resolution(interface, packet.sender_station, &reply))
        ++interface->counters.address_resolution_requests_answered;
}

/**
 * @brief Answers an echo request from @p packet's source, back to the station that sent its frame.
 */
static void kernel_network_interface_answer_echo(KernelNetworkInterface_t *interface,
                                                 const KernelEthernetFrame_t *frame,
                                                 const KernelInternetProtocolPacket_t *packet)
{
    const uint32_t length =
        KERNEL_ETHERNET_HEADER_BYTES + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES + packet->payload_bytes;
    uint32_t slot = 0u;

    if (length > interface->device->frame_most_bytes)
    {
        ++interface->counters.control_messages_ignored;
        return;
    }

    uint8_t *const reply = kernel_network_interface_reserve_frame(interface, &slot);

    if (reply == NULL)
        return;
    kernel_ethernet_write_header(reply, frame->source, interface->device->address, KERNEL_ETHERNET_TYPE_IPV4);
    kernel_internet_protocol_write_header(reply + KERNEL_ETHERNET_HEADER_BYTES, interface->address, packet->source,
                                          KERNEL_INTERNET_PROTOCOL_ICMP, packet->payload_bytes,
                                          interface->next_identification++);
    kernel_internet_control_message_write_echo_reply(reply + KERNEL_ETHERNET_HEADER_BYTES +
                                                         KERNEL_INTERNET_PROTOCOL_HEADER_BYTES,
                                                     packet->payload, packet->payload_bytes);
    kernel_network_interface_publish_frame(interface, slot, length);
    ++interface->counters.echo_requests_answered;
}

/**
 * @brief Takes an ICMP message sent to the interface: answers an echo request, counts the rest.
 */
static void kernel_network_interface_take_control_message(KernelNetworkInterface_t *interface,
                                                          const KernelEthernetFrame_t *frame,
                                                          const KernelInternetProtocolPacket_t *packet)
{
    if (kernel_internet_control_message_is_echo_request(packet->payload, packet->payload_bytes))
    {
        kernel_network_interface_answer_echo(interface, frame, packet);
        return;
    }
    if (packet->payload_bytes != 0u && packet->payload[0] == KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY)
        ++interface->counters.echo_replies_received;
    else
        ++interface->counters.control_messages_ignored;
}

/**
 * @brief Takes an IPv4 packet: checks it, and hands what is the interface's to its protocol.
 */
static void kernel_network_interface_take_internet_protocol(KernelNetworkInterface_t *interface,
                                                            const KernelEthernetFrame_t *frame)
{
    KernelInternetProtocolPacket_t packet;
    const KernelInternetProtocolStatus_t status =
        kernel_internet_protocol_parse(frame->payload, frame->payload_bytes, &packet);

    if (status != KERNEL_INTERNET_PROTOCOL_TAKEN)
    {
        ++interface->counters.internet_protocol_refused[status];
        return;
    }
    if (packet.destination != interface->address)
    {
        ++interface->counters.internet_protocol_for_others;
        return;
    }
    if (packet.protocol == KERNEL_INTERNET_PROTOCOL_ICMP)
        kernel_network_interface_take_control_message(interface, frame, &packet);
    else
        ++interface->counters.internet_protocol_unhandled;
}

/**
 * @brief Takes one frame the card received.
 */
static void kernel_network_interface_take_frame(KernelNetworkInterface_t *interface, const uint8_t *bytes,
                                                uint32_t length, uint32_t now)
{
    KernelEthernetFrame_t frame;

    ++interface->counters.frames_received;
    if (!kernel_ethernet_parse(bytes, length, &frame))
    {
        ++interface->counters.frames_truncated;
        return;
    }
    if (!kernel_ethernet_is_for(&frame, interface->device->address))
    {
        ++interface->counters.frames_for_others;
        return;
    }
    if (frame.type == KERNEL_ETHERNET_TYPE_ARP)
        kernel_network_interface_take_address_resolution(interface, &frame, now);
    else if (frame.type == KERNEL_ETHERNET_TYPE_IPV4)
        kernel_network_interface_take_internet_protocol(interface, &frame);
    else
        ++interface->counters.frames_of_unknown_type;
}

bool kernel_network_interface_initialize(KernelNetworkInterface_t *interface, KernelNetworkDevice_t *device,
                                         uint32_t address, uint32_t netmask, uint32_t gateway, uint32_t cache_lifetime)
{
    if (device == NULL || device->queue_count == 0u)
        return false;
    memset(interface, 0, sizeof(*interface));
    interface->device = device;
    interface->address = address;
    interface->netmask = netmask;
    interface->gateway = gateway;
    kernel_address_resolution_cache_initialize(&interface->cache, cache_lifetime);
    return true;
}

uint32_t kernel_network_interface_poll(KernelNetworkInterface_t *interface, uint32_t now)
{
    KernelNetworkDevice_t *const device = interface->device;
    KernelNetworkQueue_t *const queue = kernel_network_interface_queue(interface);

    (void) device->operations->harvest(device, 0u);

    const uint32_t ready = kernel_spsc_ring_ready_count(queue->receive);

    for (uint32_t frame = 0u; frame < ready; ++frame)
    {
        const uint32_t slot = kernel_spsc_ring_slot_at(queue->receive, frame);

        kernel_network_interface_take_frame(interface, kernel_network_receive_bytes(device, queue, slot),
                                            kernel_network_receive_length(queue, slot), now);
    }
    kernel_spsc_ring_release(queue->receive, ready);
    (void) device->operations->kick(device, 0u);
    return ready;
}

bool kernel_network_interface_request_address(KernelNetworkInterface_t *interface, uint32_t target)
{
    KernelAddressResolutionPacket_t request = {.operation = KERNEL_ADDRESS_RESOLUTION_REQUEST,
                                               .sender_address = interface->address,
                                               .target_address = target};

    memcpy(request.sender_station, interface->device->address, KERNEL_NETWORK_ADDRESS_BYTES);
    if (!kernel_network_interface_send_address_resolution(interface, kernel_network_interface_broadcast, &request))
        return false;
    ++interface->counters.address_resolution_requests_sent;
    (void) interface->device->operations->kick(interface->device, 0u);
    return true;
}

bool kernel_network_interface_find_station(const KernelNetworkInterface_t *interface, uint32_t destination,
                                           uint32_t now, uint8_t *out_station)
{
    const bool on_subnet = ((destination ^ interface->address) & interface->netmask) == 0u;
    const uint32_t next_hop = on_subnet || interface->gateway == 0u ? destination : interface->gateway;

    return kernel_address_resolution_cache_find(&interface->cache, next_hop, now, out_station);
}
