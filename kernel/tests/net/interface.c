#include <kernel/net/arp.h>
#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/udp.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <string.h>

#define NETWORK_FAKE_SLOTS            8u          /**< Slots of each ring of the made-up card. */
#define NETWORK_FAKE_BUFFER_BYTES     1536u       /**< Bytes of each of its buffers. */
#define NETWORK_FAKE_FRAME_MOST       1514u       /**< Its longest frame, its CRC left to it. */
#define NETWORK_FAKE_ADDRESS          0x0A000002u /**< 10.0.0.2, the interface's address. */
#define NETWORK_FAKE_NETMASK          0xFFFFFF00u /**< A /24. */
#define NETWORK_FAKE_PEER             0x0A000001u /**< 10.0.0.1, the peer's. */
#define NETWORK_FAKE_OTHER            0x0A000009u /**< 10.0.0.9, an address neither has. */
#define NETWORK_FAKE_GATEWAY          0x0A0000FEu /**< 10.0.0.254, the gateway of the tests that set one. */
#define NETWORK_FAKE_LIFETIME         100u        /**< The cache's lifetime, in the tests' time. */
#define NETWORK_FAKE_REQUEST_INTERVAL 10u         /**< Least time between two ARP requests a send makes. */
#define NETWORK_FAKE_ECHO_DATA        4u          /**< Bytes of data in an echo request, after its 8 of header. */
#define NETWORK_FAKE_MESSAGE          34u         /**< Where an ICMP message starts in a frame: 14 + 20 bytes. */
#define NETWORK_FAKE_GAME_PORT        9000u       /**< The port the datagram tests bind lent. */
#define NETWORK_FAKE_NAME_PORT        53u         /**< The port the datagram tests bind copied. */
#define NETWORK_FAKE_PEER_PORT        7777u       /**< The peer's port. */
#define NETWORK_FAKE_PORT_SLOTS       4u          /**< Slots of the copied port's ring. */
#define NETWORK_FAKE_PORT_BYTES       16u         /**< Bytes of each of its buffers. */
#define NETWORK_FAKE_BIG_SLOTS        128u        /**< Slots of the card with more slots than lent places. */
#define NETWORK_FAKE_BIG_BYTES        64u         /**< Bytes of each of its buffers. */

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
 * @brief The made-up interface's configuration: 10.0.0.2/24 through @p gateway.
 */
static KernelNetworkInterfaceConfig_t network_fake_config(uint32_t gateway)
{
    return (KernelNetworkInterfaceConfig_t){.address = NETWORK_FAKE_ADDRESS,
                                            .netmask = NETWORK_FAKE_NETMASK,
                                            .gateway = gateway,
                                            .cache_lifetime = NETWORK_FAKE_LIFETIME,
                                            .request_interval = NETWORK_FAKE_REQUEST_INTERVAL};
}

/**
 * @brief Sets up the made-up card, its shortest frame @p frame_least_bytes, and the interface 10.0.0.2/24 on it,
 *        through @p gateway.
 */
