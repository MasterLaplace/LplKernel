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
#define NETWORK_INTERFACE_TEST_PEER_ADDRESS   0x0A000002u /**< 10.0.0.2, the kernel's address facing a Linux peer. */
#define NETWORK_INTERFACE_TEST_LIFETIME       1000000u    /**< A cache lifetime no test outlives, in ticks. */
#define NETWORK_INTERFACE_TEST_MICROSECONDS   2000000u    /**< A frame's way back, past QEMU's 1 s after RCTL. */
#define NETWORK_INTERFACE_TEST_PEER_SECONDS   30u         /**< How long the peer test answers. */
#define NETWORK_INTERFACE_TEST_PEER_PINGS     5u          /**< Echo requests the peer sends, and the test waits for. */
#define NETWORK_INTERFACE_TEST_ECHO_BYTES     16u         /**< An echo request's bytes: its header and 8 of data. */
#define NETWORK_INTERFACE_TEST_INTERRUPT_FLAG (1u << 9)   /**< EFLAGS.IF. */

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
                                                                 NETWORK_INTERFACE_TEST_LIFETIME),
                             "the interface sets up on the e1000");
}

/**
 * @brief Sends an echo request of 16 bytes from the interface to itself, written as the test lays it out.
 */
static bool network_interface_test_send_echo_to_itself(void)
{
    KernelNetworkDevice_t *const device = network_interface_test.device;
    KernelNetworkQueue_t *const queue = &device->queues[0];
    const uint32_t length =
        KERNEL_ETHERNET_HEADER_BYTES + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES + NETWORK_INTERFACE_TEST_ECHO_BYTES;
    uint32_t slot = 0u;

    (void) device->operations->kick(device, 0u);
    if (kernel_spsc_ring_reserve(queue->transmit, 1u, &slot) == 0u)
        return false;

    uint8_t *const frame = kernel_network_transmit_buffer(device, queue, slot);
    uint8_t *const echo = frame + KERNEL_ETHERNET_HEADER_BYTES + KERNEL_INTERNET_PROTOCOL_HEADER_BYTES;

    kernel_ethernet_write_header(frame, device->address, device->address, KERNEL_ETHERNET_TYPE_IPV4);
    kernel_internet_protocol_write_header(frame + KERNEL_ETHERNET_HEADER_BYTES, network_interface_test.address,
                                          network_interface_test.address, KERNEL_INTERNET_PROTOCOL_ICMP,
                                          NETWORK_INTERFACE_TEST_ECHO_BYTES, 1u);
    memset(echo, 0x5A, NETWORK_INTERFACE_TEST_ECHO_BYTES);
    echo[0] = KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST;
    echo[1] = 0u;
    kernel_network_write_16(echo + 2u, 0u);
    kernel_network_write_16(echo + 2u, kernel_internet_checksum_finish(
                                           kernel_internet_checksum_add(0u, echo, NETWORK_INTERFACE_TEST_ECHO_BYTES)));
    if (!kernel_network_transmit_describe(device, queue, slot, length))
        return false;
    kernel_spsc_ring_publish(queue->transmit, 1u);
    return device->operations->kick(device, 0u) == 1u;
}

