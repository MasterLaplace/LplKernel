#include <kernel/net/arp.h>
#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <string.h>

#define NETWORK_FAKE_SLOTS        8u          /**< Slots of each ring of the made-up card. */
#define NETWORK_FAKE_BUFFER_BYTES 1536u       /**< Bytes of each of its buffers. */
#define NETWORK_FAKE_FRAME_MOST   1514u       /**< Its longest frame, its CRC left to it. */
#define NETWORK_FAKE_ADDRESS      0x0A000002u /**< 10.0.0.2, the interface's address. */
#define NETWORK_FAKE_NETMASK      0xFFFFFF00u /**< A /24. */
#define NETWORK_FAKE_PEER         0x0A000001u /**< 10.0.0.1, the peer's. */
#define NETWORK_FAKE_OTHER        0x0A000009u /**< 10.0.0.9, an address neither has. */
#define NETWORK_FAKE_GATEWAY      0x0A0000FEu /**< 10.0.0.254, the gateway of the tests that set one. */
#define NETWORK_FAKE_LIFETIME     100u        /**< The cache's lifetime, in the tests' time. */
#define NETWORK_FAKE_ECHO_DATA    4u          /**< Bytes of data in an echo request, after its 8 of header. */
#define NETWORK_FAKE_MESSAGE      34u         /**< Where an ICMP message starts in a frame: 14 + 20 bytes. */

/**
 * @brief A card in memory: the test produces on its receive ring, and its kick captures what the interface sends.
 */
typedef struct NetworkFakeCard {
    KernelSpscRing_t receive;                                               /**< Frames the test hands the interface. */
    KernelSpscRing_t transmit;                                              /**< Frames the interface sends. */
    uint8_t receive_buffers[NETWORK_FAKE_SLOTS][NETWORK_FAKE_BUFFER_BYTES]; /**< The receive buffers. */
    uint8_t transmit_buffers[NETWORK_FAKE_SLOTS][NETWORK_FAKE_BUFFER_BYTES]; /**< The transmit buffers. */
    uint16_t receive_lengths[NETWORK_FAKE_SLOTS];                            /**< Each receive slot's length. */
    uint32_t transmit_words[NETWORK_FAKE_SLOTS];                 /**< Each transmit slot's described word. */
    uint8_t sent[NETWORK_FAKE_SLOTS][NETWORK_FAKE_BUFFER_BYTES]; /**< The frames the kicks captured. */
    uint32_t sent_lengths[NETWORK_FAKE_SLOTS];                   /**< Their lengths. */
    uint32_t sent_count;                                         /**< How many. */
    uint32_t kicks;                                              /**< Kicks the interface made. */
    bool kick_holds;                                             /**< Whether a kick takes nothing back. */
} NetworkFakeCard_t;

static NetworkFakeCard_t network_fake_card;
static KernelNetworkQueue_t network_fake_queue;
static KernelNetworkDevice_t network_fake_device;
static KernelNetworkInterface_t network_fake_interface;
static const uint8_t network_fake_own_station[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u};
static const uint8_t network_fake_peer_station[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};
static const uint8_t network_fake_broadcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

/**
 * @brief The made-up card's harvest: the test already published its frames.
 */
static uint32_t network_fake_harvest(KernelNetworkDevice_t *device, uint32_t queue)
{
    (void) device;
    (void) queue;
    return 0u;
}

/**
 * @brief The made-up card's kick: captures every frame published, then gives their slots back, unless it holds.
 */
static uint32_t network_fake_kick(KernelNetworkDevice_t *device, uint32_t queue)
{
    uint32_t first_slot = 0u;
    uint32_t handed = 0u;

    (void) device;
    (void) queue;
    ++network_fake_card.kicks;
    if (network_fake_card.kick_holds)
        return 0u;
    for (uint32_t run = 0u; run < 2u; ++run)
    {
        const uint32_t ready = kernel_spsc_ring_peek(&network_fake_card.transmit, NETWORK_FAKE_SLOTS, &first_slot);

        for (uint32_t frame = 0u; frame < ready && network_fake_card.sent_count < NETWORK_FAKE_SLOTS; ++frame)
        {
            const uint32_t slot = first_slot + frame;
            const uint32_t length = network_fake_card.transmit_words[slot];

            memcpy(network_fake_card.sent[network_fake_card.sent_count], network_fake_card.transmit_buffers[slot],
                   length);
            network_fake_card.sent_lengths[network_fake_card.sent_count++] = length;
        }
        kernel_spsc_ring_release(&network_fake_card.transmit, ready);
        handed += ready;
    }
    return handed;
}

/**
 * @brief The made-up card's link: always up.
 */
static bool network_fake_link_up(KernelNetworkDevice_t *device)
{
    (void) device;
    return true;
}

static const KernelNetworkDeviceOperations_t network_fake_operations = {.harvest = network_fake_harvest,
                                                                        .kick = network_fake_kick,
                                                                        .link_up = network_fake_link_up};

/**
 * @brief Sets up the made-up card, empty, its shortest frame @p frame_least_bytes, without the interface.
 */
