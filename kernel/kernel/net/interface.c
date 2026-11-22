#include <kernel/net/byte_order.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>

#include <stddef.h>
#include <string.h>

#define KERNEL_NETWORK_INTERFACE_CLASS_MASK      0xF0000000u /**< The four bits that tell 224/4 and 240/4. */
#define KERNEL_NETWORK_INTERFACE_MULTICAST_CLASS 0xE0000000u /**< 224/4, the multicast groups. */
#define KERNEL_NETWORK_INTERFACE_RESERVED_CLASS  0xF0000000u /**< 240/4, reserved but the limited broadcast. */
#define KERNEL_NETWORK_INTERFACE_GROUP_LOW_BITS  0x007FFFFFu /**< The group bits a multicast station carries. */

/**
 * @brief The broadcast station address, every bit set.
 */
static const uint8_t kernel_network_interface_broadcast[KERNEL_NETWORK_ADDRESS_BYTES] = {0xFFu, 0xFFu, 0xFFu,
                                                                                         0xFFu, 0xFFu, 0xFFu};

/**
 * @brief The prefix RFC 1112 (6.4) puts in front of a multicast group's low 23 bits.
 */
static const uint8_t kernel_network_interface_multicast_prefix[3] = {0x01u, 0x00u, 0x5Eu};

/**
 * @brief The queue pair the interface uses: the card's first.
 */
static KernelNetworkQueue_t *kernel_network_interface_queue(const KernelNetworkInterface_t *interface)
{
    return &interface->device->queues[0];
}

/**
 * @brief Whether @p address is the limited broadcast or the subnet's directed one.
 */
static bool kernel_network_interface_is_broadcast(const KernelNetworkInterface_t *interface, uint32_t address)
{
    const bool has_broadcast = (~interface->netmask & 0xFFFFFFFEu) != 0u;
    const bool on_subnet = ((address ^ interface->address) & interface->netmask) == 0u;

    return address == KERNEL_INTERNET_PROTOCOL_LIMITED_BROADCAST ||
           (has_broadcast && on_subnet && (address | interface->netmask) == KERNEL_INTERNET_PROTOCOL_LIMITED_BROADCAST);
}

/**
 * @brief Whether @p address is a multicast group.
 */
static bool kernel_network_interface_is_multicast(uint32_t address)
{
    return (address & KERNEL_NETWORK_INTERFACE_CLASS_MASK) == KERNEL_NETWORK_INTERFACE_MULTICAST_CLASS;
}

/**
 * @brief Whether no packet leaves for @p address: this network, loopback, or 240/4 but the limited broadcast.
 */
static bool kernel_network_interface_is_unreachable(uint32_t address)
{
    const uint32_t first_byte = address >> 24;

    return first_byte == 0u || first_byte == KERNEL_INTERNET_PROTOCOL_LOOPBACK_NETWORK ||
           ((address & KERNEL_NETWORK_INTERFACE_CLASS_MASK) == KERNEL_NETWORK_INTERFACE_RESERVED_CLASS &&
            address != KERNEL_INTERNET_PROTOCOL_LIMITED_BROADCAST);
}

/**
 * @brief The next hop of @p destination: itself on the subnet, the gateway off it.
 */
static uint32_t kernel_network_interface_next_hop(const KernelNetworkInterface_t *interface, uint32_t destination)
{
    const bool on_subnet = ((destination ^ interface->address) & interface->netmask) == 0u;

    return on_subnet || interface->gateway == 0u ? destination : interface->gateway;
}

/**
 * @brief Whether an ARP sender may be held in the cache: a unicast address of the subnet, not the interface's own.
 */
static bool kernel_network_interface_may_learn(const KernelNetworkInterface_t *interface, uint32_t sender)
{
    return ((sender ^ interface->address) & interface->netmask) == 0u &&
           kernel_internet_protocol_source_is_acceptable(sender, interface->address, interface->netmask);
}

/**
 * @brief Reserves one transmit slot, kicking the card once a batch to take back what it sent when none is free.
 *
 * @return The slot's buffer, or NULL when the ring stays full: the rest of the batch then sends nothing.
 */