static void network_fake_set_up_through(uint32_t frame_least_bytes, uint32_t gateway)
{
    network_fake_set_up_card(frame_least_bytes);
    const KernelNetworkInterfaceConfig_t config = network_fake_config(gateway);

    (void) kernel_network_interface_initialize(&network_fake_interface, &network_fake_device, &config);
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

/**
 * @brief How a made-up datagram's checksum is written.
 */
typedef enum NetworkFakeChecksum {
    NETWORK_FAKE_CHECKSUM_SOUND = 0, /**< As the sender computes it. */
    NETWORK_FAKE_CHECKSUM_NONE,      /**< Zero, none computed. */
    NETWORK_FAKE_CHECKSUM_WRONG,     /**< The sound one, a bit flipped. */
} NetworkFakeChecksum_t;

/**
 * @brief Writes a datagram of @p payload_bytes, each byte its index plus @p seed, from the peer at @p source_port to
 *        the interface's @p destination_port, in a frame to its station.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_datagram(uint8_t *frame, uint16_t destination_port, uint32_t payload_bytes,
                                            uint8_t seed, NetworkFakeChecksum_t checksum)
{
    uint8_t *const datagram = frame + NETWORK_FAKE_MESSAGE;
    uint8_t *const payload = datagram + KERNEL_USER_DATAGRAM_HEADER_BYTES;
    const uint32_t datagram_bytes = KERNEL_USER_DATAGRAM_HEADER_BYTES + payload_bytes;
    uint16_t sum = 0u;

    kernel_ethernet_write_header(frame, network_fake_own_station, network_fake_peer_station,
                                 KERNEL_ETHERNET_TYPE_INTERNET_PROTOCOL_VERSION_4);
    kernel_internet_protocol_write_header(frame + KERNEL_ETHERNET_HEADER_BYTES, NETWORK_FAKE_PEER, NETWORK_FAKE_ADDRESS,
                                          KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM, datagram_bytes, 5u);
    for (uint32_t byte = 0u; byte < payload_bytes; ++byte)
        payload[byte] = (uint8_t) (byte + seed);
    sum = kernel_user_datagram_checksum(
        kernel_user_datagram_pseudo_header_sum(NETWORK_FAKE_PEER, NETWORK_FAKE_ADDRESS, datagram_bytes),
        NETWORK_FAKE_PEER_PORT, destination_port, payload_bytes,
        kernel_internet_checksum_add(0u, payload, payload_bytes));
    if (checksum == NETWORK_FAKE_CHECKSUM_NONE)
        sum = 0u;
    if (checksum == NETWORK_FAKE_CHECKSUM_WRONG)
        sum ^= 0x0100u;
    kernel_user_datagram_write_header(datagram, NETWORK_FAKE_PEER_PORT, destination_port, payload_bytes, sum);
    return NETWORK_FAKE_MESSAGE + datagram_bytes;
}

/**
 * @brief Writes the datagram of network_fake_write_datagram behind an IPv4 header of 24 bytes, its 4 bytes of options
 *        four no-operations: a datagram only the general path takes.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_datagram_with_options(uint8_t *frame, uint32_t payload_bytes, uint8_t seed)
{
    static uint8_t plain[128];
    uint8_t *const header = frame + KERNEL_ETHERNET_HEADER_BYTES;
    const uint32_t length =
        network_fake_write_datagram(plain, NETWORK_FAKE_GAME_PORT, payload_bytes, seed, NETWORK_FAKE_CHECKSUM_SOUND);

    memcpy(frame, plain, KERNEL_ETHERNET_HEADER_BYTES + 20u);
    memset(header + 20u, 0x01, 4u);
    memcpy(header + 24u, plain + KERNEL_ETHERNET_HEADER_BYTES + 20u, length - KERNEL_ETHERNET_HEADER_BYTES - 20u);
    header[0] = 0x46u;
    kernel_network_write_16(header + 2u, (uint16_t) (length - KERNEL_ETHERNET_HEADER_BYTES + 4u));
    kernel_network_write_16(header + 10u, 0u);
    kernel_network_write_16(header + 10u,
                            kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, header, 24u)));
    return length + 4u;
}

/**
 * @brief Binds @p port lent on the made-up interface, taking datagrams without checksum when @p without_checksum.
 */
static KernelNetworkInterfaceBind_t network_fake_bind_lent(uint16_t port, bool without_checksum)
{
    const KernelNetworkPortBinding_t binding = {.port = port,
                                                .delivery = KERNEL_NETWORK_DELIVERY_LENT,
                                                .without_checksum = without_checksum};

    return kernel_network_interface_bind(&network_fake_interface, &binding);
}

static KernelSpscRing_t network_fake_port_ring;
static KernelNetworkDatagram_t network_fake_port_datagrams[NETWORK_FAKE_PORT_SLOTS];
static uint8_t network_fake_port_buffers[NETWORK_FAKE_PORT_SLOTS][NETWORK_FAKE_PORT_BYTES];

/**
 * @brief Binds the name port copied, into a ring of 4 buffers of 16 bytes.
 */
static KernelNetworkInterfaceBind_t network_fake_bind_copied(void)
{
    const KernelNetworkPortBinding_t binding = {
        .port = NETWORK_FAKE_NAME_PORT,
        .delivery = KERNEL_NETWORK_DELIVERY_COPIED,
        .copies = {.ring = &network_fake_port_ring,
                   .datagrams = network_fake_port_datagrams,
                   .buffers = &network_fake_port_buffers[0][0],
                   .buffer_bytes = NETWORK_FAKE_PORT_BYTES}
    };

    (void) kernel_spsc_ring_initialize(&network_fake_port_ring, NETWORK_FAKE_PORT_SLOTS);
    return kernel_network_interface_bind(&network_fake_interface, &binding);
}

/**
 * @brief Teaches the interface the peer's station, by the peer's ARP request for the interface, at time 1.
 */
static void network_fake_learn_peer(void)
{
    uint8_t frame[64] = {0u};

    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    network_fake_card.sent_count = 0u;
}

/**
 * @brief Reads the datagram the kicks captured at @p index.
 *
 * @return Its UDP status, or KERNEL_USER_DATAGRAM_STATUSES when it is no UDP datagram in a sound IPv4 packet.
 */
static KernelUserDatagramStatus_t network_fake_read_sent_datagram(uint32_t index,
                                                                  KernelInternetProtocolPacket_t *out_packet,
                                                                  KernelUserDatagram_t *out_datagram)
{
    KernelEthernetFrame_t frame;

    if (index >= network_fake_card.sent_count ||
        !kernel_ethernet_parse(network_fake_card.sent[index], network_fake_card.sent_lengths[index], &frame) ||
        kernel_internet_protocol_parse(frame.payload, frame.payload_bytes, out_packet) !=
            KERNEL_INTERNET_PROTOCOL_TAKEN ||
        out_packet->protocol != KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM)
        return KERNEL_USER_DATAGRAM_STATUSES;
    return kernel_user_datagram_parse(out_packet, out_datagram);
}

/**
 * @brief Reserves a datagram, writes @p payload_bytes of @p seed into it, and sends it from the game port to
 *        @p destination with @p checksum, the payload's sum the caller's own.
 */
static KernelNetworkInterfaceSend_t network_fake_send(uint32_t destination, uint32_t payload_bytes, uint8_t seed,
                                                      KernelNetworkChecksum_t checksum, uint32_t now)
{
    uint32_t slot = 0u;
    uint32_t room = 0u;
    uint8_t *const payload = kernel_network_interface_reserve_datagram(&network_fake_interface, &slot, &room);

    if (payload == NULL || payload_bytes > room + 1u)
        return KERNEL_NETWORK_INTERFACE_SEND_RESULTS;
    memset(payload, seed, payload_bytes < room ? payload_bytes : room);

    const KernelNetworkDatagramOut_t datagram = {
        .destination_address = destination,
        .destination_port = NETWORK_FAKE_PEER_PORT,
        .source_port = NETWORK_FAKE_GAME_PORT,
        .payload_bytes = payload_bytes,
        .payload_sum = kernel_internet_checksum_add(0u, payload, payload_bytes < room ? payload_bytes : room),
        .checksum = checksum};

    return kernel_network_interface_send_datagram(&network_fake_interface, slot, &datagram, now);
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
    const KernelNetworkInterfaceConfig_t config = network_fake_config(0u);

    without_kick.kick = NULL;
    network_fake_set_up_card(KERNEL_ETHERNET_HEADER_BYTES);
    kernel_test_check(
        test, kernel_network_interface_initialize(&interface, NULL, &config) == KERNEL_NETWORK_INTERFACE_NO_DEVICE,
        "no card");
    network_fake_device.queue_count = 0u;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, &config) ==
                          KERNEL_NETWORK_INTERFACE_NO_QUEUE,
                      "a card without a queue pair");
    network_fake_device.queue_count = 1u;
    network_fake_device.operations = &without_kick;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, &config) ==
                          KERNEL_NETWORK_INTERFACE_NO_OPERATIONS,
                      "a card without a kick");
    network_fake_device.operations = &network_fake_operations;
    network_fake_device.frame_most_bytes = NETWORK_FAKE_BUFFER_BYTES + 1u;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, &config) ==
                          KERNEL_NETWORK_INTERFACE_BAD_FRAME_BOUNDS,
                      "a longest frame past its buffers");
    network_fake_device.frame_most_bytes = KERNEL_ETHERNET_FRAME_LEAST_BYTES - 1u;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, &config) ==
                          KERNEL_NETWORK_INTERFACE_BAD_FRAME_BOUNDS,
                      "a longest frame under Ethernet's shortest");
    network_fake_device.frame_most_bytes = NETWORK_FAKE_FRAME_MOST;
    network_fake_device.frame_least_bytes = NETWORK_FAKE_FRAME_MOST + 1u;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, &config) ==
                          KERNEL_NETWORK_INTERFACE_BAD_FRAME_BOUNDS,
                      "a shortest frame past its longest");
    network_fake_device.frame_least_bytes = KERNEL_ETHERNET_HEADER_BYTES;
    kernel_test_check(test,
                      kernel_network_interface_initialize(&interface, &network_fake_device, &config) ==
                          KERNEL_NETWORK_INTERFACE_READY,
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
 * @brief The polls free entries a lifetime old, one entry a poll, so that one nobody looks at stays gone when the
 *        time wraps.
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
    for (uint32_t poll = 0u; poll < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++poll)
        (void) kernel_network_interface_poll(&network_fake_interface, 1u + NETWORK_FAKE_LIFETIME + poll);
    kernel_test_check(test,
                      !kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 1u, station),
                      "16 polls a lifetime later, one entry a poll, free it: 2^32 later, at 1 again, it is still gone");
    network_fake_check_counted(test, 1u);
}