static void network_fake_set_up_card(uint32_t frame_least_bytes)
{
    memset(&network_fake_card, 0, sizeof(network_fake_card));
    (void) kernel_spsc_ring_initialize(&network_fake_card.receive, NETWORK_FAKE_SLOTS);
    (void) kernel_spsc_ring_initialize(&network_fake_card.transmit, NETWORK_FAKE_SLOTS);
    network_fake_queue = (KernelNetworkQueue_t){.receive = &network_fake_card.receive,
                                                .receive_buffers = &network_fake_card.receive_buffers[0][0],
                                                .receive_lengths = network_fake_card.receive_lengths,
                                                .receive_length_stride = 1u,
                                                .transmit = &network_fake_card.transmit,
                                                .transmit_buffers = &network_fake_card.transmit_buffers[0][0],
                                                .transmit_words = network_fake_card.transmit_words,
                                                .transmit_word_stride = 1u,
                                                .transmit_command = 0u};
    network_fake_device = (KernelNetworkDevice_t){.name = "fake",
                                                  .buffer_bytes = NETWORK_FAKE_BUFFER_BYTES,
                                                  .frame_least_bytes = frame_least_bytes,
                                                  .frame_most_bytes = NETWORK_FAKE_FRAME_MOST,
                                                  .queue_count = 1u,
                                                  .queues = &network_fake_queue,
                                                  .operations = &network_fake_operations};
    memcpy(network_fake_device.address, network_fake_own_station, 6u);
}

/**
 * @brief Sets up the made-up card, its shortest frame @p frame_least_bytes, and the interface 10.0.0.2/24 on it,
 *        through @p gateway.
 */
static void network_fake_set_up_through(uint32_t frame_least_bytes, uint32_t gateway)
{
    network_fake_set_up_card(frame_least_bytes);
    (void) kernel_network_interface_initialize(&network_fake_interface, &network_fake_device, NETWORK_FAKE_ADDRESS,
                                               NETWORK_FAKE_NETMASK, gateway, NETWORK_FAKE_LIFETIME);
}

/**
 * @brief Sets up the made-up card, its shortest frame a header, and the interface 10.0.0.2/24 without a gateway.
 */
static void network_fake_set_up(void) { network_fake_set_up_through(KERNEL_ETHERNET_HEADER_BYTES, 0u); }

/**
 * @brief Hands the interface the frame of @p length bytes at @p bytes, as the card would.
 */
static void network_fake_receive(const uint8_t *bytes, uint32_t length)
{
    uint32_t slot = 0u;

    if (kernel_spsc_ring_reserve(&network_fake_card.receive, 1u, &slot) == 0u)
        return;
    memcpy(network_fake_card.receive_buffers[slot], bytes, length);
    network_fake_card.receive_lengths[slot] = (uint16_t) length;
    kernel_spsc_ring_publish(&network_fake_card.receive, 1u);
}

/**
 * @brief Writes an ARP packet of @p operation from @p sender at @p sender_station about @p target, in a frame to
 *        @p destination.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_address_resolution(uint8_t *frame, const uint8_t *destination, uint16_t operation,
                                                      uint32_t sender, const uint8_t *sender_station, uint32_t target)
{
    KernelAddressResolutionPacket_t packet = {.operation = operation,
                                              .sender_address = sender,
                                              .target_address = target};

    memcpy(packet.sender_station, sender_station, 6u);
    kernel_ethernet_write_header(frame, destination, sender_station, KERNEL_ETHERNET_TYPE_ADDRESS_RESOLUTION);
    kernel_address_resolution_write(frame + KERNEL_ETHERNET_HEADER_BYTES, &packet);
    return KERNEL_ETHERNET_HEADER_BYTES + KERNEL_ADDRESS_RESOLUTION_BYTES;
}

/**
 * @brief Writes an ARP request from the peer for @p target, broadcast.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_request(uint8_t *frame, uint32_t target)
{
    return network_fake_write_address_resolution(frame, network_fake_broadcast, KERNEL_ADDRESS_RESOLUTION_REQUEST,
                                                 NETWORK_FAKE_PEER, network_fake_peer_station, target);
}

/**
 * @brief Writes the checksum of the ICMP message of @p message_bytes at @p message.
 */
static void network_fake_seal_control_message(uint8_t *message, uint32_t message_bytes)
{
    kernel_network_write_16(message + 2u, 0u);
    kernel_network_write_16(message + 2u,
                            kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, message, message_bytes)));
}

/**
 * @brief Writes an ICMP message of @p type with @p data_bytes of data, from @p source to @p destination, in a
 *        frame from the peer's station to @p station.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_control_message(uint8_t *frame, const uint8_t *station, uint32_t source,
                                                   uint32_t destination, uint8_t type, uint32_t data_bytes)
{
    uint8_t *const message = frame + NETWORK_FAKE_MESSAGE;
    const uint32_t message_bytes = KERNEL_INTERNET_CONTROL_MESSAGE_HEADER_BYTES + data_bytes;

    kernel_ethernet_write_header(frame, station, network_fake_peer_station,
                                 KERNEL_ETHERNET_TYPE_INTERNET_PROTOCOL_VERSION_4);
    kernel_internet_protocol_write_header(frame + KERNEL_ETHERNET_HEADER_BYTES, source, destination,
                                          KERNEL_INTERNET_PROTOCOL_CONTROL_MESSAGE, message_bytes, 3u);
    memset(message, 0x77, message_bytes);
    message[0] = type;
    message[1] = 0u;
    network_fake_seal_control_message(message, message_bytes);
    return NETWORK_FAKE_MESSAGE + message_bytes;
}

/**
 * @brief Writes an echo request from the peer to @p destination, in a frame to the interface's station.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_echo(uint8_t *frame, uint32_t destination)
{
    return network_fake_write_control_message(frame, network_fake_own_station, NETWORK_FAKE_PEER, destination,
                                              KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST, NETWORK_FAKE_ECHO_DATA);
}

/**
 * @brief Writes an ICMP message of @p type from the peer to the interface, with @p data_bytes of data.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_to_interface(uint8_t *frame, uint8_t type, uint32_t data_bytes)
{
    return network_fake_write_control_message(frame, network_fake_own_station, NETWORK_FAKE_PEER, NETWORK_FAKE_ADDRESS,
                                              type, data_bytes);
}

/**
 * @brief The frames received the interface counted, summed over every fate and every IPv4 refusal.
 */
