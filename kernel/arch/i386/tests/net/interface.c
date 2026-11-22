#include <kernel/cpu/irq.h>
#include <kernel/drivers/e1000.h>
#include <kernel/lib/asmutils.h>
#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <string.h>

#define NETWORK_INTERFACE_TEST_ADDRESS        0x0A00020Fu /**< 10.0.2.15, the loopback tests' own address. */
#define NETWORK_INTERFACE_TEST_NETMASK        0xFFFFFF00u /**< A /24. */
#define NETWORK_INTERFACE_TEST_NEIGHBOUR      0x0A000202u /**< 10.0.2.2, a neighbour the loopback tests make up. */
#define NETWORK_INTERFACE_TEST_ASKED          0x0A000203u /**< 10.0.2.3, a neighbour the interface asks for. */
#define NETWORK_INTERFACE_TEST_PEER_ADDRESS   0x0A000002u /**< 10.0.0.2, the kernel's address facing a Linux peer. */
#define NETWORK_INTERFACE_TEST_LIFETIME       1000000u    /**< A cache lifetime no test outlives, in ticks. */
#define NETWORK_INTERFACE_TEST_MICROSECONDS   2000000u    /**< A frame's way back, past QEMU's 1 s after RCTL. */
#define NETWORK_INTERFACE_TEST_PEER_SECONDS   30u         /**< How long the peer test answers. */
#define NETWORK_INTERFACE_TEST_PEER_PINGS     5u          /**< Echo requests the peer sends, and the test waits for. */
#define NETWORK_INTERFACE_TEST_ECHO_BYTES     16u         /**< An echo request's bytes: its header and 8 of data. */
#define NETWORK_INTERFACE_TEST_INTERRUPT_FLAG (1u << 9)   /**< EFLAGS.IF. */
#define NETWORK_INTERFACE_TEST_ETHER_TYPE     0x88B5u     /**< IEEE 802's Local Experimental 1: no stack answers. */
#define NETWORK_INTERFACE_TEST_FRAME_BYTES    60u         /**< Ethernet's minimum, CRC left out: never padded. */
#define NETWORK_INTERFACE_TEST_BOUNCE_FRAMES  1024u       /**< Frames the bounce sends. */
#define NETWORK_INTERFACE_TEST_BOUNCE_BATCH   16u         /**< Frames one publish of the bounce sends. */
#define NETWORK_INTERFACE_TEST_IDLE_ROUNDS    4096u       /**< Empty peeks after which the bounce gives up. */

static KernelNetworkInterface_t network_interface_test;

/**
 * @brief Waits for the next interrupt when interrupts are on, or spins once when they are off.
 */
static void network_interface_test_wait_a_little(void)
{
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();

    asmutils_restore_flags(flags);
    if ((flags & NETWORK_INTERFACE_TEST_INTERRUPT_FLAG) != 0u)
        asmutils_halt();
    else
        asmutils_pause();
}

/**
 * @brief Brings the e1000 up in loopback, unless it already is, and sets the interface up on it.
 *
 * @return false, skipped or failed, when the test cannot run.
 */
static bool network_interface_test_bring_up(KernelTest_t *test, uint32_t address, bool loopback)
{
    if (!e1000_state()->controller_ready && !e1000_initialize())
    {
        kernel_test_skip(test, "no 8254x the driver brings up");
        return false;
    }
    if (e1000_state()->loopback != loopback &&
        !kernel_test_check(test, e1000_set_loopback(loopback), "the PHY takes the loopback asked"))
        return false;
    return kernel_test_check(test,
                             kernel_network_interface_initialize(&network_interface_test, e1000_network_device(),
                                                                 address, NETWORK_INTERFACE_TEST_NETMASK, 0u,
                                                                 NETWORK_INTERFACE_TEST_LIFETIME) ==
                                 KERNEL_NETWORK_INTERFACE_READY,
                             "the interface sets up on the e1000");
}

/**
 * @brief Sends the frame of @p length bytes at @p bytes through the interface's card, past the interface.
 */