static uint8_t *kernel_network_interface_reserve_frame(KernelNetworkInterface_t *interface, uint32_t *out_slot)
{
    KernelNetworkDevice_t *const device = interface->device;
    KernelNetworkQueue_t *const queue = kernel_network_interface_queue(interface);

    if (kernel_spsc_ring_reserve(queue->transmit, 1u, out_slot) == 0u)
    {
        if (interface->transmit_exhausted)
            return NULL;
        (void) device->operations->kick(device, 0u);
        if (kernel_spsc_ring_reserve(queue->transmit, 1u, out_slot) == 0u)
        {
            interface->transmit_exhausted = true;
            return NULL;
        }
    }
    return kernel_network_transmit_buffer(device, queue, *out_slot);
}

/**
 * @brief Pads the frame of @p length bytes in transmit slot @p slot with zeros to the card's shortest, describes
 *        and publishes it.
 *
 * @return false, the slot left unpublished, when the card's describe refuses the length.
 */
static bool kernel_network_interface_publish_frame(KernelNetworkInterface_t *interface, uint32_t slot, uint8_t *frame,
                                                   uint32_t length)
{
    KernelNetworkDevice_t *const device = interface->device;
    KernelNetworkQueue_t *const queue = kernel_network_interface_queue(interface);

    if (length < device->frame_least_bytes)
    {
        memset(frame + length, 0, device->frame_least_bytes - length);
        length = device->frame_least_bytes;
    }
    if (!kernel_network_transmit_describe(device, queue, slot, length))
        return false;
    kernel_spsc_ring_publish(queue->transmit, 1u);
    return true;
}

/**
 * @brief Sends the ARP packet @p packet to @p destination.
 *
 * @return true when published; otherwise false, @p out_failure then saying why.
 */
static bool kernel_network_interface_send_address_resolution(KernelNetworkInterface_t *interface,
                                                             const uint8_t *destination,
                                                             const KernelAddressResolutionPacket_t *packet,
                                                             KernelNetworkInterfaceFate_t *out_failure)
{
    uint32_t slot = 0u;
    uint8_t *const frame = kernel_network_interface_reserve_frame(interface, &slot);

    if (frame == NULL)
    {
        *out_failure = KERNEL_NETWORK_INTERFACE_ANSWER_RING_FULL;
        return false;
    }
    kernel_ethernet_write_header(frame, destination, interface->device->address,
                                 KERNEL_ETHERNET_TYPE_ADDRESS_RESOLUTION);
    kernel_address_resolution_write(frame + KERNEL_ETHERNET_HEADER_BYTES, packet);
    if (!kernel_network_interface_publish_frame(interface, slot, frame,
                                                KERNEL_ETHERNET_HEADER_BYTES + KERNEL_ADDRESS_RESOLUTION_BYTES))
    {
        *out_failure = KERNEL_NETWORK_INTERFACE_ANSWER_REFUSED;
        return false;
    }
    return true;
}

/**
 * @brief Answers the ARP request @p request with the interface's station.
 */
static KernelNetworkInterfaceFate_t
kernel_network_interface_answer_address_resolution(KernelNetworkInterface_t *interface,
                                                   const KernelAddressResolutionPacket_t *request)
{
    KernelAddressResolutionPacket_t reply = {.operation = KERNEL_ADDRESS_RESOLUTION_REPLY,
                                             .sender_address = interface->address,
                                             .target_address = request->sender_address};
    KernelNetworkInterfaceFate_t failure = KERNEL_NETWORK_INTERFACE_FATES;

    memcpy(reply.sender_station, interface->device->address, KERNEL_NETWORK_ADDRESS_BYTES);
    memcpy(reply.target_station, request->sender_station, KERNEL_NETWORK_ADDRESS_BYTES);
    if (!kernel_network_interface_send_address_resolution(interface, request->sender_station, &reply, &failure))
        return failure;
    return KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED;
}

/**
 * @brief The fate of an ARP packet: merged into the cache when its sender is held, the sender added when it asks
 *        the interface, and a request for the interface's address answered.
 */