static uint32_t network_fake_frames_counted(void)
{
    const KernelNetworkInterfaceCounters_t *const counters = &network_fake_interface.counters;
    uint32_t counted = 0u;

    for (uint32_t fate = 0u; fate < KERNEL_NETWORK_INTERFACE_FATES; ++fate)
        counted += counters->fates[fate];
    for (uint32_t status = 0u; status < KERNEL_INTERNET_PROTOCOL_STATUSES; ++status)
        counted += counters->internet_protocol_refused[status];
    return counted;
}

/**
 * @brief Checks that the interface read @p frames frames, and counted each of them once.
 */
static void network_fake_check_counted(KernelTest_t *test, uint32_t frames)
{
    kernel_test_check(
        test, network_fake_interface.counters.frames_received == frames && network_fake_frames_counted() == frames,
        "every frame read lands in exactly one fate");
}

/**
 * @brief How many frames received fell under @p fate.
 */
static uint32_t network_fake_fate(KernelNetworkInterfaceFate_t fate)
{
    return network_fake_interface.counters.fates[fate];
}

KERNEL_TEST_SUITE(network_interface_alone, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief An ARP request for the interface's address is answered to its sender, and the sender learned.
 */
KERNEL_TEST(an_arp_request_for_the_interface_is_answered_to_its_sender)
{
    uint8_t frame[64] = {0u};
    KernelEthernetFrame_t reply_frame;
    KernelAddressResolutionPacket_t reply;
    uint8_t station[6] = {0u};

    network_fake_set_up();
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    kernel_test_check(test, kernel_network_interface_poll(&network_fake_interface, 1u) == 1u,
                      "the poll reads the request");
    kernel_test_check(test, network_fake_card.sent_count == 1u && network_fake_card.sent_lengths[0] == 42u,
                      "and sends one frame of 42 bytes");
    kernel_test_check(test,
                      kernel_ethernet_parse(network_fake_card.sent[0], 42u, &reply_frame) &&
                          memcmp(reply_frame.destination, network_fake_peer_station, 6u) == 0 &&
                          reply_frame.type == KERNEL_ETHERNET_TYPE_ADDRESS_RESOLUTION &&
                          kernel_address_resolution_parse(reply_frame.payload, reply_frame.payload_bytes, &reply),
                      "an ARP frame to the peer's station");
    kernel_test_check(test,
                      reply.operation == KERNEL_ADDRESS_RESOLUTION_REPLY &&
                          reply.sender_address == NETWORK_FAKE_ADDRESS &&
                          memcmp(reply.sender_station, network_fake_own_station, 6u) == 0 &&
                          reply.target_address == NETWORK_FAKE_PEER &&
                          memcmp(reply.target_station, network_fake_peer_station, 6u) == 0,
                      "a reply that gives 10.0.0.2's station to 10.0.0.1");
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 2u, station) &&
                          memcmp(station, network_fake_peer_station, 6u) == 0,
                      "and the peer's station is learned");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED) == 1u,
                      "the request is counted as answered");
    network_fake_check_counted(test, 1u);
}

/**
 * @brief A reply shorter than the card's shortest frame is padded with zeros to it.
 */
KERNEL_TEST(an_answer_shorter_than_the_cards_shortest_frame_is_padded_with_zeros)
{
    uint8_t frame[64] = {0u};
    bool padding_zero = true;

    network_fake_set_up_through(KERNEL_ETHERNET_FRAME_LEAST_BYTES, 0u);
    memset(network_fake_card.transmit_buffers, 0xAA, sizeof(network_fake_card.transmit_buffers));
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    for (uint32_t byte = 42u; byte < KERNEL_ETHERNET_FRAME_LEAST_BYTES; ++byte)
        padding_zero = padding_zero && network_fake_card.sent[0][byte] == 0u;
    kernel_test_check(test,
                      network_fake_card.sent_count == 1u &&
                          network_fake_card.sent_lengths[0] == KERNEL_ETHERNET_FRAME_LEAST_BYTES && padding_zero,
                      "the reply of 42 bytes goes out as 60, its last 18 bytes zero");
    network_fake_check_counted(test, 1u);
}

/**
 * @brief An ARP request for another address goes unanswered and teaches nothing, but refreshes a station held.
 */
KERNEL_TEST(an_arp_packet_for_another_address_only_refreshes_a_station_held)
{
    static const uint8_t moved[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x77u};
    uint8_t frame[64] = {0u};
    uint8_t station[6] = {0u};

    network_fake_set_up();
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_OTHER));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test, network_fake_card.sent_count == 0u, "no frame is sent");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_FOR_OTHERS) == 1u,
                      "the request is counted as for another");
    kernel_test_check(test,
                      !kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 2u, station),
                      "and its sender is not learned");
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 3u);
    network_fake_receive(frame, network_fake_write_address_resolution(frame, network_fake_broadcast,
                                                                      KERNEL_ADDRESS_RESOLUTION_REQUEST,
                                                                      NETWORK_FAKE_PEER, moved, NETWORK_FAKE_PEER));
    (void) kernel_network_interface_poll(&network_fake_interface, 4u);
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 5u, station) &&
                          memcmp(station, moved, 6u) == 0,
                      "a gratuitous ARP from 10.0.0.1, once held, moves it to its new station (RFC 826, merge)");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_MERGED) == 1u,
                      "and is counted as merged");
    network_fake_check_counted(test, 3u);
}

/**
 * @brief ARP senders no entry may hold teach nothing: a probe from 0.0.0.0 and a sender off the subnet are answered,
 *        a sender station that is a group is not, and an unknown operation is counted apart.
 */