static bool network_interface_test_send(const uint8_t *bytes, uint32_t length)
{
    KernelNetworkDevice_t *const device = network_interface_test.device;
    KernelNetworkQueue_t *const queue = &device->queues[0];
    uint32_t slot = 0u;

    (void) device->operations->kick(device, 0u);
    if (kernel_spsc_ring_reserve(queue->transmit, 1u, &slot) == 0u)
        return false;
    memcpy(kernel_network_transmit_buffer(device, queue, slot), bytes, length);
    if (!kernel_network_transmit_describe(device, queue, slot, length))
        return false;
    kernel_spsc_ring_publish(queue->transmit, 1u);
    return device->operations->kick(device, 0u) == 1u;
}

/**
 * @brief Writes an ARP packet of @p operation from @p sender about @p target, the card's own station on both sides
 *        of the frame so that the loopback brings it, and the answer to it, back.
 *
 * @return Its length.
 */
static uint32_t network_interface_test_write_address_resolution(uint8_t *frame, uint16_t operation, uint32_t sender,
                                                                uint32_t target)
{
    const uint8_t *const station = network_interface_test.device->address;
    KernelAddressResolutionPacket_t packet = {.operation = operation,
                                              .sender_address = sender,
                                              .target_address = target};

    memcpy(packet.sender_station, station, KERNEL_NETWORK_ADDRESS_BYTES);
    kernel_ethernet_write_header(frame, station, station, KERNEL_ETHERNET_TYPE_ADDRESS_RESOLUTION);
    kernel_address_resolution_write(frame + KERNEL_ETHERNET_HEADER_BYTES, &packet);
    return KERNEL_ETHERNET_HEADER_BYTES + KERNEL_ADDRESS_RESOLUTION_BYTES;
}

/**
 * @brief Writes an echo request of 16 bytes from the made-up neighbour 10.0.2.2 to the interface, the card's own
 *        station on both sides of the frame.
 *
 * @return Its length.
 */
static uint32_t network_interface_test_write_echo(uint8_t *frame)
{
    const uint8_t *const station = network_interface_test.device->address;
    uint8_t *const echo = frame + KERNEL_ETHERNET_HEADER_BYTES + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES;

    kernel_ethernet_write_header(frame, station, station, KERNEL_ETHERNET_TYPE_INTERNET_PROTOCOL_VERSION_4);
    kernel_internet_protocol_write_header(frame + KERNEL_ETHERNET_HEADER_BYTES, NETWORK_INTERFACE_TEST_NEIGHBOUR,
                                          network_interface_test.address, KERNEL_INTERNET_PROTOCOL_CONTROL_MESSAGE,
                                          NETWORK_INTERFACE_TEST_ECHO_BYTES, 1u);
    memset(echo, 0x5A, NETWORK_INTERFACE_TEST_ECHO_BYTES);
    echo[0] = KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST;
    echo[1] = 0u;
    kernel_network_write_16(echo + 2u, 0u);
    kernel_network_write_16(echo + 2u, kernel_internet_checksum_finish(
                                           kernel_internet_checksum_add(0u, echo, NETWORK_INTERFACE_TEST_ECHO_BYTES)));
    return KERNEL_ETHERNET_HEADER_BYTES + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES + NETWORK_INTERFACE_TEST_ECHO_BYTES;
}

/**
 * @brief Whether the frame of @p length bytes at @p bytes is the interface's echo reply to @p request, its data
 *        the request's and its checksum sound.
 */