KERNEL_TEST_SUITE(network_interface, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief Through the loopback, the interface answers its own ARP request and learns its own address from the reply.
 *
 * @details The request is sent again until one comes back: QEMU drops looped-back frames for 1 s after the
 *          driver's last write of RCTL.
 */
KERNEL_TEST(the_interface_answers_an_arp_request_and_learns_the_reply)
{
    if (!network_interface_test_bring_up(test, NETWORK_INTERFACE_TEST_ADDRESS, true))
        return;

    const KernelNetworkInterfaceCounters_t *const counters = &network_interface_test.counters;
    InterruptRequestDeadline_t deadline;
    uint8_t station[KERNEL_NETWORK_ADDRESS_BYTES] = {0u};
    uint32_t now = 0u;

    interrupt_request_deadline_start(&deadline, NETWORK_INTERFACE_TEST_MICROSECONDS);
    while (counters->address_resolution_replies_learned == 0u && !interrupt_request_deadline_passed(&deadline))
    {
        (void) kernel_network_interface_request_address(&network_interface_test, NETWORK_INTERFACE_TEST_ADDRESS);
        for (uint32_t round = 0u; round < 8u; ++round)
        {
            (void) kernel_network_interface_poll(&network_interface_test, ++now);
            network_interface_test_wait_a_little();
        }
    }

    kernel_test_check(test, counters->address_resolution_requests_answered != 0u,
                      "the interface answers the ARP request for its own address");
    kernel_test_check(test, counters->address_resolution_replies_learned != 0u, "and learns from the reply");
    kernel_test_check(
        test,
        kernel_network_interface_find_station(&network_interface_test, NETWORK_INTERFACE_TEST_ADDRESS, now, station) &&
            memcmp(station, e1000_state()->address, KERNEL_NETWORK_ADDRESS_BYTES) == 0,
        "so that its own address resolves to the e1000's station address");
    kernel_test_check(test, counters->frames_for_others == 0u && counters->frames_of_unknown_type == 0u,
                      "and no frame was dropped on the way");
    kernel_test_measure(test, "requests_sent", counters->address_resolution_requests_sent);
}

/**
 * @brief Through the loopback, the interface answers an echo request sent to itself, and sees its own reply.
 */
KERNEL_TEST(the_interface_answers_an_echo_request)
{
    if (!network_interface_test_bring_up(test, NETWORK_INTERFACE_TEST_ADDRESS, true))
        return;

    const KernelNetworkInterfaceCounters_t *const counters = &network_interface_test.counters;
    InterruptRequestDeadline_t deadline;
    uint32_t now = 0u;

    interrupt_request_deadline_start(&deadline, NETWORK_INTERFACE_TEST_MICROSECONDS);
    while (counters->echo_replies_received == 0u && !interrupt_request_deadline_passed(&deadline))
    {
        (void) network_interface_test_send_echo_to_itself();
        for (uint32_t round = 0u; round < 8u; ++round)
        {
            (void) kernel_network_interface_poll(&network_interface_test, ++now);
            network_interface_test_wait_a_little();
        }
    }

    kernel_test_check(test, counters->echo_requests_answered != 0u, "the interface answers the echo request");
    kernel_test_check(test, counters->echo_replies_received != 0u, "and its reply comes back as an echo reply");
    kernel_test_check(test,
                      counters->control_messages_ignored == 0u &&
                          counters->internet_protocol_refused[KERNEL_INTERNET_PROTOCOL_BAD_CHECKSUM] == 0u,
                      "with no message refused on the way");
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
        if (!answered && counters->echo_requests_answered >= NETWORK_INTERFACE_TEST_PEER_PINGS)
        {
            answered = true;
            interrupt_request_deadline_start(&grace, 1000000u);
        }
        if (answered && interrupt_request_deadline_passed(&grace))
            break;
        network_interface_test_wait_a_little();
    }

    kernel_test_check(test, counters->address_resolution_requests_answered != 0u,
                      "the peer's ARP request for 10.0.0.2 is answered");
    kernel_test_check(test, counters->echo_requests_answered >= NETWORK_INTERFACE_TEST_PEER_PINGS,
                      "and its five echo requests too");
    kernel_test_measure(test, "frames_received", counters->frames_received);
    kernel_test_measure(test, "address_resolution_requests_answered", counters->address_resolution_requests_answered);
    kernel_test_measure(test, "echo_requests_answered", counters->echo_requests_answered);
    kernel_test_measure(test, "frames_for_others", counters->frames_for_others);
    kernel_test_measure(test, "frames_of_unknown_type", counters->frames_of_unknown_type);
}