KERNEL_TEST(arp_senders_no_entry_may_hold_teach_nothing)
{
    static const uint8_t group_station[6] = {0x01u, 0x00u, 0x5Eu, 0x00u, 0x00u, 0x01u};
    uint8_t frame[64] = {0u};
    uint8_t station[6] = {0u};
    uint32_t length = 0u;

    network_fake_set_up();
    network_fake_receive(frame, network_fake_write_address_resolution(frame, network_fake_broadcast,
                                                                      KERNEL_ADDRESS_RESOLUTION_REQUEST, 0u,
                                                                      network_fake_peer_station, NETWORK_FAKE_ADDRESS));
    network_fake_receive(frame, network_fake_write_address_resolution(frame, network_fake_broadcast,
                                                                      KERNEL_ADDRESS_RESOLUTION_REQUEST, 0xC0A80101u,
                                                                      network_fake_peer_station, NETWORK_FAKE_ADDRESS));
    length = network_fake_write_address_resolution(frame, network_fake_broadcast, KERNEL_ADDRESS_RESOLUTION_REQUEST,
                                                   NETWORK_FAKE_PEER, group_station, NETWORK_FAKE_ADDRESS);
    memcpy(frame + KERNEL_NETWORK_ADDRESS_BYTES, network_fake_peer_station, 6u);
    network_fake_receive(frame, length);
    network_fake_receive(frame, network_fake_write_address_resolution(
                                    frame, network_fake_broadcast, KERNEL_ADDRESS_RESOLUTION_REQUEST,
                                    NETWORK_FAKE_ADDRESS, network_fake_peer_station, NETWORK_FAKE_ADDRESS));
    network_fake_receive(frame,
                         network_fake_write_address_resolution(frame, network_fake_broadcast, 3u, NETWORK_FAKE_PEER,
                                                               network_fake_peer_station, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED) == 3u &&
                          network_fake_card.sent_count == 3u,
                      "the probe from 0.0.0.0, the request from 192.168.1.1 and one claiming 10.0.0.2 are answered");
    kernel_test_check(
        test,
        !kernel_network_interface_find_station(&network_fake_interface, 0xC0A80101u, 2u, station) &&
            !kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_ADDRESS, 2u, station) &&
            !kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 2u, station),
        "and no sender, nor the group's, is learned");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_FROM_GROUP) == 1u,
                      "a request whose sender station is a group, in a unicast frame, is dropped, not answered to all");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_OPERATION_UNKNOWN) == 1u,
                      "and operation 3 is counted as unknown");
    network_fake_check_counted(test, 5u);
}

/**
 * @brief A reply is learned only when the interface asked for its sender; the request it asked with is a broadcast
 *        request for the next hop, with an empty target station, sent by the next poll's kick.
 */
KERNEL_TEST(a_reply_is_learned_only_when_the_interface_asked_for_it)
{
    static const uint8_t no_station[6] = {0u};
    uint8_t frame[64] = {0u};
    uint8_t station[6] = {0u};
    KernelEthernetFrame_t request_frame;
    KernelAddressResolutionPacket_t request;

    network_fake_set_up();
    network_fake_receive(frame, network_fake_write_address_resolution(
                                    frame, network_fake_own_station, KERNEL_ADDRESS_RESOLUTION_REPLY, NETWORK_FAKE_PEER,
                                    network_fake_peer_station, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(
        test,
        network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REPLY_UNSOLICITED) == 1u &&
            !kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 2u, station),
        "a reply nobody asked for creates no entry");
    kernel_test_check(test,
                      kernel_network_interface_request_address(&network_fake_interface, NETWORK_FAKE_PEER, 2u) ==
                          KERNEL_NETWORK_INTERFACE_REQUEST_SENT,
                      "the interface asks for 10.0.0.1");
    kernel_test_check(test, network_fake_card.kicks == 1u && network_fake_card.sent_count == 0u,
                      "the request waits for the next poll's kick");
    network_fake_receive(frame, network_fake_write_address_resolution(
                                    frame, network_fake_own_station, KERNEL_ADDRESS_RESOLUTION_REPLY, NETWORK_FAKE_PEER,
                                    network_fake_peer_station, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 3u);
    kernel_test_check(
        test,
        network_fake_card.sent_count == 1u &&
            kernel_ethernet_parse(network_fake_card.sent[0], network_fake_card.sent_lengths[0], &request_frame) &&
            memcmp(request_frame.destination, network_fake_broadcast, 6u) == 0 &&
            kernel_address_resolution_parse(request_frame.payload, request_frame.payload_bytes, &request),
        "the request went out broadcast");
    kernel_test_check(
        test,
        request.operation == KERNEL_ADDRESS_RESOLUTION_REQUEST && request.sender_address == NETWORK_FAKE_ADDRESS &&
            request.target_address == NETWORK_FAKE_PEER && memcmp(request.target_station, no_station, 6u) == 0,
        "as a request from 10.0.0.2 for 10.0.0.1, its target station empty");
    kernel_test_check(
        test,
        network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REPLY_LEARNED) == 1u &&
            kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 4u, station) &&
            memcmp(station, network_fake_peer_station, 6u) == 0,
        "and the reply to it is learned");
    network_fake_check_counted(test, 2u);
}

/**
 * @brief Off the subnet, the request is for the gateway, which then stays held through a flood of requests from
 *        senders that only talked to the interface.
 */