static bool network_interface_test_is_echo_reply_to(const uint8_t *bytes, uint32_t length, const uint8_t *request)
{
    const uint8_t *const request_echo = request + KERNEL_ETHERNET_HEADER_BYTES + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES;
    KernelEthernetFrame_t frame;
    KernelInternetProtocolPacket_t packet;
    KernelInternetControlMessage_t message;

    return kernel_ethernet_parse(bytes, length, &frame) &&
           frame.type == KERNEL_ETHERNET_TYPE_INTERNET_PROTOCOL_VERSION_4 &&
           kernel_internet_protocol_parse(frame.payload, frame.payload_bytes, &packet) ==
               KERNEL_INTERNET_PROTOCOL_TAKEN &&
           packet.source == network_interface_test.address && packet.destination == NETWORK_INTERFACE_TEST_NEIGHBOUR &&
           kernel_internet_control_message_parse(packet.payload, packet.payload_bytes, &message) &&
           message.type == KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY &&
           packet.payload_bytes == NETWORK_INTERFACE_TEST_ECHO_BYTES &&
           memcmp(packet.payload + 4, request_echo + 4, NETWORK_INTERFACE_TEST_ECHO_BYTES - 4u) == 0;
}

/**
 * @brief Harvests the card past the interface, and tells whether one of its frames is the echo reply to @p request.
 */
static bool network_interface_test_read_echo_reply(const uint8_t *request)
{
    KernelNetworkDevice_t *const device = network_interface_test.device;
    KernelNetworkQueue_t *const queue = &device->queues[0];
    bool found = false;

    (void) device->operations->harvest(device, 0u);

    const uint32_t ready = kernel_spsc_ring_ready_count(queue->receive);

    for (uint32_t frame = 0u; frame < ready; ++frame)
    {
        const uint32_t slot = kernel_spsc_ring_slot_at(queue->receive, frame);

        found = found || network_interface_test_is_echo_reply_to(kernel_network_receive_bytes(device, queue, slot),
                                                                 kernel_network_receive_length(queue, slot), request);
    }
    kernel_spsc_ring_release(queue->receive, ready);
    return found;
}

/**
 * @brief Writes, once, a frame of 60 bytes to the card's own station in every transmit buffer.
 */
static void network_interface_test_fill_transmit_buffers(KernelNetworkDevice_t *device)
{
    KernelNetworkQueue_t *const queue = &device->queues[0];
    const uint32_t slots = kernel_spsc_ring_capacity(queue->transmit);

    for (uint32_t slot = 0u; slot < slots; ++slot)
    {
        uint8_t *const frame = kernel_network_transmit_buffer(device, queue, slot);

        memset(frame, 0, NETWORK_INTERFACE_TEST_FRAME_BYTES);
        kernel_ethernet_write_header(frame, device->address, device->address, NETWORK_INTERFACE_TEST_ETHER_TYPE);
    }
}

/**
 * @brief Sends the frames of the transmit buffers through the loopback until one comes back, within
 *        NETWORK_INTERFACE_TEST_MICROSECONDS: QEMU drops looped-back frames for 1 s after a write of RCTL.
 */
static bool network_interface_test_wait_for_the_loopback(KernelNetworkDevice_t *device)
{
    KernelNetworkQueue_t *const queue = &device->queues[0];
    InterruptRequestDeadline_t deadline;
    uint32_t slot = 0u;

    interrupt_request_deadline_start(&deadline, NETWORK_INTERFACE_TEST_MICROSECONDS);
    while (!interrupt_request_deadline_passed(&deadline))
    {
        (void) device->operations->kick(device, 0u);
        if (kernel_spsc_ring_reserve(queue->transmit, 1u, &slot) == 1u &&
            kernel_network_transmit_describe(device, queue, slot, NETWORK_INTERFACE_TEST_FRAME_BYTES))
            kernel_spsc_ring_publish(queue->transmit, 1u);
        (void) device->operations->kick(device, 0u);
        (void) device->operations->harvest(device, 0u);

        const uint32_t ready = kernel_spsc_ring_ready_count(queue->receive);

        kernel_spsc_ring_release(queue->receive, ready);
        if (ready != 0u)
            return true;
        network_interface_test_wait_a_little();
    }
    return false;
}