static KernelNetworkInterfaceFate_t
kernel_network_interface_take_address_resolution(KernelNetworkInterface_t *interface,
                                                 const KernelEthernetFrame_t *frame, uint32_t now)
{
    KernelAddressResolutionPacket_t packet;

    if (!kernel_address_resolution_parse(frame->payload, frame->payload_bytes, &packet))
        return KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_MALFORMED;
    if (packet.operation != KERNEL_ADDRESS_RESOLUTION_REQUEST && packet.operation != KERNEL_ADDRESS_RESOLUTION_REPLY)
        return KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_OPERATION_UNKNOWN;
    if (kernel_ethernet_is_group(packet.sender_station))
        return KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_FROM_GROUP;

    const bool learnable = kernel_network_interface_may_learn(interface, packet.sender_address);
    const bool merged = learnable && kernel_address_resolution_cache_merge(&interface->cache, packet.sender_address,
                                                                           packet.sender_station, now);

    if (packet.target_address != interface->address)
        return merged ? KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_MERGED :
                        KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_FOR_OTHERS;
    if (packet.operation == KERNEL_ADDRESS_RESOLUTION_REPLY)
    {
        if (!learnable)
            return KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_SENDER_REFUSED;
        return merged ? KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REPLY_LEARNED :
                        KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REPLY_UNSOLICITED;
    }
    if (learnable && !merged)
        (void) kernel_address_resolution_cache_add(&interface->cache, packet.sender_address, packet.sender_station,
                                                   now);
    return kernel_network_interface_answer_address_resolution(interface, &packet);
}

/**
 * @brief Answers the echo request @p request, carried by @p packet, back to the station that sent its frame.
 */
static KernelNetworkInterfaceFate_t kernel_network_interface_answer_echo(KernelNetworkInterface_t *interface,
                                                                         const KernelEthernetFrame_t *frame,
                                                                         const KernelInternetProtocolPacket_t *packet,
                                                                         const KernelInternetControlMessage_t *request)
{
    const uint32_t length =
        KERNEL_ETHERNET_HEADER_BYTES + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES + packet->payload_bytes;
    uint32_t slot = 0u;

    if (length > interface->device->frame_most_bytes)
        return KERNEL_NETWORK_INTERFACE_ECHO_TOO_LONG;

    uint8_t *const reply = kernel_network_interface_reserve_frame(interface, &slot);

    if (reply == NULL)
        return KERNEL_NETWORK_INTERFACE_ANSWER_RING_FULL;
    kernel_ethernet_write_header(reply, frame->source, interface->device->address,
                                 KERNEL_ETHERNET_TYPE_INTERNET_PROTOCOL_VERSION_4);
    kernel_internet_protocol_write_header(reply + KERNEL_ETHERNET_HEADER_BYTES, interface->address, packet->source,
                                          KERNEL_INTERNET_PROTOCOL_CONTROL_MESSAGE, packet->payload_bytes,
                                          interface->next_identification++);
    kernel_internet_control_message_write_echo_reply(
        reply + KERNEL_ETHERNET_HEADER_BYTES + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES, request);
    if (!kernel_network_interface_publish_frame(interface, slot, reply, length))
        return KERNEL_NETWORK_INTERFACE_ANSWER_REFUSED;
    return KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED;
}

/**
 * @brief The fate of an ICMP message sent to the interface: an echo request answered, the rest counted.
 */
static KernelNetworkInterfaceFate_t
kernel_network_interface_take_control_message(KernelNetworkInterface_t *interface, const KernelEthernetFrame_t *frame,
                                              const KernelInternetProtocolPacket_t *packet)
{
    KernelInternetControlMessage_t message;

    if (!kernel_internet_control_message_parse(packet->payload, packet->payload_bytes, &message))
        return KERNEL_NETWORK_INTERFACE_CONTROL_MESSAGE_MALFORMED;
    if (kernel_internet_control_message_is_echo_request(&message))
        return kernel_network_interface_answer_echo(interface, frame, packet, &message);
    if (message.type == KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY)
        return KERNEL_NETWORK_INTERFACE_ECHO_REPLY_RECEIVED;
    return KERNEL_NETWORK_INTERFACE_CONTROL_MESSAGE_IGNORED;
}

/**
 * @brief The fate of an IPv4 packet: what is the interface's handed to its protocol.
 *
 * @return Its fate, or KERNEL_NETWORK_INTERFACE_FATES when its parse refused it, then counted by status.
 */