KERNEL_TEST(the_gateway_asked_for_outlives_a_flood_of_senders)
{
    static const uint8_t gateway_station[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0xFEu};
    uint8_t frame[64] = {0u};
    uint8_t station[6] = {0u};
    KernelAddressResolutionPacket_t request;
    KernelEthernetFrame_t request_frame;

    network_fake_set_up_through(KERNEL_ETHERNET_HEADER_BYTES, NETWORK_FAKE_GATEWAY);
    (void) kernel_network_interface_request_address(&network_fake_interface, 0x08080808u, 1u);
    network_fake_receive(
        frame, network_fake_write_address_resolution(frame, network_fake_own_station, KERNEL_ADDRESS_RESOLUTION_REPLY,
                                                     NETWORK_FAKE_GATEWAY, gateway_station, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 2u);
    kernel_test_check(
        test,
        kernel_ethernet_parse(network_fake_card.sent[0], network_fake_card.sent_lengths[0], &request_frame) &&
            kernel_address_resolution_parse(request_frame.payload, request_frame.payload_bytes, &request) &&
            request.target_address == NETWORK_FAKE_GATEWAY,
        "a packet to 8.8.8.8 asks for the gateway 10.0.0.254");
    for (uint32_t sender = 0u; sender < 2u * KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++sender)
    {
        const uint8_t forged[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x01u, (uint8_t) sender};

        network_fake_receive(frame, network_fake_write_address_resolution(
                                        frame, network_fake_broadcast, KERNEL_ADDRESS_RESOLUTION_REQUEST,
                                        0x0A000010u + sender, forged, NETWORK_FAKE_ADDRESS));
        if ((sender + 1u) % NETWORK_FAKE_SLOTS == 0u)
            (void) kernel_network_interface_poll(&network_fake_interface, 3u + sender);
    }
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_fake_interface, 0x08080808u, 40u, station) &&
                          memcmp(station, gateway_station, 6u) == 0,
                      "32 requests from made-up senders leave 8.8.8.8 going to the gateway's station");
    network_fake_check_counted(test, 1u + 2u * KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES);
}

/**
 * @brief An echo request to the interface is answered, addresses swapped and DF set; one to another address is not.
 */
KERNEL_TEST(an_echo_request_is_answered_only_when_it_is_the_interfaces)
{
    uint8_t frame[64] = {0u};
    KernelEthernetFrame_t reply_frame;
    KernelInternetProtocolPacket_t reply;
    KernelInternetControlMessage_t message;

    network_fake_set_up();
    network_fake_receive(frame, network_fake_write_echo(frame, NETWORK_FAKE_ADDRESS));
    network_fake_receive(frame, network_fake_write_echo(frame, NETWORK_FAKE_OTHER));
    kernel_test_check(test, kernel_network_interface_poll(&network_fake_interface, 1u) == 2u,
                      "the poll reads both requests");
    kernel_test_check(test, network_fake_card.sent_count == 1u, "and answers one");
    kernel_test_check(
        test,
        kernel_ethernet_parse(network_fake_card.sent[0], network_fake_card.sent_lengths[0], &reply_frame) &&
            memcmp(reply_frame.destination, network_fake_peer_station, 6u) == 0 &&
            kernel_internet_protocol_parse(reply_frame.payload, reply_frame.payload_bytes, &reply) ==
                KERNEL_INTERNET_PROTOCOL_TAKEN,
        "with a sound IPv4 packet back to the peer's station");
    kernel_test_check(test,
                      reply.source == NETWORK_FAKE_ADDRESS && reply.destination == NETWORK_FAKE_PEER &&
                          reply.protocol == KERNEL_INTERNET_PROTOCOL_CONTROL_MESSAGE &&
                          reply.payload_bytes ==
                              KERNEL_INTERNET_CONTROL_MESSAGE_HEADER_BYTES + NETWORK_FAKE_ECHO_DATA &&
                          (reply_frame.payload[6] & 0x40u) != 0u,
                      "from 10.0.0.2 to 10.0.0.1, its 12 bytes of ICMP, DF set");
    kernel_test_check(test,
                      kernel_internet_control_message_parse(reply.payload, reply.payload_bytes, &message) &&
                          message.type == KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY,
                      "an echo reply whose checksum holds");
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_FOR_OTHERS) == 1u &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED) == 1u,
                      "and the request to 10.0.0.9 is counted as for another");
    network_fake_check_counted(test, 2u);
}

/**
 * @brief An echo request from a source no host sends from, or to the interface in a broadcast frame, is dropped
 *        unanswered (RFC 1122, 3.2.1.3 and 3.3.6).
 */
KERNEL_TEST(an_echo_from_an_invalid_source_or_in_a_broadcast_frame_is_dropped)
{
    static const uint32_t sources[] = {0u, 0x7F000001u, 0xE0000001u, 0xFFFFFFFFu, 0x0A0000FFu, NETWORK_FAKE_ADDRESS};
    const uint32_t source_count = sizeof(sources) / sizeof(sources[0]);
    uint8_t frame[64] = {0u};

    network_fake_set_up();
    for (uint32_t source = 0u; source < source_count; ++source)
    {
        network_fake_receive(frame, network_fake_write_control_message(
                                        frame, network_fake_own_station, sources[source], NETWORK_FAKE_ADDRESS,
                                        KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST, NETWORK_FAKE_ECHO_DATA));
        (void) kernel_network_interface_poll(&network_fake_interface, 1u + source);
    }
    network_fake_receive(frame, network_fake_write_control_message(
                                    frame, network_fake_broadcast, NETWORK_FAKE_PEER, NETWORK_FAKE_ADDRESS,
                                    KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST, NETWORK_FAKE_ECHO_DATA));
    (void) kernel_network_interface_poll(&network_fake_interface, 10u);
    kernel_test_check(test, network_fake_card.sent_count == 0u, "none is answered");
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_SOURCE_REFUSED) == source_count,
                      "0.0.0.0, 127.0.0.1, 224.0.0.1, 255.255.255.255, 10.0.0.255 and 10.0.0.2 are refused sources");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_LINK_BROADCAST) == 1u,
                      "and a request to 10.0.0.2 in a broadcast frame is dropped");
    network_fake_check_counted(test, source_count + 1u);
}