/**
 * @brief Bounces @p frames frames of 60 bytes through the loopback by the network device interface alone, in
 *        publishes of at most @p batch, reading back each frame's length.
 *
 * @details The counterpart of e1000_test_bounce through kernel_network_* and the card's operations, so that a
 *          count of the two sets the interface's cost a frame against the driver's own. It reads the card and its
 *          queue pair through copies taken once, as device.h says a batch does. Kept out of interprocedural
 *          optimisation (noipa), so that its symbol keeps its name.
 *
 * @return The bytes the frames received carried, summed.
 */
__attribute__((noipa)) static uint32_t network_interface_test_bounce(KernelNetworkDevice_t *device, uint32_t frames,
                                                                     uint32_t batch)
{
    const KernelNetworkDevice_t card = *device;
    const KernelNetworkQueue_t queue = device->queues[0];
    uint32_t sent = 0u;
    uint32_t received = 0u;
    uint32_t bytes = 0u;
    uint32_t idle = 0u;

    while (received < frames && idle < NETWORK_INTERFACE_TEST_IDLE_ROUNDS)
    {
        uint32_t first_slot = 0u;

        if (sent < frames)
        {
            const uint32_t wanted = frames - sent < batch ? frames - sent : batch;
            const uint32_t reserved = kernel_spsc_ring_reserve(queue.transmit, wanted, &first_slot);

            for (uint32_t frame = 0u; frame < reserved; ++frame)
                (void) kernel_network_transmit_describe(&card, &queue, first_slot + frame,
                                                        NETWORK_INTERFACE_TEST_FRAME_BYTES);
            kernel_spsc_ring_publish(queue.transmit, reserved);
            sent += reserved;
            (void) card.operations->kick(device, 0u);
        }
        (void) card.operations->harvest(device, 0u);

        const uint32_t peeked =
            kernel_spsc_ring_peek(queue.receive, kernel_spsc_ring_capacity(queue.receive), &first_slot);

        for (uint32_t frame = 0u; frame < peeked; ++frame)
            bytes += kernel_network_receive_length(&queue, first_slot + frame);
        kernel_spsc_ring_release(queue.receive, peeked);
        received += peeked;
        idle = peeked == 0u ? idle + 1u : 0u;
    }
    (void) card.operations->harvest(device, 0u);
    return bytes;
}