static KernelNetworkInterfaceFate_t kernel_network_interface_take_internet_protocol(KernelNetworkInterface_t *interface,
                                                                                    const KernelEthernetFrame_t *frame)
{
    KernelInternetProtocolPacket_t packet;
    const KernelInternetProtocolStatus_t status =
        kernel_internet_protocol_parse(frame->payload, frame->payload_bytes, &packet);

    if (status != KERNEL_INTERNET_PROTOCOL_TAKEN)
    {
        ++interface->counters.internet_protocol_refused[status];
        return KERNEL_NETWORK_INTERFACE_FATES;
    }
    if (packet.destination != interface->address)
        return KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_FOR_OTHERS;
    if (kernel_ethernet_is_group(frame->destination))
        return KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_LINK_BROADCAST;
    if (!kernel_internet_protocol_source_is_acceptable(packet.source, interface->address, interface->netmask))
        return KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_SOURCE_REFUSED;
    if (packet.protocol != KERNEL_INTERNET_PROTOCOL_CONTROL_MESSAGE)
        return KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_UNHANDLED;
    return kernel_network_interface_take_control_message(interface, frame, &packet);
}

/**
 * @brief The fate of one frame the card received, its length not yet believed.
 */
static KernelNetworkInterfaceFate_t kernel_network_interface_take_frame(KernelNetworkInterface_t *interface,
                                                                        const uint8_t *bytes, uint32_t length,
                                                                        uint32_t now)
{
    KernelEthernetFrame_t frame;

    if (length > interface->device->buffer_bytes)
        return KERNEL_NETWORK_INTERFACE_FRAME_OVERSIZED;
    if (!kernel_ethernet_parse(bytes, length, &frame))
        return KERNEL_NETWORK_INTERFACE_FRAME_TRUNCATED;
    if (!kernel_ethernet_is_for(&frame, interface->device->address))
        return KERNEL_NETWORK_INTERFACE_FRAME_FOR_OTHERS;
    if (kernel_ethernet_is_group(frame.source))
        return KERNEL_NETWORK_INTERFACE_FRAME_FROM_GROUP;
    if (frame.type == KERNEL_ETHERNET_TYPE_ADDRESS_RESOLUTION)
        return kernel_network_interface_take_address_resolution(interface, &frame, now);
    if (frame.type == KERNEL_ETHERNET_TYPE_INTERNET_PROTOCOL_VERSION_4)
        return kernel_network_interface_take_internet_protocol(interface, &frame);
    return KERNEL_NETWORK_INTERFACE_FRAME_OF_UNKNOWN_TYPE;
}

/**
 * @brief Why @p device cannot carry an interface, or KERNEL_NETWORK_INTERFACE_READY.
 */
static KernelNetworkInterfaceSetup_t kernel_network_interface_check_device(const KernelNetworkDevice_t *device)
{
    if (device == NULL)
        return KERNEL_NETWORK_INTERFACE_NO_DEVICE;

    const KernelNetworkQueue_t *const queue = device->queues;

    if (device->queue_count == 0u || queue == NULL || queue->receive == NULL || queue->transmit == NULL ||
        queue->receive_buffers == NULL || queue->receive_lengths == NULL || queue->transmit_buffers == NULL ||
        queue->transmit_words == NULL)
        return KERNEL_NETWORK_INTERFACE_NO_QUEUE;
    if (device->operations == NULL || device->operations->harvest == NULL || device->operations->kick == NULL ||
        device->operations->link_up == NULL)
        return KERNEL_NETWORK_INTERFACE_NO_OPERATIONS;
    if (device->frame_least_bytes > device->frame_most_bytes || device->frame_most_bytes > device->buffer_bytes ||
        device->frame_most_bytes < KERNEL_ETHERNET_FRAME_LEAST_BYTES)
        return KERNEL_NETWORK_INTERFACE_BAD_FRAME_BOUNDS;
    return KERNEL_NETWORK_INTERFACE_READY;
}

KernelNetworkInterfaceSetup_t kernel_network_interface_initialize(KernelNetworkInterface_t *interface,
                                                                  KernelNetworkDevice_t *device, uint32_t address,
                                                                  uint32_t netmask, uint32_t gateway,
                                                                  uint32_t cache_lifetime)
{
    const KernelNetworkInterfaceSetup_t setup = kernel_network_interface_check_device(device);

    if (setup != KERNEL_NETWORK_INTERFACE_READY)
        return setup;
    memset(interface, 0, sizeof(*interface));
    interface->device = device;
    interface->address = address;
    interface->netmask = netmask;
    interface->gateway = gateway;
    kernel_address_resolution_cache_initialize(&interface->cache, cache_lifetime);
    return KERNEL_NETWORK_INTERFACE_READY;
}