/**
 * @brief An echo in a frame padded past its IPv4 total length is answered with its own bytes, the padding left out.
 */
KERNEL_TEST(an_echo_in_a_padded_frame_is_answered_without_its_padding)
{
    uint8_t frame[64];
    KernelEthernetFrame_t reply_frame;
    KernelInternetProtocolPacket_t reply;

    network_fake_set_up();
    memset(frame, 0x5A, sizeof(frame));
    (void) network_fake_write_to_interface(frame, KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST, 0u);
    network_fake_receive(frame, KERNEL_ETHERNET_FRAME_LEAST_BYTES);
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED) == 1u,
                      "an echo of 8 bytes in a frame of 60, 18 bytes of 5Ah after it, is answered");
    kernel_test_check(test,
                      network_fake_card.sent_count == 1u && network_fake_card.sent_lengths[0] == 42u &&
                          kernel_ethernet_parse(network_fake_card.sent[0], 42u, &reply_frame) &&
                          kernel_internet_protocol_parse(reply_frame.payload, reply_frame.payload_bytes, &reply) ==
                              KERNEL_INTERNET_PROTOCOL_TAKEN &&
                          reply.payload_bytes == KERNEL_INTERNET_CONTROL_MESSAGE_HEADER_BYTES,
                      "with a reply of 42 bytes carrying its 8");
    network_fake_check_counted(test, 1u);
}

/**
 * @brief The longest echo whose reply fits a frame is answered; one byte more is counted as too long.
 */
KERNEL_TEST(an_echo_whose_reply_fits_no_frame_is_counted_too_long)
{
    static uint8_t frame[NETWORK_FAKE_BUFFER_BYTES];
    const uint32_t longest_data =
        NETWORK_FAKE_FRAME_MOST - NETWORK_FAKE_MESSAGE - KERNEL_INTERNET_CONTROL_MESSAGE_HEADER_BYTES;

    network_fake_set_up();
    network_fake_receive(
        frame, network_fake_write_to_interface(frame, KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST, longest_data));
    network_fake_receive(
        frame, network_fake_write_to_interface(frame, KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST, longest_data + 1u));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED) == 1u &&
                          network_fake_card.sent_lengths[0] == NETWORK_FAKE_FRAME_MOST,
                      "an echo in a frame of 1514 bytes is answered with as many");
    kernel_test_check(
        test, network_fake_fate(KERNEL_NETWORK_INTERFACE_ECHO_TOO_LONG) == 1u && network_fake_card.sent_count == 1u,
        "one in a frame of 1515 is counted as too long, unanswered");
    network_fake_check_counted(test, 2u);
}

/**
 * @brief Echo replies are counted only sound; other ICMP messages, and echo requests of another code, are ignored.
 */
KERNEL_TEST(control_messages_are_counted_by_what_they_are)
{
    uint8_t frame[64] = {0u};
    uint32_t length = 0u;

    network_fake_set_up();
    network_fake_receive(frame, network_fake_write_to_interface(frame, KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY,
                                                                NETWORK_FAKE_ECHO_DATA));
    length = network_fake_write_to_interface(frame, KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY, NETWORK_FAKE_ECHO_DATA);
    frame[length - 1u] ^= 0x01u;
    network_fake_receive(frame, length);
    (void) network_fake_write_to_interface(frame, KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY, 0u);
    kernel_internet_protocol_write_header(frame + KERNEL_ETHERNET_HEADER_BYTES, NETWORK_FAKE_PEER, NETWORK_FAKE_ADDRESS,
                                          KERNEL_INTERNET_PROTOCOL_CONTROL_MESSAGE, 4u, 3u);
    network_fake_seal_control_message(frame + NETWORK_FAKE_MESSAGE, 4u);
    network_fake_receive(frame, NETWORK_FAKE_MESSAGE + 4u);
    network_fake_receive(frame, network_fake_write_to_interface(frame, 13u, NETWORK_FAKE_ECHO_DATA));
    length =
        network_fake_write_to_interface(frame, KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST, NETWORK_FAKE_ECHO_DATA);
    frame[NETWORK_FAKE_MESSAGE + 1u] = 1u;
    network_fake_seal_control_message(frame + NETWORK_FAKE_MESSAGE, length - NETWORK_FAKE_MESSAGE);
    network_fake_receive(frame, length);
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_ECHO_REPLY_RECEIVED) == 1u,
                      "a sound echo reply is counted as one");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_CONTROL_MESSAGE_MALFORMED) == 2u,
                      "a reply with a flipped bit, and an echo reply of 4 bytes whose sum holds, are malformed");
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_CONTROL_MESSAGE_IGNORED) == 2u &&
                          network_fake_card.sent_count == 0u,
                      "a timestamp request and an echo request of code 1 are ignored, unanswered");
    network_fake_check_counted(test, 5u);
}

/**
 * @brief When the transmit ring stays full, the batch kicks once to take slots back, then drops its answers
 *        without kicking again, each counted, and so until the next poll.
 */