/**
 * @brief A datagram to a lent port is lent where the card wrote it; its frame, and the frames after it, go back to
 *        the card only as the lent datagrams before them are released, in their order of arrival.
 */
KERNEL_TEST(a_datagram_to_a_lent_port_is_lent_where_it_arrived_and_released_in_order)
{
    uint8_t frame[64] = {0u};
    const KernelNetworkDatagram_t *lent = NULL;

    network_fake_set_up();
    kernel_test_check(test,
                      network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false) == KERNEL_NETWORK_INTERFACE_BIND_BOUND,
                      "port 9000 binds lent");
    network_fake_receive(frame, network_fake_write_datagram_with_options(frame, 6u, 10u));
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    network_fake_receive(
        frame, network_fake_write_datagram(frame, NETWORK_FAKE_GAME_PORT, 3u, 20u, NETWORK_FAKE_CHECKSUM_SOUND));
    kernel_test_check(test, kernel_network_interface_poll(&network_fake_interface, 1u) == 3u, "the poll reads three");
    kernel_test_check(test,
                      kernel_network_interface_lent_count(&network_fake_interface) == 2u &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT) == 2u &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED) == 1u,
                      "lends both datagrams and answers the ARP request between them");
    lent = kernel_network_interface_lent_at(&network_fake_interface, 0u);
    kernel_test_check(test,
                      lent->payload == network_fake_card.receive_buffers[0] + NETWORK_FAKE_MESSAGE + 4 + 8 &&
                          lent->payload_bytes == 6u && lent->payload[0] == 10u && lent->payload[5] == 15u &&
                          lent->source_address == NETWORK_FAKE_PEER && lent->source_port == NETWORK_FAKE_PEER_PORT &&
                          lent->destination_port == NETWORK_FAKE_GAME_PORT,
                      "the first, behind 4 bytes of options, is lent where the card wrote it, from 10.0.0.1:7777 to "
                      "port 9000");
    kernel_test_check(test, network_fake_interface.counters.datagrams_common == 1u,
                      "by the general path, the second by the common one");
    kernel_test_check(test, kernel_network_interface_lent_at(&network_fake_interface, 1u)->payload[0] == 20u,
                      "the second after it");
    kernel_test_check(test, kernel_spsc_ring_size(&network_fake_card.receive) == 3u,
                      "and the card gets none of the three slots back yet");
    kernel_test_check(test,
                      kernel_network_interface_poll(&network_fake_interface, 2u) == 0u &&
                          network_fake_interface.counters.frames_received == 3u &&
                          kernel_network_interface_lent_count(&network_fake_interface) == 2u,
                      "a poll in between takes none of them again");
    kernel_test_check(test,
                      kernel_network_interface_release_lent(&network_fake_interface, 1u) == 1u &&
                          kernel_spsc_ring_size(&network_fake_card.receive) == 1u,
                      "releasing the first gives back its slot and the ARP request's, the second still held");
    kernel_test_check(test,
                      kernel_network_interface_release_lent(&network_fake_interface, 5u) == 1u &&
                          kernel_spsc_ring_size(&network_fake_card.receive) == 0u,
                      "and releasing five releases the one left, every slot back");
    network_fake_check_counted(test, 3u);
}

/**
 * @brief A datagram to a copied port is copied into the port's ring, its frame given back at once; a datagram past
 *        the port's buffers, or past its full ring, is dropped and counted.
 */
KERNEL_TEST(a_datagram_to_a_copied_port_is_copied_and_its_frame_given_back)
{
    static uint8_t frame[128];
    uint32_t first = 0u;

    network_fake_set_up();
    kernel_test_check(test, network_fake_bind_copied() == KERNEL_NETWORK_INTERFACE_BIND_BOUND, "port 53 binds copied");
    for (uint32_t datagram = 0u; datagram < NETWORK_FAKE_PORT_SLOTS + 1u; ++datagram)
        network_fake_receive(frame, network_fake_write_datagram(frame, NETWORK_FAKE_NAME_PORT, 8u, (uint8_t) datagram,
                                                                NETWORK_FAKE_CHECKSUM_SOUND));
    network_fake_receive(frame, network_fake_write_datagram(frame, NETWORK_FAKE_NAME_PORT, NETWORK_FAKE_PORT_BYTES + 1u,
                                                            0u, NETWORK_FAKE_CHECKSUM_SOUND));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_DATAGRAM_COPIED) == NETWORK_FAKE_PORT_SLOTS &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_DATAGRAM_PORT_FULL) == 1u &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_DATAGRAM_TOO_LONG_FOR_PORT) == 1u,
                      "four are copied, the fifth finds the ring full, and 17 bytes do not fit a buffer of 16");
    kernel_test_check(
        test,
        kernel_spsc_ring_peek(&network_fake_port_ring, NETWORK_FAKE_PORT_SLOTS, &first) == NETWORK_FAKE_PORT_SLOTS &&
            network_fake_port_datagrams[first].payload == network_fake_port_buffers[first] &&
            network_fake_port_datagrams[first].payload_bytes == 8u && network_fake_port_buffers[first][7] == 7u &&
            network_fake_port_datagrams[first].source_port == NETWORK_FAKE_PEER_PORT,
        "the first copy lies in the port's own buffer, its 8 bytes there");
    kernel_test_check(test,
                      kernel_spsc_ring_size(&network_fake_card.receive) == 0u &&
                          kernel_network_interface_lent_count(&network_fake_interface) == 0u,
                      "and every frame went back to the card");
    network_fake_check_counted(test, NETWORK_FAKE_PORT_SLOTS + 2u);
}

/**
 * @brief Datagrams the interface does not deliver are counted by their cause: to a port nobody bound or to port 0,
 *        without a checksum to a port that wants one, with a checksum that fails, or with a length past them.
 */
