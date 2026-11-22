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
#define NETWORK_FAKE_ADDRESS      0x0A000002u /**< 10.0.0.2, the interface's address. */
#define NETWORK_FAKE_PEER         0x0A000001u /**< 10.0.0.1, the peer's. */
#define NETWORK_FAKE_OTHER        0x0A000009u /**< 10.0.0.9, an address neither has. */

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
} NetworkFakeCard_t;

static NetworkFakeCard_t network_fake_card;
static KernelNetworkQueue_t network_fake_queue;
static KernelNetworkDevice_t network_fake_device;
static KernelNetworkInterface_t network_fake_interface;
static const uint8_t network_fake_own_station[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u};
static const uint8_t network_fake_peer_station[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};

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
 * @brief The made-up card's kick: captures every frame published, then gives their slots back.
 */
static uint32_t network_fake_kick(KernelNetworkDevice_t *device, uint32_t queue)
{
    uint32_t first_slot = 0u;
    uint32_t handed = 0u;

    (void) device;
    (void) queue;
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
 * @brief Sets up the made-up card, empty, and the interface 10.0.0.2/24 on it.
 */
static void network_fake_set_up(void)
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
                                                  .frame_least_bytes = 14u,
                                                  .frame_most_bytes = 1514u,
                                                  .queue_count = 1u,
                                                  .queues = &network_fake_queue,
                                                  .operations = &network_fake_operations};
    memcpy(network_fake_device.address, network_fake_own_station, 6u);
    (void) kernel_network_interface_initialize(&network_fake_interface, &network_fake_device, NETWORK_FAKE_ADDRESS,
                                               0xFFFFFF00u, 0u, 100u);
}

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
 * @brief Writes an ARP request from the peer for @p target, broadcast.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_request(uint8_t *frame, uint32_t target)
{
    static const uint8_t broadcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    KernelAddressResolutionPacket_t request = {.operation = KERNEL_ADDRESS_RESOLUTION_REQUEST,
                                               .sender_address = NETWORK_FAKE_PEER,
                                               .target_address = target};

    memcpy(request.sender_station, network_fake_peer_station, 6u);
    kernel_ethernet_write_header(frame, broadcast, network_fake_peer_station, KERNEL_ETHERNET_TYPE_ARP);
    kernel_address_resolution_write(frame + 14u, &request);
    return 14u + KERNEL_ADDRESS_RESOLUTION_BYTES;
}

/**
 * @brief Writes an echo request of 12 bytes from the peer to @p destination.
 *
 * @return Its length.
 */
static uint32_t network_fake_write_echo(uint8_t *frame, uint32_t destination)
{
    uint8_t *const echo = frame + 14u + 20u;

    kernel_ethernet_write_header(frame, network_fake_own_station, network_fake_peer_station, KERNEL_ETHERNET_TYPE_IPV4);
    kernel_internet_protocol_write_header(frame + 14u, NETWORK_FAKE_PEER, destination, KERNEL_INTERNET_PROTOCOL_ICMP,
                                          12u, 3u);
    memset(echo, 0x77, 12u);
    echo[0] = KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST;
    echo[1] = 0u;
    kernel_network_write_16(echo + 2u, 0u);
    kernel_network_write_16(echo + 2u, kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, echo, 12u)));
    return 14u + 20u + 12u;
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
                          reply_frame.type == KERNEL_ETHERNET_TYPE_ARP &&
                          kernel_address_resolution_parse(reply_frame.payload, reply_frame.payload_bytes, &reply),
                      "an ARP frame to the peer's station");
    kernel_test_check(test,
                      reply.operation == KERNEL_ADDRESS_RESOLUTION_REPLY &&
                          reply.sender_address == NETWORK_FAKE_ADDRESS &&
                          memcmp(reply.sender_station, network_fake_own_station, 6u) == 0 &&
                          reply.target_address == NETWORK_FAKE_PEER,
                      "a reply that gives 10.0.0.2's station to 10.0.0.1");
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 2u, station) &&
                          memcmp(station, network_fake_peer_station, 6u) == 0,
                      "and the peer's station is learned");
}

/**
 * @brief An ARP request for another address goes unanswered, and teaches the interface nothing.
 */
KERNEL_TEST(an_arp_request_for_another_address_teaches_nothing)
{
    uint8_t frame[64] = {0u};
    uint8_t station[6] = {0u};

    network_fake_set_up();
    network_fake_receive(frame, network_fake_write_request(frame, NETWORK_FAKE_OTHER));
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test, network_fake_card.sent_count == 0u, "no frame is sent");
    kernel_test_check(test, network_fake_interface.counters.address_resolution_for_others == 1u,
                      "the request is counted as for another");
    kernel_test_check(test,
                      !kernel_network_interface_find_station(&network_fake_interface, NETWORK_FAKE_PEER, 2u, station),
                      "and its sender is not learned");
}

/**
 * @brief An echo request to the interface is answered, addresses swapped; one to another address is not.
 */
KERNEL_TEST(an_echo_request_is_answered_only_when_it_is_the_interfaces)
{
    uint8_t frame[64] = {0u};
    KernelEthernetFrame_t reply_frame;
    KernelInternetProtocolPacket_t reply;

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
                          reply.protocol == KERNEL_INTERNET_PROTOCOL_ICMP && reply.payload_bytes == 12u &&
                          reply.payload[0] == KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY,
                      "from 10.0.0.2 to 10.0.0.1, an echo reply of 12 bytes");
    kernel_test_check(test, network_fake_interface.counters.internet_protocol_for_others == 1u,
                      "and the request to 10.0.0.9 is counted as for another");
}

/**
 * @brief Frames for another station, of an unknown EtherType or shorter than a header are dropped, each counted.
 */
KERNEL_TEST(frames_the_interface_does_not_take_are_counted_by_their_cause)
{
    uint8_t frame[64] = {0u};
    static const uint8_t elsewhere[6] = {0x02u, 0u, 0u, 0u, 0u, 0x55u};

    network_fake_set_up();
    (void) network_fake_write_echo(frame, NETWORK_FAKE_ADDRESS);
    memcpy(frame, elsewhere, 6u);
    network_fake_receive(frame, 46u);
    memcpy(frame, network_fake_own_station, 6u);
    frame[12] = 0x86u;
    frame[13] = 0xDDu;
    network_fake_receive(frame, 46u);
    network_fake_receive(frame, 13u);
    (void) kernel_network_interface_poll(&network_fake_interface, 1u);
    kernel_test_check(test,
                      network_fake_interface.counters.frames_for_others == 1u &&
                          network_fake_interface.counters.frames_of_unknown_type == 1u &&
                          network_fake_interface.counters.frames_truncated == 1u &&
                          network_fake_interface.counters.frames_received == 3u,
                      "one for another station, one of EtherType 86DDh, one of 13 bytes, out of three");
    kernel_test_check(test, network_fake_card.sent_count == 0u, "and none is answered");
}