KERNEL_TEST(a_full_transmit_ring_costs_one_kick_a_batch)
{
    uint8_t frame[64] = {0u};
    uint32_t slot = 0u;

    network_fake_set_up();
    (void) kernel_spsc_ring_reserve(&network_fake_card.transmit, NETWORK_FAKE_SLOTS, &slot);
    kernel_spsc_ring_publish(&network_fake_card.transmit, NETWORK_FAKE_SLOTS);
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED) == 1u &&
                          network_fake_card.kicks == 2u,
                      "a ring left full by the last batch is taken back by one kick, and the request answered");
    network_fake_card.kick_holds = true;
    (void) kernel_spsc_ring_reserve(&network_fake_card.transmit, NETWORK_FAKE_SLOTS, &slot);
    kernel_spsc_ring_publish(&network_fake_card.transmit, NETWORK_FAKE_SLOTS);
    for (uint32_t request = 0u; request < 3u; ++request)
        network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 2u);
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_ANSWER_RING_FULL) == 3u,
                      "with a card that takes nothing back, three requests find no slot, each counted");
    kernel_test_check(test, network_fake_card.kicks == 4u, "for one kick to take slots back, and the poll's own");
    kernel_test_check(test,
                      kernel_network_interface_request_address(&network_fake_interface, NETWORK_FAKE_PEER, 2u) ==
                              KERNEL_NETWORK_INTERFACE_REQUEST_RING_FULL &&
                          network_fake_interface.counters.address_resolution_requests_unsent == 1u &&
                          network_fake_card.kicks == 4u,
                      "a request for an address then finds no slot, unsent and counted, without a kick");
    network_fake_check_counted(test, 4u);
}

/**
 * @brief An answer the card's describe refuses is counted as refused, its slot left unpublished.
 */
KERNEL_TEST(an_answer_the_card_refuses_is_counted_and_not_published)
{
    uint8_t frame[64] = {0u};

    network_fake_set_up();
    network_fake_device.frame_most_bytes = 40u;
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_ANSWER_REFUSED) == 1u &&
                          network_fake_card.sent_count == 0u &&
                          kernel_spsc_ring_size(&network_fake_card.transmit) == 0u,
                      "a reply of 42 bytes to a card that came to take 40 is refused and never published");
    network_fake_check_counted(test, 1u);
}

/**
 * @brief Frames longer than a buffer, for another station, from a group, of an unknown EtherType or shorter than a
 *        header are dropped, each counted, and every slot read is given back.
 */
KERNEL_TEST(frames_the_interface_does_not_take_are_counted_by_their_cause)
{
    static const uint8_t elsewhere[6] = {0x02u, 0u, 0u, 0u, 0u, 0x55u};
    static const uint8_t group[6] = {0x01u, 0x00u, 0x5Eu, 0u, 0u, 0x01u};
    uint8_t frame[64] = {0u};
    uint32_t slot = 0u;

    network_fake_set_up();
    (void) network_fake_write_echo(frame, NETWORK_FAKE_ADDRESS);
    memcpy(frame, elsewhere, 6u);
    network_fake_receive(frame, 46u);
    memcpy(frame, network_fake_own_station, 6u);
    memcpy(frame + 6, group, 6u);
    network_fake_receive(frame, 46u);
    memcpy(frame + 6, network_fake_peer_station, 6u);
    frame[12] = 0x86u;
    frame[13] = 0xDDu;
    network_fake_receive(frame, 46u);
    network_fake_receive(frame, 13u);
    (void) kernel_spsc_ring_reserve(&network_fake_card.receive, 1u, &slot);
    network_fake_card.receive_lengths[slot] = NETWORK_FAKE_BUFFER_BYTES + 1u;
    kernel_spsc_ring_publish(&network_fake_card.receive, 1u);
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_FRAME_FOR_OTHERS) == 1u &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_FRAME_FROM_GROUP) == 1u &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_FRAME_OF_UNKNOWN_TYPE) == 1u &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_FRAME_TRUNCATED) == 1u &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_FRAME_OVERSIZED) == 1u,
                      "for another station, from a group, EtherType 86DDh, 13 bytes, and 1537 bytes in 1536");
    kernel_test_check(test, network_fake_card.sent_count == 0u, "and none is answered");
    kernel_test_check(test, kernel_spsc_ring_size(&network_fake_card.receive) == 0u,
                      "and the poll gives every slot it read back to the card");
    network_fake_check_counted(test, 5u);
}

/**
 * @brief An interface refuses to set up on no card, on a card without a queue or an operation, and on frame bounds
 *        out of order, each refusal by its cause.
 */
KERNEL_TEST(an_interface_refuses_a_card_by_its_cause)
{
    KernelNetworkInterface_t interface;
    KernelNetworkDeviceOperations_t without_kick = network_fake_operations;

    without_kick.kick = NULL;
    network_fake_set_up_card(KERNEL_ETHERNET_HEADER_BYTES);
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, NULL, NETWORK_FAKE_ADDRESS, NETWORK_FAKE_NETMASK,
                                                          0u, 1u) == KERNEL_NETWORK_INTERFACE_NO_DEVICE,
                      "no card");
    network_fake_device.queue_count = 0u;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, NETWORK_FAKE_ADDRESS,
                                                          NETWORK_FAKE_NETMASK, 0u,
                                                          1u) == KERNEL_NETWORK_INTERFACE_NO_QUEUE,
                      "a card without a queue pair");
    network_fake_device.queue_count = 1u;
    network_fake_device.operations = &without_kick;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, NETWORK_FAKE_ADDRESS,
                                                          NETWORK_FAKE_NETMASK, 0u,
                                                          1u) == KERNEL_NETWORK_INTERFACE_NO_OPERATIONS,
                      "a card without a kick");
    network_fake_device.operations = &network_fake_operations;
    network_fake_device.frame_most_bytes = NETWORK_FAKE_BUFFER_BYTES + 1u;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, NETWORK_FAKE_ADDRESS,
                                                          NETWORK_FAKE_NETMASK, 0u,
                                                          1u) == KERNEL_NETWORK_INTERFACE_BAD_FRAME_BOUNDS,
                      "a longest frame past its buffers");
    network_fake_device.frame_most_bytes = KERNEL_ETHERNET_FRAME_LEAST_BYTES - 1u;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, NETWORK_FAKE_ADDRESS,
                                                          NETWORK_FAKE_NETMASK, 0u,
                                                          1u) == KERNEL_NETWORK_INTERFACE_BAD_FRAME_BOUNDS,
                      "a longest frame under Ethernet's shortest");
    network_fake_device.frame_most_bytes = NETWORK_FAKE_FRAME_MOST;
    network_fake_device.frame_least_bytes = NETWORK_FAKE_FRAME_MOST + 1u;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, NETWORK_FAKE_ADDRESS,
                                                          NETWORK_FAKE_NETMASK, 0u,
                                                          1u) == KERNEL_NETWORK_INTERFACE_BAD_FRAME_BOUNDS,
                      "a shortest frame past its longest");
    network_fake_device.frame_least_bytes = KERNEL_ETHERNET_HEADER_BYTES;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, NETWORK_FAKE_ADDRESS,
                                                          NETWORK_FAKE_NETMASK, 0u,
                                                          1u) == KERNEL_NETWORK_INTERFACE_READY,
                      "and the same card, its bounds in order, is taken");
}

