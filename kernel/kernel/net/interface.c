#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
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
 * @details A slot reserved for a datagram the caller has not sent yet is that same slot: the datagram's reservation
 *          is stale from then on, and its send says so.
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
    interface->datagram_reserved = false;
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

    kernel_network_station_copy(reply.sender_station, interface->device->address);
    kernel_network_station_copy(reply.target_station, request->sender_station);
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
 * @brief The 4 bytes at @p bytes, loaded in the host's order, whatever their alignment.
 */
static uint32_t kernel_network_interface_load_32(const uint8_t *bytes)
{
    uint32_t value = 0u;

    __builtin_memcpy(&value, bytes, sizeof(value));
    return value;
}

/**
 * @brief The 2 bytes at @p bytes, loaded in the host's order, whatever their alignment.
 */
static uint16_t kernel_network_interface_load_16(const uint8_t *bytes)
{
    uint16_t value = 0u;

    __builtin_memcpy(&value, bytes, sizeof(value));
    return value;
}

/**
 * @brief @p value, a 4-byte field in host order, as a load of it from a frame reads it.
 */
static uint32_t kernel_network_interface_as_loaded_32(uint32_t value)
{
    uint8_t bytes[4];

    kernel_network_write_32(bytes, value);
    return kernel_network_interface_load_32(bytes);
}

/**
 * @brief @p value, a 2-byte field in host order, as a load of it from a frame reads it.
 */
static uint16_t kernel_network_interface_as_loaded_16(uint16_t value)
{
    uint8_t bytes[2];

    kernel_network_write_16(bytes, value);
    return kernel_network_interface_load_16(bytes);
}

/**
 * @brief The port bound as @p port, or NULL.
 */
static KernelNetworkPortBinding_t *kernel_network_interface_port(KernelNetworkInterface_t *interface, uint16_t port)
{
    for (uint32_t place = 0u; place < KERNEL_NETWORK_INTERFACE_PORTS; ++place)
        if (interface->ports[place].port == port)
            return &interface->ports[place];
    return NULL;
}

/**
 * @brief Lends @p datagram where the card wrote it, its frame held until the consumer releases it.
 */
static KernelNetworkInterfaceFate_t kernel_network_interface_lend(KernelNetworkInterface_t *interface,
                                                                  const KernelNetworkDatagram_t *datagram)
{
    uint32_t slot = 0u;

    if (kernel_spsc_ring_reserve(&interface->lent_ring, 1u, &slot) == 0u)
    {
        kernel_spsc_ring_count_rejected(&interface->lent_ring, 1u);
        return KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT_FULL;
    }
    interface->lent[slot] = *datagram;
    kernel_spsc_ring_publish(&interface->lent_ring, 1u);
    return KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT;
}

/**
 * @brief Copies @p datagram into @p port's own ring.
 */
static KernelNetworkInterfaceFate_t kernel_network_interface_copy(const KernelNetworkPortBinding_t *port,
                                                                  const KernelNetworkDatagram_t *datagram)
{
    const KernelNetworkPortRing_t *const copies = &port->copies;
    uint32_t slot = 0u;

    if (datagram->payload_bytes > copies->buffer_bytes)
        return KERNEL_NETWORK_INTERFACE_DATAGRAM_TOO_LONG_FOR_PORT;
    if (kernel_spsc_ring_reserve(copies->ring, 1u, &slot) == 0u)
    {
        kernel_spsc_ring_count_rejected(copies->ring, 1u);
        return KERNEL_NETWORK_INTERFACE_DATAGRAM_PORT_FULL;
    }

    uint8_t *const copy = copies->buffers + slot * copies->buffer_bytes;

    memcpy(copy, datagram->payload, datagram->payload_bytes);
    copies->datagrams[slot] = *datagram;
    copies->datagrams[slot].payload = copy;
    kernel_spsc_ring_publish(copies->ring, 1u);
    return KERNEL_NETWORK_INTERFACE_DATAGRAM_COPIED;
}

/**
 * @brief The fate of a UDP datagram sent to the interface: delivered to its port, lent or copied, or refused.
 */