KERNEL_TEST(datagrams_the_interface_does_not_deliver_are_counted_by_their_cause)
{
    uint8_t frame[64] = {0u};
    uint32_t length = 0u;

    network_fake_set_up();
    (void) network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false);
    network_fake_receive(frame, network_fake_write_datagram(frame, 9001u, 4u, 0u, NETWORK_FAKE_CHECKSUM_SOUND));
    network_fake_receive(frame, network_fake_write_datagram(frame, 0u, 4u, 0u, NETWORK_FAKE_CHECKSUM_SOUND));
    network_fake_receive(
        frame, network_fake_write_datagram(frame, NETWORK_FAKE_GAME_PORT, 4u, 0u, NETWORK_FAKE_CHECKSUM_NONE));
    network_fake_receive(
        frame, network_fake_write_datagram(frame, NETWORK_FAKE_GAME_PORT, 4u, 0u, NETWORK_FAKE_CHECKSUM_WRONG));
    length = network_fake_write_datagram(frame, NETWORK_FAKE_GAME_PORT, 4u, 0u, NETWORK_FAKE_CHECKSUM_SOUND);
    kernel_network_write_16(frame + NETWORK_FAKE_MESSAGE + 4u, 13u);
    network_fake_receive(frame, length);
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_PORT_UNBOUND) == 2u,
                      "port 9001, unbound, and port 0 deliver nothing");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_CHECKSUM_ABSENT) == 1u,
                      "a zero checksum to port 9000, which wants one, is refused");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_BAD_CHECKSUM) == 1u,
                      "a checksum with a flipped bit is refused");
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_MALFORMED) == 1u,
                      "a length of 13 in a datagram of 12 is refused");
    kernel_test_check(test,
                      kernel_network_interface_lent_count(&network_fake_interface) == 0u &&
                          kernel_spsc_ring_size(&network_fake_card.receive) == 0u,
                      "nothing is lent, and every frame went back");
    network_fake_check_counted(test, 5u);
}

/**
 * @brief How a datagram to port 9000 is spoiled, each way failing one check of the common path.
 */
typedef enum NetworkFakeSpoil {
    NETWORK_FAKE_SPOIL_STATION_HEAD = 0, /**< To another station, its first 4 bytes not the interface's. */
    NETWORK_FAKE_SPOIL_STATION_TAIL,     /**< To another station, its last 2 bytes not the interface's. */
    NETWORK_FAKE_SPOIL_GROUP_SOURCE,     /**< From a group station. */
    NETWORK_FAKE_SPOIL_ETHER_TYPE,       /**< EtherType 86DDh. */
    NETWORK_FAKE_SPOIL_VERSION,          /**< IP version 6. */
    NETWORK_FAKE_SPOIL_HEADER_LENGTH,    /**< An IHL of 6, its checksum over 20 bytes only. */
    NETWORK_FAKE_SPOIL_DESTINATION,      /**< To 10.0.0.9, its UDP checksum still the one for 10.0.0.2. */
    NETWORK_FAKE_SPOIL_FRAGMENT,         /**< A first fragment. */
    NETWORK_FAKE_SPOIL_PROTOCOL,         /**< Protocol 6, TCP. */
    NETWORK_FAKE_SPOIL_TOTAL,            /**< A total length a byte past the frame. */
    NETWORK_FAKE_SPOIL_LOOPBACK_SOURCE,  /**< From 127.0.0.1, its UDP checksum sound. */
    NETWORK_FAKE_SPOIL_BROADCAST_SOURCE, /**< From 10.0.0.255, its UDP checksum sound. */
    NETWORK_FAKE_SPOIL_SHORT_LENGTH,     /**< A UDP length of 7. */
    NETWORK_FAKE_SPOIL_LONG_LENGTH,      /**< A UDP length a byte past its packet, its UDP checksum sound. */
    NETWORK_FAKE_SPOIL_HEADER_CHECKSUM,  /**< An IPv4 checksum with a flipped bit. */
    NETWORK_FAKE_SPOILS                  /**< How many spoils there are. */
} NetworkFakeSpoil_t;

/**
 * @brief Writes the UDP checksum of the datagram at @p frame again, from the addresses and length it now carries.
 */
static void network_fake_seal_datagram(uint8_t *frame)
{
    uint8_t *const header = frame + KERNEL_ETHERNET_HEADER_BYTES;
    uint8_t *const datagram = header + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES;
    const uint32_t datagram_bytes = kernel_network_read_16(datagram + 4u);
    const uint32_t payload_bytes = datagram_bytes - KERNEL_USER_DATAGRAM_HEADER_BYTES;
    const uint32_t pseudo_header_sum = kernel_user_datagram_pseudo_header_sum(
        kernel_network_read_32(header + 12u), kernel_network_read_32(header + 16u), datagram_bytes);

    kernel_network_write_16(
        datagram + 6u,
        kernel_user_datagram_checksum(
            pseudo_header_sum, kernel_network_read_16(datagram), kernel_network_read_16(datagram + 2u), payload_bytes,
            kernel_internet_checksum_add(0u, datagram + KERNEL_USER_DATAGRAM_HEADER_BYTES, payload_bytes)));
}

/**
 * @brief Writes the IPv4 checksum of the 20 bytes of header at @p frame again.
 */
static void network_fake_seal_header(uint8_t *frame)
{
    uint8_t *const header = frame + KERNEL_ETHERNET_HEADER_BYTES;

    kernel_network_write_16(header + 10u, 0u);
    kernel_network_write_16(header + 10u,
                            kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, header, 20u)));
}