KERNEL_TEST_SUITE(network_interface, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief Through the loopback, the interface answers an ARP request from a neighbour and learns it, and learns the
 *        reply to a request it sent; each answer comes back through the card as a packet for another address.
 *
 * @details The neighbours are made up, the card's own station standing for theirs, so that the loopback brings
 *          their packets in and the answers back; each is sent again until it comes back, as QEMU drops
 *          looped-back frames for 1 s after the driver's last write of RCTL.
 */
KERNEL_TEST(the_interface_answers_an_arp_request_and_learns_the_reply_it_asked_for)
{
    if (!network_interface_test_bring_up(test, NETWORK_INTERFACE_TEST_ADDRESS, true))
        return;

    const uint32_t *const fates = network_interface_test.counters.fates;
    const uint8_t *const own_station = e1000_state()->address;
    InterruptRequestDeadline_t deadline;
    uint8_t frame[64] = {0u};
    uint8_t station[KERNEL_NETWORK_ADDRESS_BYTES] = {0u};
    uint32_t now = 0u;

    interrupt_request_deadline_start(&deadline, NETWORK_INTERFACE_TEST_MICROSECONDS);
    while ((fates[KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED] == 0u ||
            fates[KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REPLY_LEARNED] == 0u) &&
           !interrupt_request_deadline_passed(&deadline))
    {
        (void) network_interface_test_send(
            frame, network_interface_test_write_address_resolution(frame, KERNEL_ADDRESS_RESOLUTION_REQUEST,
                                                                   NETWORK_INTERFACE_TEST_NEIGHBOUR,
                                                                   NETWORK_INTERFACE_TEST_ADDRESS));
        (void) kernel_network_interface_request_address(&network_interface_test, NETWORK_INTERFACE_TEST_ASKED, ++now);
        (void) network_interface_test_send(frame, network_interface_test_write_address_resolution(
                                                      frame, KERNEL_ADDRESS_RESOLUTION_REPLY,
                                                      NETWORK_INTERFACE_TEST_ASKED, NETWORK_INTERFACE_TEST_ADDRESS));
        for (uint32_t round = 0u; round < 8u; ++round)
        {
            (void) kernel_network_interface_poll(&network_interface_test, ++now);
            network_interface_test_wait_a_little();
        }
    }

    kernel_test_check(test, fates[KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED] != 0u,
                      "the interface answers 10.0.2.2's request for its address");
    kernel_test_check(test,
                      kernel_network_interface_find_station(&network_interface_test, NETWORK_INTERFACE_TEST_NEIGHBOUR,
                                                            now, station) &&
                          memcmp(station, own_station, KERNEL_NETWORK_ADDRESS_BYTES) == 0,
                      "and learns 10.0.2.2 at the station its request came from");
    kernel_test_check(
        test,
        fates[KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REPLY_LEARNED] != 0u &&
            kernel_network_interface_find_station(&network_interface_test, NETWORK_INTERFACE_TEST_ASKED, now, station),
        "and learns the reply about 10.0.2.3, which it asked for");
    kernel_test_check(test, fates[KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_FOR_OTHERS] != 0u,
                      "its answers and requests come back through the card, about other addresses");
    kernel_test_check(test,
                      fates[KERNEL_NETWORK_INTERFACE_FRAME_FOR_OTHERS] == 0u &&
                          fates[KERNEL_NETWORK_INTERFACE_FRAME_OF_UNKNOWN_TYPE] == 0u,
                      "and no frame was dropped on the way");
    kernel_test_measure(test, "requests_sent", network_interface_test.counters.address_resolution_requests_sent);
}

/**
 * @brief Through the loopback, the interface answers an echo request from a neighbour, and its reply, read back
 *        from the card, carries the request's data with a sound checksum.
 */
KERNEL_TEST(the_interface_answers_an_echo_request)
{
    if (!network_interface_test_bring_up(test, NETWORK_INTERFACE_TEST_ADDRESS, true))
        return;

    const uint32_t *const fates = network_interface_test.counters.fates;
    InterruptRequestDeadline_t deadline;
    uint8_t request[64] = {0u};
    bool reply_read = false;
    uint32_t now = 0u;
    const uint32_t length = network_interface_test_write_echo(request);

    interrupt_request_deadline_start(&deadline, NETWORK_INTERFACE_TEST_MICROSECONDS);
    while (!reply_read && !interrupt_request_deadline_passed(&deadline))
    {
        if (fates[KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED] == 0u)
        {
            (void) network_interface_test_send(request, length);
            (void) kernel_network_interface_poll(&network_interface_test, ++now);
        }
        else
            reply_read = network_interface_test_read_echo_reply(request);
        network_interface_test_wait_a_little();
    }

    kernel_test_check(test, fates[KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED] != 0u,
                      "the interface answers 10.0.2.2's echo request");
    kernel_test_check(test, reply_read, "and its reply comes back with the request's data and a sound checksum");
    kernel_test_check(
        test,
        fates[KERNEL_NETWORK_INTERFACE_CONTROL_MESSAGE_MALFORMED] == 0u &&
            network_interface_test.counters.internet_protocol_refused[KERNEL_INTERNET_PROTOCOL_BAD_CHECKSUM] == 0u,
        "with no message refused on the way");
}

/**
 * @brief The card's link reads through the network device interface as the driver reads it.
 */
KERNEL_TEST(the_link_reads_through_the_network_device_interface)
{
    if (!network_interface_test_bring_up(test, NETWORK_INTERFACE_TEST_ADDRESS, true))
        return;

    KernelNetworkDevice_t *const device = network_interface_test.device;

    kernel_test_check(test, device->operations->link_up(device) == e1000_link_up(),
                      "the operation gives STATUS.LU, as e1000_link_up does");
}

/**
 * @brief 1024 frames bounce through the loopback by the network device interface alone, with no byte written per
 *        frame, so that a count of network_interface_test_bounce sets its cost against e1000_test_bounce's.
 *
 * @details `tools/instructions.sh --inside network_interface_test_bounce --times 1 --append
 *          lpl.test=network_interface.*bounce*` on a debug kernel counts it.
 */
KERNEL_TEST(frames_bounce_through_the_network_device_interface)
{
    if (!network_interface_test_bring_up(test, NETWORK_INTERFACE_TEST_ADDRESS, true))
        return;

    KernelNetworkDevice_t *const device = network_interface_test.device;

    network_interface_test_fill_transmit_buffers(device);
    if (!kernel_test_check(test, network_interface_test_wait_for_the_loopback(device), "the loopback carries frames"))
        return;

    const uint32_t bytes = network_interface_test_bounce(device, NETWORK_INTERFACE_TEST_BOUNCE_FRAMES,
                                                         NETWORK_INTERFACE_TEST_BOUNCE_BATCH);

    kernel_test_check(test, bytes == NETWORK_INTERFACE_TEST_BOUNCE_FRAMES * NETWORK_INTERFACE_TEST_FRAME_BYTES,
                      "1024 frames of 60 bytes bounce");
}

/**
 * @brief A Linux peer, on the other side of a tap device, resolves the kernel's address by ARP and pings it.
 *
 * @details tools/net/linux-peer.sh runs it: QEMU's e1000 on a tap device of an unprivileged network namespace,
 *          whose own Linux stack is the peer, at 10.0.0.1, the kernel at 10.0.0.2. The test answers for
 *          NETWORK_INTERFACE_TEST_PEER_SECONDS, or until it answered the peer's five echo requests and one more second.
 */
KERNEL_TEST_MANUAL(answers_a_linux_peer, "needs a peer on the network, as tools/net/linux-peer.sh gives it")
{
    if (!network_interface_test_bring_up(test, NETWORK_INTERFACE_TEST_PEER_ADDRESS, false))
        return;

    const KernelNetworkInterfaceCounters_t *const counters = &network_interface_test.counters;
    InterruptRequestDeadline_t deadline;
    InterruptRequestDeadline_t grace;
    bool answered = false;
    uint32_t now = 0u;

    kernel_test_note(test, "answering at 10.0.0.2");
    interrupt_request_deadline_start(&deadline, NETWORK_INTERFACE_TEST_PEER_SECONDS * 1000000u);
    while (!interrupt_request_deadline_passed(&deadline))
    {
        (void) kernel_network_interface_poll(&network_interface_test, ++now);
        if (!answered && counters->fates[KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED] >= NETWORK_INTERFACE_TEST_PEER_PINGS)
        {
            answered = true;
            interrupt_request_deadline_start(&grace, 1000000u);
        }
        if (answered && interrupt_request_deadline_passed(&grace))
            break;
        network_interface_test_wait_a_little();
    }

    kernel_test_check(test, counters->fates[KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED] != 0u,
                      "the peer's ARP request for 10.0.0.2 is answered");
    kernel_test_check(test,
                      counters->fates[KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED] >= NETWORK_INTERFACE_TEST_PEER_PINGS,
                      "and its five echo requests too");
    kernel_test_measure(test, "frames_received", counters->frames_received);
    kernel_test_measure(test, "address_resolution_requests_answered",
                        counters->fates[KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED]);
    kernel_test_measure(test, "echo_requests_answered", counters->fates[KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED]);
    kernel_test_measure(test, "frames_for_others", counters->fates[KERNEL_NETWORK_INTERFACE_FRAME_FOR_OTHERS]);
    kernel_test_measure(test, "internet_protocol_for_others",
                        counters->fates[KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_FOR_OTHERS]);
    kernel_test_measure(test, "frames_of_unknown_type",
                        counters->fates[KERNEL_NETWORK_INTERFACE_FRAME_OF_UNKNOWN_TYPE]);
}