static KernelNetworkInterfaceFate_t
kernel_network_interface_take_user_datagram(KernelNetworkInterface_t *interface,
                                            const KernelInternetProtocolPacket_t *packet)
{
    KernelUserDatagram_t datagram;
    const KernelUserDatagramStatus_t status = kernel_user_datagram_parse(packet, &datagram);

    if (status == KERNEL_USER_DATAGRAM_TRUNCATED || status == KERNEL_USER_DATAGRAM_BAD_LENGTH)
        return KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_MALFORMED;
    if (status == KERNEL_USER_DATAGRAM_BAD_CHECKSUM)
        return KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_BAD_CHECKSUM;

    const KernelNetworkPortBinding_t *const port =
        datagram.destination_port == 0u ? NULL : kernel_network_interface_port(interface, datagram.destination_port);

    if (port == NULL)
        return KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_PORT_UNBOUND;
    if (status == KERNEL_USER_DATAGRAM_NO_CHECKSUM && !port->without_checksum)
        return KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_CHECKSUM_ABSENT;

    const KernelNetworkDatagram_t delivered = {.payload = datagram.payload,
                                               .payload_bytes = datagram.payload_bytes,
                                               .payload_sum = datagram.payload_sum,
                                               .source_address = packet->source,
                                               .source_port = datagram.source_port,
                                               .destination_port = datagram.destination_port,
                                               .arrival = interface->arrival};

    if (port->delivery == KERNEL_NETWORK_DELIVERY_LENT)
        return kernel_network_interface_lend(interface, &delivered);
    return kernel_network_interface_copy(port, &delivered);
}

/**
 * @brief The place @p port, as loaded, is bound lent at, or KERNEL_NETWORK_INTERFACE_PORTS.
 */
static uint32_t kernel_network_interface_lent_place(const KernelNetworkInterfaceWire_t *wire, uint16_t port)
{
    uint32_t place = 0u;

    while (place < KERNEL_NETWORK_INTERFACE_PORTS && (port == 0u || wire->lent_ports[place] != port))
        ++place;
    return place;
}

/**
 * @brief Whether an IPv4 header's five words, loaded in the host's order, sum to FFFFh once folded: its checksum
 *        holds, whatever the host's order, since FFFFh reads the same both ways.
 */
static bool kernel_network_interface_header_holds(uint32_t first, uint32_t second, uint32_t third, uint32_t fourth,
                                                  uint32_t fifth)
{
    const uint64_t sum = (uint64_t) first + second + third + fourth + fifth;
    uint32_t folded = (uint32_t) (sum & 0xFFFFu) + (uint32_t) ((sum >> 16) & 0xFFFFu) + (uint32_t) (sum >> 32);

    folded = (folded & 0xFFFFu) + (folded >> 16);
    folded = (folded & 0xFFFFu) + (folded >> 16);
    return folded == 0xFFFFu;
}

/**
 * @brief Whether @p source, host order, may send to the interface: what kernel_internet_protocol_source_is_acceptable
 *        decides, on the subnet's values the interface computed once.
 */
static bool kernel_network_interface_accepts_source(const KernelNetworkInterface_t *interface, uint32_t source)
{
    const uint32_t first_byte = source >> 24;
    const KernelNetworkInterfaceWire_t *const wire = &interface->wire;

    if (first_byte - 1u >= KERNEL_INTERNET_PROTOCOL_FIRST_MULTICAST_NETWORK - 1u ||
        first_byte == KERNEL_INTERNET_PROTOCOL_LOOPBACK_NETWORK || source == interface->address)
        return false;
    return !(wire->has_broadcast && (source == wire->subnet_network || source == wire->subnet_broadcast));
}

/**
 * @brief Lends the frame of @p length bytes at @p frame when it is the common datagram, every check passed.
 *
 * @return true when lent; false for any other frame, which the general path then takes and counts by its cause.
 */