/**
 * @brief Writes a datagram of 6 bytes to port 9000 at @p frame, zeros after it, and spoils it by @p spoil.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_spoiled_datagram(uint8_t *frame, NetworkFakeSpoil_t spoil)
{
    uint8_t *const header = frame + KERNEL_ETHERNET_HEADER_BYTES;
    uint8_t *const datagram = header + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES;
    uint32_t length = 0u;

    memset(frame, 0, 64u);
    length = network_fake_write_datagram(frame, NETWORK_FAKE_GAME_PORT, 6u, 0u, NETWORK_FAKE_CHECKSUM_SOUND);
    if (spoil == NETWORK_FAKE_SPOIL_STATION_HEAD)
        frame[2] = 0x77u;
    if (spoil == NETWORK_FAKE_SPOIL_STATION_TAIL)
        frame[5] = 0x55u;
    if (spoil == NETWORK_FAKE_SPOIL_GROUP_SOURCE)
        frame[6] |= 0x01u;
    if (spoil == NETWORK_FAKE_SPOIL_ETHER_TYPE)
        kernel_network_write_16(frame + 12u, 0x86DDu);
    if (spoil == NETWORK_FAKE_SPOIL_VERSION)
        header[0] = 0x65u;
    if (spoil == NETWORK_FAKE_SPOIL_HEADER_LENGTH)
        header[0] = 0x46u;
    if (spoil == NETWORK_FAKE_SPOIL_DESTINATION)
        kernel_network_write_32(header + 16u, NETWORK_FAKE_OTHER);
    if (spoil == NETWORK_FAKE_SPOIL_FRAGMENT)
        kernel_network_write_16(header + 6u, 0x2000u);
    if (spoil == NETWORK_FAKE_SPOIL_PROTOCOL)
        header[9] = 6u;
    if (spoil == NETWORK_FAKE_SPOIL_TOTAL)
        kernel_network_write_16(header + 2u, (uint16_t) (length - KERNEL_ETHERNET_HEADER_BYTES + 1u));
    if (spoil == NETWORK_FAKE_SPOIL_LOOPBACK_SOURCE)
        kernel_network_write_32(header + 12u, 0x7F000001u);
    if (spoil == NETWORK_FAKE_SPOIL_BROADCAST_SOURCE)
        kernel_network_write_32(header + 12u, 0x0A0000FFu);
    if (spoil == NETWORK_FAKE_SPOIL_SHORT_LENGTH)
        kernel_network_write_16(datagram + 4u, 7u);
    if (spoil == NETWORK_FAKE_SPOIL_LONG_LENGTH)
        kernel_network_write_16(datagram + 4u, (uint16_t) (kernel_network_read_16(datagram + 4u) + 1u));
    if (spoil != NETWORK_FAKE_SPOIL_SHORT_LENGTH && spoil != NETWORK_FAKE_SPOIL_DESTINATION)
        network_fake_seal_datagram(frame);
    network_fake_seal_header(frame);
    if (spoil == NETWORK_FAKE_SPOIL_HEADER_CHECKSUM)
        header[10] ^= 0x01u;
    return length;
}

/**
 * @brief Publishes on the made-up card a receive slot of @p length bytes whose buffer holds a sound datagram to port
 *        9000, whatever @p length says.
 */
static void network_fake_receive_over_datagram(uint32_t length)
{
    uint32_t slot = 0u;

    (void) kernel_spsc_ring_reserve(&network_fake_card.receive, 1u, &slot);
    (void) network_fake_write_datagram(network_fake_card.receive_buffers[slot], NETWORK_FAKE_GAME_PORT, 6u, 0u,
                                       NETWORK_FAKE_CHECKSUM_SOUND);
    network_fake_card.receive_lengths[slot] = (uint16_t) length;
    kernel_spsc_ring_publish(&network_fake_card.receive, 1u);
}

/**
 * @brief A datagram to the lent port that fails one check of the common path is refused by its cause, as any other
 *        frame: none is lent. So is a slot whose length is past its buffer, or short of the datagram the buffer
 *        still holds.
 */
KERNEL_TEST(a_datagram_to_a_lent_port_failing_one_check_is_refused_by_its_cause)
{
    const uint32_t *const fates = network_fake_interface.counters.fates;
    const uint32_t *const refused = network_fake_interface.counters.internet_protocol_refused;
    uint8_t frame[64];

    network_fake_set_up();
    (void) network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false);
    for (uint32_t spoil = 0u; spoil < NETWORK_FAKE_SPOILS; ++spoil)
    {
        network_fake_receive(frame, network_fake_write_spoiled_datagram(frame, (NetworkFakeSpoil_t) spoil));
        (void) kernel_network_interface_poll(&network_fake_interface, 1u + spoil);
    }
    network_fake_receive_over_datagram(NETWORK_FAKE_BUFFER_BYTES + 1u);
    network_fake_receive_over_datagram(13u);
    (void) kernel_network_interface_poll(&network_fake_interface, 20u);
    kernel_test_check(test,
                      fates[KERNEL_NETWORK_INTERFACE_FRAME_FOR_OTHERS] == 2u &&
                          fates[KERNEL_NETWORK_INTERFACE_FRAME_FROM_GROUP] == 1u &&
                          fates[KERNEL_NETWORK_INTERFACE_FRAME_OF_UNKNOWN_TYPE] == 1u,
                      "two other stations, a group source and EtherType 86DDh");
    kernel_test_check(
        test,
        refused[KERNEL_INTERNET_PROTOCOL_NOT_VERSION_4] == 1u && refused[KERNEL_INTERNET_PROTOCOL_BAD_CHECKSUM] == 2u &&
            refused[KERNEL_INTERNET_PROTOCOL_FRAGMENT] == 1u && refused[KERNEL_INTERNET_PROTOCOL_TRUNCATED] == 1u,
        "version 6, an IHL of 6 and a flipped checksum, a fragment, a total past the frame");
    kernel_test_check(test,
                      fates[KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_FOR_OTHERS] == 1u &&
                          fates[KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_UNHANDLED] == 1u &&
                          fates[KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_SOURCE_REFUSED] == 2u,
                      "to 10.0.0.9, TCP, and from 127.0.0.1 and 10.0.0.255");
    kernel_test_check(test,
                      fates[KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_MALFORMED] == 2u &&
                          fates[KERNEL_NETWORK_INTERFACE_FRAME_OVERSIZED] == 1u &&
                          fates[KERNEL_NETWORK_INTERFACE_FRAME_TRUNCATED] == 1u,
                      "UDP lengths of 7 and a byte past, a slot past its buffer, and one of 13 bytes");
    kernel_test_check(test,
                      fates[KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT] == 0u &&
                          network_fake_interface.counters.datagrams_common == 0u,
                      "and none is lent");
    network_fake_check_counted(test, NETWORK_FAKE_SPOILS + 2u);
}

/**
 * @brief A sound datagram to a lent port takes the common path, one whose IPv4 header's words carry past 32 bits
 *        included.
 */
KERNEL_TEST(a_sound_datagram_to_a_lent_port_takes_the_common_path)
{
    uint8_t frame[64];

    network_fake_set_up();
    (void) network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false);
    memset(frame, 0, sizeof(frame));
    network_fake_receive(
        frame, network_fake_write_datagram(frame, NETWORK_FAKE_GAME_PORT, 6u, 0u, NETWORK_FAKE_CHECKSUM_SOUND));
    kernel_network_write_32(frame + KERNEL_ETHERNET_HEADER_BYTES + 12u, 0xDFFEFEFEu);
    kernel_network_write_16(frame + KERNEL_ETHERNET_HEADER_BYTES + 4u, 0xFFFFu);
    frame[KERNEL_ETHERNET_HEADER_BYTES + 1u] = 0xFFu;
    network_fake_seal_datagram(frame);
    network_fake_seal_header(frame);
    network_fake_receive(frame, KERNEL_NETWORK_INTERFACE_HEADERS_BYTES + 6u);
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT) == 2u &&
                          network_fake_interface.counters.datagrams_common == 2u,
                      "both datagrams are lent by the common path, the second from 223.254.254.254, its header's "
                      "words carrying past 32 bits");
    kernel_test_check(test,
                      kernel_network_interface_lent_at(&network_fake_interface, 1u)->source_address == 0xDFFEFEFEu,
                      "its source read as it arrived");
    network_fake_check_counted(test, 2u);
}