/**
 * @brief A packet goes to its own station on the subnet, to the gateway's off it, to every station for a broadcast,
 *        and to RFC 1112's station for a multicast group; no packet goes to loopback or this network.
 */
KERNEL_TEST(a_packet_goes_to_the_station_its_destination_names)
{
    static const uint8_t gateway_station[6] = {0x02u, 0u, 0u, 0u, 0u, 0xFEu};
    static const uint8_t neighbour_station[6] = {0x02u, 0u, 0u, 0u, 0u, 0x07u};
    static const uint8_t mdns[6] = {0x01u, 0x00u, 0x5Eu, 0x00u, 0x00u, 0xFBu};
    static const uint8_t ssdp[6] = {0x01u, 0x00u, 0x5Eu, 0x7Fu, 0xFFu, 0xFAu};
    uint8_t found[6] = {0u};

    network_fake_set_up_through(KERNEL_ETHERNET_HEADER_BYTES, NETWORK_FAKE_GATEWAY);
    (void) kernel_address_resolution_cache_add(&network_fake_interface.cache, NETWORK_FAKE_GATEWAY, gateway_station,
                                               0u);
    (void) kernel_address_resolution_cache_add(&network_fake_interface.cache, 0x0A000007u, neighbour_station, 0u);
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_fake_interface, 0x0A000007u, 1u, found) &&
                          memcmp(found, neighbour_station, 6u) == 0,
                      "10.0.0.7 goes to its own station");
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_fake_interface, 0x08080808u, 1u, found) &&
                          memcmp(found, gateway_station, 6u) == 0,
                      "8.8.8.8 goes to the gateway's");
    kernel_test_check(test,
                      !kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_OTHER, 1u, found),
                      "10.0.0.9, never learned, is not found");
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_fake_interface, 0xFFFFFFFFu, 1u, found) &&
                          memcmp(found, network_fake_broadcast, 6u) == 0 &&
                          kernel_network_interface_find_station(&network_fake_interface, 0x0A0000FFu, 1u, found) &&
                          memcmp(found, network_fake_broadcast, 6u) == 0,
                      "255.255.255.255 and 10.0.0.255 go to every station");
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_fake_interface, 0xE00000FBu, 1u, found) &&
                          memcmp(found, mdns, 6u) == 0 &&
                          kernel_network_interface_find_station(&network_fake_interface, 0xEFFFFFFAu, 1u, found) &&
                          memcmp(found, ssdp, 6u) == 0,
                      "224.0.0.251 to 01-00-5E-00-00-FB and 239.255.255.250 to 01-00-5E-7F-FF-FA (RFC 1112, 6.4)");
    kernel_test_check(test,
                      !kernel_network_interface_find_station(&network_fake_interface, 0x7F000001u, 1u, found) &&
                          !kernel_network_interface_find_station(&network_fake_interface, 0u, 1u, found) &&
                          !kernel_network_interface_find_station(&network_fake_interface, 0xF0000001u, 1u, found),
                      "127.0.0.1, 0.0.0.0 and 240.0.0.1 go nowhere");
    kernel_test_check(test,
                      kernel_network_interface_request_address(&network_fake_interface, 0xFFFFFFFFu, 1u) ==
                              KERNEL_NETWORK_INTERFACE_REQUEST_NEEDLESS &&
                          kernel_network_interface_request_address(&network_fake_interface, 0xE00000FBu, 1u) ==
                              KERNEL_NETWORK_INTERFACE_REQUEST_NEEDLESS &&
                          kernel_network_interface_request_address(&network_fake_interface, 0x7F000001u, 1u) ==
                              KERNEL_NETWORK_INTERFACE_REQUEST_UNREACHABLE &&
                          network_fake_interface.counters.address_resolution_requests_sent == 0u,
                      "and none of these is asked for by ARP");
}

/**
 * @brief The poll frees entries a lifetime old, so that one nobody looks at stays gone when the time wraps.
 */
KERNEL_TEST(the_poll_forgets_expired_stations_before_the_time_wraps)
{
    uint8_t frame[64] = {0u};
    uint8_t station[6] = {0u};

    network_fake_set_up();
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 2u, station),
                      "the peer is learned at 1");
    (void) kernel_network_interface_poll(&network_fake_interface, 1u + NETWORK_FAKE_LIFETIME);
    kernel_test_check(test,
                      !kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 1u, station),
                      "a poll a lifetime later frees it: 2^32 later, at 1 again, it is still gone");
    network_fake_check_counted(test, 1u);
}