uint32_t kernel_network_interface_poll(KernelNetworkInterface_t *interface, uint32_t now)
{
    KernelNetworkDevice_t *const device = interface->device;
    const KernelNetworkDevice_t card = *device;
    const KernelNetworkQueue_t queue = *kernel_network_interface_queue(interface);

    interface->transmit_exhausted = false;
    kernel_address_resolution_cache_expire(&interface->cache, now);
    (void) card.operations->harvest(device, 0u);

    const uint32_t ready = kernel_spsc_ring_ready_count(queue.receive);

    for (uint32_t frame = 0u; frame < ready; ++frame)
    {
        const uint32_t slot = kernel_spsc_ring_slot_at(queue.receive, frame);
        const KernelNetworkInterfaceFate_t fate =
            kernel_network_interface_take_frame(interface, kernel_network_receive_bytes(&card, &queue, slot),
                                                kernel_network_receive_length(&queue, slot), now);

        ++interface->counters.frames_received;
        if (fate != KERNEL_NETWORK_INTERFACE_FATES)
            ++interface->counters.fates[fate];
    }
    kernel_spsc_ring_release(queue.receive, ready);
    (void) card.operations->kick(device, 0u);
    return ready;
}

KernelNetworkInterfaceRequest_t kernel_network_interface_request_address(KernelNetworkInterface_t *interface,
                                                                         uint32_t destination, uint32_t now)
{
    if (kernel_network_interface_is_unreachable(destination))
        return KERNEL_NETWORK_INTERFACE_REQUEST_UNREACHABLE;
    if (kernel_network_interface_is_broadcast(interface, destination) ||
        kernel_network_interface_is_multicast(destination))
        return KERNEL_NETWORK_INTERFACE_REQUEST_NEEDLESS;

    const uint32_t next_hop = kernel_network_interface_next_hop(interface, destination);
    KernelAddressResolutionPacket_t request = {.operation = KERNEL_ADDRESS_RESOLUTION_REQUEST,
                                               .sender_address = interface->address,
                                               .target_address = next_hop};
    KernelNetworkInterfaceFate_t failure = KERNEL_NETWORK_INTERFACE_FATES;

    kernel_address_resolution_cache_ask(&interface->cache, next_hop, now);
    memcpy(request.sender_station, interface->device->address, KERNEL_NETWORK_ADDRESS_BYTES);
    if (!kernel_network_interface_send_address_resolution(interface, kernel_network_interface_broadcast, &request,
                                                          &failure))
    {
        ++interface->counters.address_resolution_requests_unsent;
        return failure == KERNEL_NETWORK_INTERFACE_ANSWER_RING_FULL ? KERNEL_NETWORK_INTERFACE_REQUEST_RING_FULL :
                                                                      KERNEL_NETWORK_INTERFACE_REQUEST_REFUSED;
    }
    ++interface->counters.address_resolution_requests_sent;
    return KERNEL_NETWORK_INTERFACE_REQUEST_SENT;
}

bool kernel_network_interface_find_station(const KernelNetworkInterface_t *interface, uint32_t destination,
                                           uint32_t now, uint8_t *out_station)
{
    if (kernel_network_interface_is_unreachable(destination))
        return false;
    if (kernel_network_interface_is_broadcast(interface, destination))
    {
        memcpy(out_station, kernel_network_interface_broadcast, KERNEL_NETWORK_ADDRESS_BYTES);
        return true;
    }
    if (kernel_network_interface_is_multicast(destination))
    {
        memcpy(out_station, kernel_network_interface_multicast_prefix,
               sizeof(kernel_network_interface_multicast_prefix));
        out_station[3] = (uint8_t) ((destination & KERNEL_NETWORK_INTERFACE_GROUP_LOW_BITS) >> 16);
        out_station[4] = (uint8_t) (destination >> 8);
        out_station[5] = (uint8_t) destination;
        return true;
    }
    return kernel_address_resolution_cache_find(
        &interface->cache, kernel_network_interface_next_hop(interface, destination), now, out_station);
}