/**
 * @brief A port bound without checksum takes a datagram that has none and sends one with none; any other port may
 *        not send one.
 */
KERNEL_TEST(a_port_bound_without_checksum_takes_and_sends_datagrams_without_one)
{
    uint8_t frame[64] = {0u};
    KernelInternetProtocolPacket_t packet;
    KernelUserDatagram_t datagram;

    network_fake_set_up();
    (void) network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, true);
    network_fake_learn_peer();
    network_fake_receive(
        frame, network_fake_write_datagram(frame, NETWORK_FAKE_GAME_PORT, 4u, 0u, NETWORK_FAKE_CHECKSUM_NONE));
    (void) kernel_network_interface_poll(&network_fake_interface, 2u);
    kernel_test_check(test, network_fake_fate(KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT) == 1u,
                      "a datagram without checksum to port 9000, bound so, is lent");
    (void) kernel_network_interface_release_lent(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_send(NETWORK_FAKE_PEER, 4u, 0x31u, KERNEL_NETWORK_CHECKSUM_NONE, 3u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_SENT,
                      "and port 9000 sends one with none");
    (void) kernel_network_interface_poll(&network_fake_interface, 4u);
    kernel_test_check(test,
                      network_fake_read_sent_datagram(0u, &packet, &datagram) == KERNEL_USER_DATAGRAM_NO_CHECKSUM &&
                          datagram.payload_bytes == 4u && datagram.payload[0] == 0x31u,
                      "its checksum field is zero, its payload whole");
    network_fake_check_counted(test, 2u);
}

/**
 * @brief A port binds once, in a free place, and a binding without a port, or copied without its ring, is refused.
 */
KERNEL_TEST(a_port_binds_once_in_a_free_place)
{
    const KernelNetworkPortBinding_t copied_without_ring = {.port = 1u, .delivery = KERNEL_NETWORK_DELIVERY_COPIED};
    const KernelNetworkPortBinding_t unknown_delivery = {.port = 2u, .delivery = (KernelNetworkDelivery_t) 7};

    network_fake_set_up();
    kernel_test_check(test, network_fake_bind_lent(0u, false) == KERNEL_NETWORK_INTERFACE_BIND_BAD_BINDING,
                      "port 0 is refused");
    kernel_test_check(test,
                      kernel_network_interface_bind(&network_fake_interface, &copied_without_ring) ==
                              KERNEL_NETWORK_INTERFACE_BIND_BAD_BINDING &&
                          kernel_network_interface_bind(&network_fake_interface, &unknown_delivery) ==
                              KERNEL_NETWORK_INTERFACE_BIND_BAD_BINDING,
                      "and so are a copied port without its ring and a delivery that is none");
    kernel_test_check(test,
                      network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false) == KERNEL_NETWORK_INTERFACE_BIND_BOUND &&
                          network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, true) ==
                              KERNEL_NETWORK_INTERFACE_BIND_PORT_TAKEN,
                      "port 9000 binds once");
    for (uint16_t port = 1u; port < KERNEL_NETWORK_INTERFACE_PORTS; ++port)
        (void) network_fake_bind_lent((uint16_t) (10000u + port), false);
    kernel_test_check(test, network_fake_bind_lent(20000u, false) == KERNEL_NETWORK_INTERFACE_BIND_PORTS_FULL,
                      "and a ninth port finds every place taken");
}

/**
 * @brief A datagram is sent in place, its checksum from the payload's sum the caller gives, or from the interface's
 *        own sum, counted; one payload goes to three destinations on one sum.
 */
KERNEL_TEST(a_datagram_is_sent_in_place_its_checksum_from_its_payloads_sum)
{
    static const uint32_t peers[3] = {NETWORK_FAKE_PEER, 0x0A000003u, 0x0A000004u};
    static const uint8_t station[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};
    KernelInternetProtocolPacket_t packet;
    KernelUserDatagram_t datagram;
    bool every_one_sound = true;

    network_fake_set_up();
    (void) network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false);
    network_fake_learn_peer();
    kernel_test_check(test,
                      network_fake_send(NETWORK_FAKE_PEER, 9u, 0x5Au, KERNEL_NETWORK_CHECKSUM_FROM_SUM, 2u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_SENT,
                      "a datagram of 9 bytes to the peer is sent");
    (void) kernel_network_interface_poll(&network_fake_interface, 3u);
    kernel_test_check(test,
                      network_fake_read_sent_datagram(0u, &packet, &datagram) == KERNEL_USER_DATAGRAM_TAKEN &&
                          packet.source == NETWORK_FAKE_ADDRESS && packet.destination == NETWORK_FAKE_PEER &&
                          datagram.source_port == NETWORK_FAKE_GAME_PORT &&
                          datagram.destination_port == NETWORK_FAKE_PEER_PORT && datagram.payload_bytes == 9u &&
                          datagram.payload[8] == 0x5Au &&
                          memcmp(network_fake_card.sent[0], network_fake_peer_station, 6u) == 0,
                      "from 10.0.0.2:9000 to 10.0.0.1:7777, at the peer's station, its checksum sound");
    kernel_test_check(test, network_fake_interface.counters.checksums_computed == 0u,
                      "and the interface summed no payload");
    kernel_test_check(test,
                      network_fake_send(NETWORK_FAKE_PEER, 9u, 0x5Au, KERNEL_NETWORK_CHECKSUM_COMPUTED, 4u) ==
                              KERNEL_NETWORK_INTERFACE_SEND_SENT &&
                          network_fake_interface.counters.checksums_computed == 1u,
                      "given no sum, the interface sums the payload, and counts it");
    (void) kernel_network_interface_poll(&network_fake_interface, 5u);
    kernel_test_check(test, network_fake_read_sent_datagram(1u, &packet, &datagram) == KERNEL_USER_DATAGRAM_TAKEN,
                      "its checksum is sound too");
    for (uint32_t peer = 1u; peer < 3u; ++peer)
        (void) kernel_address_resolution_cache_add(&network_fake_interface.cache, peers[peer], station, 5u);
    network_fake_card.sent_count = 0u;

    uint32_t slots[3] = {0u};
    uint32_t room = 0u;
    uint8_t payload[24];

    memset(payload, 0x42, sizeof(payload));

    const uint32_t payload_sum = kernel_internet_checksum_add(0u, payload, sizeof(payload));

    for (uint32_t peer = 0u; peer < 3u; ++peer)
    {
        uint8_t *const place = kernel_network_interface_reserve_datagram(&network_fake_interface, &slots[peer], &room);
        const KernelNetworkDatagramOut_t out = {.destination_address = peers[peer],
                                                .destination_port = NETWORK_FAKE_PEER_PORT,
                                                .source_port = NETWORK_FAKE_GAME_PORT,
                                                .payload_bytes = sizeof(payload),
                                                .payload_sum = payload_sum,
                                                .checksum = KERNEL_NETWORK_CHECKSUM_FROM_SUM};

        memcpy(place, payload, sizeof(payload));
        every_one_sound =
            every_one_sound && kernel_network_interface_send_datagram(&network_fake_interface, slots[peer], &out, 6u) ==
                                   KERNEL_NETWORK_INTERFACE_SEND_SENT;
    }
    (void) kernel_network_interface_poll(&network_fake_interface, 7u);
    for (uint32_t peer = 0u; peer < 3u; ++peer)
        every_one_sound = every_one_sound &&
                          network_fake_read_sent_datagram(peer, &packet, &datagram) == KERNEL_USER_DATAGRAM_TAKEN &&
                          packet.destination == peers[peer];
    kernel_test_check(test, every_one_sound && network_fake_interface.counters.checksums_computed == 1u,
                      "one payload, summed once, goes to 10.0.0.1, 10.0.0.3 and 10.0.0.4, each checksum sound");
    kernel_test_check(test, network_fake_interface.counters.sends[KERNEL_NETWORK_INTERFACE_SEND_SENT] == 5u,
                      "five sends are counted sent");
}