static bool kernel_network_interface_lend_common(KernelNetworkInterface_t *interface, const uint8_t *frame,
                                                 uint32_t length, uint32_t buffer_bytes, uint32_t arrival)
{
    const KernelNetworkInterfaceWire_t *const wire = &interface->wire;
    const uint8_t *const header = frame + KERNEL_ETHERNET_HEADER_BYTES;
    const uint8_t *const datagram = header + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES;

    if (length < KERNEL_NETWORK_INTERFACE_HEADERS_BYTES || length > buffer_bytes ||
        kernel_network_interface_load_32(frame) != wire->station_head ||
        kernel_network_interface_load_16(frame + 4u) != wire->station_tail ||
        kernel_ethernet_is_group(frame + KERNEL_NETWORK_ADDRESS_BYTES) ||
        (kernel_network_interface_load_32(frame + 12u) & kernel_network_interface_as_loaded_32(0xFFFFFF00u)) !=
            kernel_network_interface_as_loaded_32(0x08004500u))
        return false;

    const uint32_t first = kernel_network_interface_load_32(header);
    const uint32_t second = kernel_network_interface_load_32(header + 4u);
    const uint32_t third = kernel_network_interface_load_32(header + 8u);
    const uint32_t source_loaded = kernel_network_interface_load_32(header + 12u);
    const uint32_t destination_loaded = kernel_network_interface_load_32(header + 16u);
    const uint32_t total_bytes = kernel_network_read_16(header + 2u);

    if (destination_loaded != wire->address || (second & kernel_network_interface_as_loaded_32(0x00003FFFu)) != 0u ||
        (third & kernel_network_interface_as_loaded_32(0x00FF0000u)) !=
            kernel_network_interface_as_loaded_32((uint32_t) KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM << 16) ||
        total_bytes < KERNEL_INTERNET_PROTOCOL_HEADER_BYTES + KERNEL_USER_DATAGRAM_HEADER_BYTES ||
        total_bytes > length - KERNEL_ETHERNET_HEADER_BYTES ||
        !kernel_network_interface_header_holds(first, second, third, source_loaded, destination_loaded))
        return false;

    const uint32_t source = kernel_network_read_32(header + 12u);
    const uint32_t datagram_bytes = kernel_network_read_16(datagram + 4u);
    const uint32_t place = kernel_network_interface_lent_place(wire, kernel_network_interface_load_16(datagram + 2u));

    if (!kernel_network_interface_accepts_source(interface, source) ||
        datagram_bytes < KERNEL_USER_DATAGRAM_HEADER_BYTES ||
        datagram_bytes > total_bytes - KERNEL_INTERNET_PROTOCOL_HEADER_BYTES || place == KERNEL_NETWORK_INTERFACE_PORTS)
        return false;

    const uint16_t source_port = kernel_network_read_16(datagram);
    const uint16_t destination_port = kernel_network_read_16(datagram + 2u);
    const uint16_t checksum = kernel_network_read_16(datagram + 6u);
    const uint32_t payload_bytes = datagram_bytes - KERNEL_USER_DATAGRAM_HEADER_BYTES;
    const uint32_t payload_sum =
        kernel_internet_checksum_fold_bytes(datagram + KERNEL_USER_DATAGRAM_HEADER_BYTES, payload_bytes);
    const uint32_t sum = wire->pseudo_header_sum + (source >> 16) + (source & 0xFFFFu) + 2u * datagram_bytes +
                         source_port + destination_port + checksum + payload_sum;
    uint32_t slot = 0u;

    if ((checksum == 0u && !interface->ports[place].without_checksum) ||
        (checksum != 0u && kernel_internet_checksum_finish(sum) != 0u) ||
        kernel_spsc_ring_reserve(&interface->lent_ring, 1u, &slot) == 0u)
        return false;

    KernelNetworkDatagram_t *const lent = &interface->lent[slot];

    lent->payload = datagram + KERNEL_USER_DATAGRAM_HEADER_BYTES;
    lent->payload_bytes = payload_bytes;
    lent->payload_sum = payload_sum;
    lent->source_address = source;
    lent->source_port = source_port;
    lent->destination_port = destination_port;
    lent->arrival = arrival;
    kernel_spsc_ring_publish(&interface->lent_ring, 1u);
    return true;
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
    if (packet.protocol == KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM)
        return kernel_network_interface_take_user_datagram(interface, &packet);
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
 * @brief Gives the card back every frame taken from its oldest held up to the oldest a lent datagram lies in.
 */
static void kernel_network_interface_give_back(KernelNetworkInterface_t *interface)
{
    uint32_t oldest_lent = 0u;
    uint32_t held_from = interface->receive_released + interface->receive_taken;

    if (kernel_spsc_ring_peek(&interface->lent_ring, 1u, &oldest_lent) == 1u)
        held_from = interface->lent[oldest_lent].arrival;

    const uint32_t frames = held_from - interface->receive_released;

    kernel_spsc_ring_release(kernel_network_interface_queue(interface)->receive, frames);
    interface->receive_released = held_from;
    interface->receive_taken -= frames;
}

/**
 * @brief Whether @p binding can be bound: a port other than 0, a known delivery, and a copied port's whole ring.
 */
static bool kernel_network_interface_binding_is_sound(const KernelNetworkPortBinding_t *binding)
{
    const KernelNetworkPortRing_t *const copies = &binding->copies;

    if (binding->port == 0u)
        return false;
    if (binding->delivery == KERNEL_NETWORK_DELIVERY_LENT)
        return true;
    return binding->delivery == KERNEL_NETWORK_DELIVERY_COPIED && copies->ring != NULL && copies->datagrams != NULL &&
           copies->buffers != NULL && copies->buffer_bytes != 0u;
}

/**
 * @brief The checksum @p datagram carries, the payload at @p payload summed when the caller gave no sum.
 *
 * @return false when its checksum is to be none and its source port is not bound to send none.
 */
static bool kernel_network_interface_datagram_checksum(KernelNetworkInterface_t *interface,
                                                       const KernelNetworkDatagramOut_t *datagram,
                                                       const uint8_t *payload, uint16_t *out_checksum)
{
    uint32_t payload_sum = datagram->payload_sum;

    if (datagram->checksum == KERNEL_NETWORK_CHECKSUM_NONE)
    {
        const KernelNetworkPortBinding_t *const port = kernel_network_interface_port(interface, datagram->source_port);

        *out_checksum = 0u;
        return datagram->source_port != 0u && port != NULL && port->without_checksum;
    }
    if (datagram->checksum != KERNEL_NETWORK_CHECKSUM_FROM_SUM)
    {
        payload_sum = kernel_internet_checksum_fold_bytes(payload, datagram->payload_bytes);
        ++interface->counters.checksums_computed;
    }
    *out_checksum = kernel_user_datagram_checksum(
        kernel_user_datagram_pseudo_header_sum(interface->address, datagram->destination_address,
                                               KERNEL_USER_DATAGRAM_HEADER_BYTES + datagram->payload_bytes),
        datagram->source_port, datagram->destination_port, datagram->payload_bytes, payload_sum);
    return true;
}

/**
 * @brief The station a datagram to @p destination goes to, its next hop's looked up from the entry a send last found
 *        for that hop: KERNEL_NETWORK_INTERFACE_SEND_SENT with @p out_station written, or why none.
 *
 * @details A next hop not learned is asked for by ARP, unless a request for it is already pending.
 */
static KernelNetworkInterfaceSend_t kernel_network_interface_station_of(KernelNetworkInterface_t *interface,
                                                                        uint32_t destination, uint32_t now,
                                                                        uint8_t *out_station)
{
    if (kernel_network_interface_is_unreachable(destination))
        return KERNEL_NETWORK_INTERFACE_SEND_UNREACHABLE;
    if (kernel_network_interface_is_broadcast(interface, destination) ||
        kernel_network_interface_is_multicast(destination))
    {
        (void) kernel_network_interface_find_station(interface, destination, now, out_station);
        return KERNEL_NETWORK_INTERFACE_SEND_SENT;
    }

    const uint32_t next_hop = kernel_network_interface_next_hop(interface, destination);

    if (kernel_address_resolution_cache_find_hinted(
            &interface->cache, next_hop, now, out_station,
            &interface->station_hints[next_hop % KERNEL_NETWORK_INTERFACE_STATION_HINTS]))
        return KERNEL_NETWORK_INTERFACE_SEND_SENT;
    if (!kernel_address_resolution_cache_is_pending(&interface->cache, next_hop, now, interface->request_interval))
        (void) kernel_network_interface_request_address(interface, destination, now);
    return KERNEL_NETWORK_INTERFACE_SEND_NO_STATION;
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
                                                                  KernelNetworkDevice_t *device,
                                                                  const KernelNetworkInterfaceConfig_t *config)
{
    const KernelNetworkInterfaceSetup_t setup = kernel_network_interface_check_device(device);

    if (setup != KERNEL_NETWORK_INTERFACE_READY)
        return setup;
    memset(interface, 0, sizeof(*interface));
    interface->device = device;
    interface->address = config->address;
    interface->netmask = config->netmask;
    interface->gateway = config->gateway;
    interface->request_interval = config->request_interval;
    interface->wire.station_head = kernel_network_interface_load_32(device->address);
    interface->wire.station_tail = kernel_network_interface_load_16(device->address + 4u);
    interface->wire.address = kernel_network_interface_as_loaded_32(config->address);
    interface->wire.pseudo_header_sum =
        (config->address >> 16) + (config->address & 0xFFFFu) + KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM;
    interface->wire.subnet_network = config->address & config->netmask;
    interface->wire.subnet_broadcast = config->address | ~config->netmask;
    interface->wire.has_broadcast = (~config->netmask & 0xFFFFFFFEu) != 0u;
    kernel_address_resolution_cache_initialize(&interface->cache, config->cache_lifetime);
    (void) kernel_spsc_ring_initialize(&interface->lent_ring, KERNEL_NETWORK_INTERFACE_LENT_DATAGRAMS);
    return KERNEL_NETWORK_INTERFACE_READY;
}

uint32_t kernel_network_interface_poll(KernelNetworkInterface_t *interface, uint32_t now)
{
    KernelNetworkDevice_t *const device = interface->device;
    const KernelNetworkDevice_t card = *device;
    const KernelNetworkQueue_t queue = *kernel_network_interface_queue(interface);

    interface->transmit_exhausted = false;
    kernel_address_resolution_cache_expire_next(&interface->cache, now);
    (void) card.operations->harvest(device, 0u);

    const uint32_t ready = kernel_spsc_ring_ready_count(queue.receive);
    const uint32_t first = interface->receive_taken;
    uint32_t common = 0u;

    for (uint32_t frame = first; frame < ready; ++frame)
    {
        const uint32_t slot = kernel_spsc_ring_slot_at(queue.receive, frame);
        const uint32_t arrival = interface->receive_released + frame;
        const uint8_t *const bytes = kernel_network_receive_bytes(&card, &queue, slot);
        const uint32_t length = kernel_network_receive_length(&queue, slot);

        if (kernel_network_interface_lend_common(interface, bytes, length, card.buffer_bytes, arrival))
        {
            ++common;
            continue;
        }
        interface->arrival = arrival;

        const KernelNetworkInterfaceFate_t fate = kernel_network_interface_take_frame(interface, bytes, length, now);

        if (fate != KERNEL_NETWORK_INTERFACE_FATES)
            ++interface->counters.fates[fate];
    }
    interface->counters.frames_received += ready - first;
    interface->counters.fates[KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT] += common;
    interface->counters.datagrams_common += common;
    interface->receive_taken = ready;
    kernel_network_interface_give_back(interface);
    (void) card.operations->kick(device, 0u);
    return ready - first;
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
    kernel_network_station_copy(request.sender_station, interface->device->address);
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
        kernel_network_station_copy(out_station, kernel_network_interface_broadcast);
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

KernelNetworkInterfaceBind_t kernel_network_interface_bind(KernelNetworkInterface_t *interface,
                                                           const KernelNetworkPortBinding_t *binding)
{
    if (!kernel_network_interface_binding_is_sound(binding))
        return KERNEL_NETWORK_INTERFACE_BIND_BAD_BINDING;
    if (kernel_network_interface_port(interface, binding->port) != NULL)
        return KERNEL_NETWORK_INTERFACE_BIND_PORT_TAKEN;

    KernelNetworkPortBinding_t *const place = kernel_network_interface_port(interface, 0u);

    if (place == NULL)
        return KERNEL_NETWORK_INTERFACE_BIND_PORTS_FULL;
    *place = *binding;
    if (binding->delivery == KERNEL_NETWORK_DELIVERY_LENT)
        interface->wire.lent_ports[place - interface->ports] = kernel_network_interface_as_loaded_16(binding->port);
    return KERNEL_NETWORK_INTERFACE_BIND_BOUND;
}

uint32_t kernel_network_interface_lent_count(KernelNetworkInterface_t *interface)
{
    return kernel_spsc_ring_ready_count(&interface->lent_ring);
}

const KernelNetworkDatagram_t *kernel_network_interface_lent_at(const KernelNetworkInterface_t *interface,
                                                                uint32_t offset)
{
    return &interface->lent[kernel_spsc_ring_slot_at(&interface->lent_ring, offset)];
}

uint32_t kernel_network_interface_release_lent(KernelNetworkInterface_t *interface, uint32_t count)
{
    const uint32_t lent = kernel_spsc_ring_ready_count(&interface->lent_ring);
    const uint32_t released = count < lent ? count : lent;

    kernel_spsc_ring_release(&interface->lent_ring, released);
    kernel_network_interface_give_back(interface);
    return released;
}

uint8_t *kernel_network_interface_reserve_datagram(KernelNetworkInterface_t *interface, uint32_t *out_slot,
                                                   uint32_t *out_room)
{
    uint8_t *const frame = kernel_network_interface_reserve_frame(interface, out_slot);

    if (frame == NULL)
        return NULL;
    interface->datagram_reserved = true;
    interface->reserved_slot = *out_slot;
    *out_room = interface->device->frame_most_bytes - KERNEL_NETWORK_INTERFACE_HEADERS_BYTES;
    return frame + KERNEL_NETWORK_INTERFACE_HEADERS_BYTES;
}

KernelNetworkInterfaceSend_t kernel_network_interface_send_datagram(KernelNetworkInterface_t *interface, uint32_t slot,
                                                                    const KernelNetworkDatagramOut_t *datagram,
                                                                    uint32_t now)
{
    KernelNetworkDevice_t *const device = interface->device;
    const uint32_t length = KERNEL_NETWORK_INTERFACE_HEADERS_BYTES + datagram->payload_bytes;
    const bool reserved = interface->datagram_reserved && interface->reserved_slot == slot;
    uint8_t station[KERNEL_NETWORK_ADDRESS_BYTES];
    uint16_t checksum = 0u;
    KernelNetworkInterfaceSend_t result = KERNEL_NETWORK_INTERFACE_SEND_STALE_SLOT;

    interface->datagram_reserved = false;
    if (reserved)
        result = length > device->frame_most_bytes ?
                     KERNEL_NETWORK_INTERFACE_SEND_TOO_LONG :
                     kernel_network_interface_station_of(interface, datagram->destination_address, now, station);
    if (result == KERNEL_NETWORK_INTERFACE_SEND_SENT)
    {
        uint8_t *const frame = kernel_network_transmit_buffer(device, kernel_network_interface_queue(interface), slot);

        if (!kernel_network_interface_datagram_checksum(interface, datagram,
                                                        frame + KERNEL_NETWORK_INTERFACE_HEADERS_BYTES, &checksum))
            result = KERNEL_NETWORK_INTERFACE_SEND_ZERO_CHECKSUM_REFUSED;
        else
        {
            kernel_ethernet_write_header(frame, station, device->address,
                                         KERNEL_ETHERNET_TYPE_INTERNET_PROTOCOL_VERSION_4);
            kernel_internet_protocol_write_header(frame + KERNEL_ETHERNET_HEADER_BYTES, interface->address,
                                                  datagram->destination_address, KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM,
                                                  KERNEL_USER_DATAGRAM_HEADER_BYTES + datagram->payload_bytes,
                                                  interface->next_identification++);
            kernel_user_datagram_write_header(
                frame + KERNEL_ETHERNET_HEADER_BYTES + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES, datagram->source_port,
                datagram->destination_port, datagram->payload_bytes, checksum);
            if (!kernel_network_interface_publish_frame(interface, slot, frame, length))
                result = KERNEL_NETWORK_INTERFACE_SEND_REFUSED;
        }
    }
    ++interface->counters.sends[result];
    return result;
}