/**
 * @brief A datagram to a station not known is refused and its next hop asked for by ARP, at most once a request
 *        interval; once the reply learned, it goes.
 */
KERNEL_TEST(a_datagram_to_an_unknown_station_is_refused_and_asked_for_once_an_interval)
{
    uint8_t frame[64] = {0u};

    network_fake_set_up();
    (void) network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false);
    kernel_test_check(test,
                      network_fake_send(NETWORK_FAKE_PEER, 4u, 0u, KERNEL_NETWORK_CHECKSUM_FROM_SUM, 1u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_NO_STATION,
                      "a datagram to 10.0.0.1, never learned, is refused by name");
    kernel_test_check(test, network_fake_interface.counters.address_resolution_requests_sent == 1u,
                      "and 10.0.0.1 is asked for");
    (void) network_fake_send(NETWORK_FAKE_PEER, 4u, 0u, KERNEL_NETWORK_CHECKSUM_FROM_SUM,
                             1u + NETWORK_FAKE_REQUEST_INTERVAL - 1u);
    kernel_test_check(test, network_fake_interface.counters.address_resolution_requests_sent == 1u,
                      "a second send within the request interval asks nothing more");
    (void) network_fake_send(NETWORK_FAKE_PEER, 4u, 0u, KERNEL_NETWORK_CHECKSUM_FROM_SUM,
                             1u + NETWORK_FAKE_REQUEST_INTERVAL);
    kernel_test_check(test, network_fake_interface.counters.address_resolution_requests_sent == 2u,
                      "one after it asks again");
    network_fake_receive(frame, network_fake_write_address_resolution(
                                    frame, network_fake_own_station, KERNEL_ADDRESS_RESOLUTION_REPLY, NETWORK_FAKE_PEER,
                                    network_fake_peer_station, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 20u);
    kernel_test_check(test,
                      network_fake_send(NETWORK_FAKE_PEER, 4u, 0u, KERNEL_NETWORK_CHECKSUM_FROM_SUM, 21u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_SENT,
                      "and once the reply is learned, the datagram goes");
}

/**
 * @brief A send whose slot an answer took meanwhile, or another slot than the one reserved, is refused as stale.
 */
KERNEL_TEST(a_send_whose_slot_an_answer_took_is_refused_as_stale)
{
    uint8_t frame[64] = {0u};
    uint32_t slot = 0u;
    uint32_t room = 0u;
    const KernelNetworkDatagramOut_t out = {.destination_address = NETWORK_FAKE_PEER,
                                            .destination_port = NETWORK_FAKE_PEER_PORT,
                                            .source_port = NETWORK_FAKE_GAME_PORT,
                                            .payload_bytes = 4u,
                                            .checksum = KERNEL_NETWORK_CHECKSUM_COMPUTED};

    network_fake_set_up();
    network_fake_learn_peer();
    (void) kernel_network_interface_reserve_datagram(&network_fake_interface, &slot, &room);
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_ADDRESS));
    (void) kernel_network_interface_poll(&network_fake_interface, 2u);
    kernel_test_check(test,
                      kernel_network_interface_send_datagram(&network_fake_interface, slot, &out, 3u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_STALE_SLOT,
                      "an ARP answer took the slot reserved: the send is refused as stale");
    (void) kernel_network_interface_reserve_datagram(&network_fake_interface, &slot, &room);
    kernel_test_check(test,
                      kernel_network_interface_send_datagram(&network_fake_interface, slot + 1u, &out, 3u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_STALE_SLOT,
                      "and so is a send on a slot other than the one reserved");
    kernel_test_check(test, network_fake_card.sent_count == 1u, "only the answer went out");
}

/**
 * @brief A datagram past the card's longest frame, to an address no packet leaves for, or without checksum from a
 *        port not bound so, is refused by name.
 */
KERNEL_TEST(a_datagram_too_long_or_to_nowhere_is_refused_by_name)
{
    uint32_t slot = 0u;
    uint32_t room = 0u;

    network_fake_set_up();
    (void) network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false);
    network_fake_learn_peer();
    (void) kernel_network_interface_reserve_datagram(&network_fake_interface, &slot, &room);
    kernel_test_check(test, room == NETWORK_FAKE_FRAME_MOST - KERNEL_NETWORK_INTERFACE_HEADERS_BYTES,
                      "a payload may take 1472 bytes");
    kernel_test_check(test,
                      network_fake_send(NETWORK_FAKE_PEER, room + 1u, 0u, KERNEL_NETWORK_CHECKSUM_FROM_SUM, 2u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_TOO_LONG,
                      "1473 are refused as too long");
    kernel_test_check(test,
                      network_fake_send(0x7F000001u, 4u, 0u, KERNEL_NETWORK_CHECKSUM_FROM_SUM, 2u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_UNREACHABLE,
                      "127.0.0.1 is unreachable");
    kernel_test_check(test,
                      network_fake_send(NETWORK_FAKE_PEER, 4u, 0u, KERNEL_NETWORK_CHECKSUM_NONE, 2u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_ZERO_CHECKSUM_REFUSED,
                      "and port 9000, bound with checksums, sends none without one");
    (void) kernel_network_interface_poll(&network_fake_interface, 3u);
    kernel_test_check(test, network_fake_card.sent_count == 0u, "nothing goes out");
}

/**
 * @brief A lent datagram echoed back goes on the sum it arrived with: no second pass over its payload.
 */
KERNEL_TEST(an_echoed_datagram_goes_back_on_the_sum_it_arrived_with)
{
    static uint8_t frame[256];
    KernelInternetProtocolPacket_t packet;
    KernelUserDatagram_t datagram;
    uint32_t slot = 0u;
    uint32_t room = 0u;

    network_fake_set_up();
    (void) network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false);
    network_fake_learn_peer();
    network_fake_receive(
        frame, network_fake_write_datagram(frame, NETWORK_FAKE_GAME_PORT, 101u, 3u, NETWORK_FAKE_CHECKSUM_SOUND));
    (void) kernel_network_interface_poll(&network_fake_interface, 2u);

    const KernelNetworkDatagram_t *const lent = kernel_network_interface_lent_at(&network_fake_interface, 0u);
    uint8_t *const payload = kernel_network_interface_reserve_datagram(&network_fake_interface, &slot, &room);
    const KernelNetworkDatagramOut_t echo = {.destination_address = lent->source_address,
                                             .destination_port = lent->source_port,
                                             .source_port = lent->destination_port,
                                             .payload_bytes = lent->payload_bytes,
                                             .payload_sum = lent->payload_sum,
                                             .checksum = KERNEL_NETWORK_CHECKSUM_FROM_SUM};

    memcpy(payload, lent->payload, lent->payload_bytes);
    kernel_test_check(test,
                      kernel_network_interface_send_datagram(&network_fake_interface, slot, &echo, 3u) ==
                          KERNEL_NETWORK_INTERFACE_SEND_SENT,
                      "the datagram of 101 bytes is echoed");
    (void) kernel_network_interface_release_lent(&network_fake_interface, 1u);
    (void) kernel_network_interface_poll(&network_fake_interface, 4u);
    kernel_test_check(test,
                      network_fake_read_sent_datagram(0u, &packet, &datagram) == KERNEL_USER_DATAGRAM_TAKEN &&
                          datagram.payload_bytes == 101u && datagram.payload[100] == 103u &&
                          datagram.destination_port == NETWORK_FAKE_PEER_PORT,
                      "back to 10.0.0.1:7777, its odd payload whole and its checksum sound");
    kernel_test_check(test, network_fake_interface.counters.checksums_computed == 0u,
                      "on the sum it arrived with, the payload not summed again");
}

static KernelSpscRing_t network_fake_big_receive;
static KernelSpscRing_t network_fake_big_transmit;
static uint8_t network_fake_big_buffers[NETWORK_FAKE_BIG_SLOTS][NETWORK_FAKE_BIG_BYTES];
static uint16_t network_fake_big_lengths[NETWORK_FAKE_BIG_SLOTS];
static uint32_t network_fake_big_words[NETWORK_FAKE_BIG_SLOTS];

/**
 * @brief The big card's kick: nothing is sent through it.
 */
static uint32_t network_fake_big_kick(KernelNetworkDevice_t *device, uint32_t queue)
{
    (void) device;
    (void) queue;
    return 0u;
}

static const KernelNetworkDeviceOperations_t network_fake_big_operations = {.harvest = network_fake_harvest,
                                                                            .kick = network_fake_big_kick,
                                                                            .link_up = network_fake_link_up};

/**
 * @brief More datagrams lent at once than the interface has places for: the one past the last place is dropped and
 *        counted, never lent over another.
 */
KERNEL_TEST(a_datagram_past_the_lent_places_is_dropped_and_counted)
{
    KernelNetworkQueue_t queue = {.receive = &network_fake_big_receive,
                                  .receive_buffers = &network_fake_big_buffers[0][0],
                                  .receive_lengths = network_fake_big_lengths,
                                  .receive_length_stride = 1u,
                                  .transmit = &network_fake_big_transmit,
                                  .transmit_buffers = &network_fake_big_buffers[0][0],
                                  .transmit_words = network_fake_big_words,
                                  .transmit_word_stride = 1u};
    KernelNetworkDevice_t device = {.name = "big",
                                    .buffer_bytes = NETWORK_FAKE_BIG_BYTES,
                                    .frame_least_bytes = KERNEL_ETHERNET_HEADER_BYTES,
                                    .frame_most_bytes = NETWORK_FAKE_BIG_BYTES,
                                    .queue_count = 1u,
                                    .queues = &queue,
                                    .operations = &network_fake_big_operations};
    const KernelNetworkInterfaceConfig_t config = network_fake_config(0u);
    const uint32_t datagrams = KERNEL_NETWORK_INTERFACE_LENT_DATAGRAMS + 1u;
    uint32_t slot = 0u;

    memcpy(device.address, network_fake_own_station, 6u);
    (void) kernel_spsc_ring_initialize(&network_fake_big_receive, NETWORK_FAKE_BIG_SLOTS);
    (void) kernel_spsc_ring_initialize(&network_fake_big_transmit, NETWORK_FAKE_BIG_SLOTS);
    (void) kernel_network_interface_initialize(&network_fake_interface, &device, &config);
    (void) network_fake_bind_lent(NETWORK_FAKE_GAME_PORT, false);
    for (uint32_t datagram = 0u; datagram < datagrams; ++datagram)
    {
        (void) kernel_spsc_ring_reserve(&network_fake_big_receive, 1u, &slot);
        network_fake_big_lengths[slot] =
            (uint16_t) network_fake_write_datagram(network_fake_big_buffers[slot], NETWORK_FAKE_GAME_PORT, 4u,
                                                   (uint8_t) datagram, NETWORK_FAKE_CHECKSUM_SOUND);
        kernel_spsc_ring_publish(&network_fake_big_receive, 1u);
    }
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_fate(KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT) ==
                              KERNEL_NETWORK_INTERFACE_LENT_DATAGRAMS &&
                          network_fake_fate(KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT_FULL) == 1u,
                      "64 datagrams are lent, and the 65th is dropped, counted");
    kernel_test_check(test,
                      kernel_network_interface_lent_at(&network_fake_interface, 63u)->payload[0] == 63u &&
                          kernel_network_interface_release_lent(&network_fake_interface, datagrams) ==
                              KERNEL_NETWORK_INTERFACE_LENT_DATAGRAMS &&
                          kernel_spsc_ring_size(&network_fake_big_receive) == 0u,
                      "the 64th lent is the 64th sent, and releasing all gives every slot back");
    network_fake_check_counted(test, datagrams);
}
