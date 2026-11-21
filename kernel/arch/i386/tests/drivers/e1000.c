#include <kernel/cpu/irq.h>
#include <kernel/cpu/pci.h>
#include <kernel/drivers/e1000.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <string.h>

/** Vendor ID of Intel. */
#define E1000_TEST_VENDOR 0x8086u

/** EtherType of the test frames: IEEE 802's Local Experimental Ethertype 1, which no stack answers. */
#define E1000_TEST_ETHER_TYPE 0x88B5u

/** Where a test frame keeps its sequence number, little-endian, right after the header. */
#define E1000_TEST_SEQUENCE_OFFSET 14u

/** Where a test frame's pattern starts. */
#define E1000_TEST_PATTERN_OFFSET 18u

/** Shortest test frame: Ethernet's minimum without its CRC, so that no padding changes a length. */
#define E1000_TEST_FRAME_LEAST_BYTES 60u

/** Frames the order test sends: more than three trips around the receive ring. */
#define E1000_TEST_FRAMES 200u

/** Frames one publish of the order test sends. */
#define E1000_TEST_BATCH 16u

/** Most frames the order test lets wait in the receive ring, below the 63 it holds. */
#define E1000_TEST_MOST_IN_FLIGHT 48u

/** Frames the full-ring test sends before it reads any. */
#define E1000_TEST_OVERFILL 80u

/** Sequence number of the frame the full-ring test sends once it released the ring. */
#define E1000_TEST_AFTER_RELEASE_SEQUENCE 1000u

/** Sequence number of the frames that probe whether the loopback carries frames yet. */
#define E1000_TEST_PROBE_SEQUENCE 0xFFFFFFFFu

/** How long a frame may take to come back: QEMU drops looped-back frames for 1 s after each write of RCTL. */
#define E1000_TEST_LOOPBACK_MICROSECONDS 2000000u

/** How long the probe waits for one probe frame before it sends the next. */
#define E1000_TEST_PROBE_MICROSECONDS 10000u

/** How long a test waits for a frame it expects never to come back. */
#define E1000_TEST_SILENCE_MICROSECONDS 20000u

/** Statistics registers E1000Counters_t adds on each call: MPC, RNBC, CRCERRS, GPRC and GPTC. */
#define E1000_TEST_STATISTICS_REGISTERS 5u

/** Most peeks a drain makes before it gives up, so that a driver that never runs dry fails a claim instead of hanging.
 */
#define E1000_TEST_DRAIN_ROUNDS 8u

/** EFLAGS.IF. */
#define E1000_TEST_INTERRUPT_FLAG (1u << 9)

/** The receive descriptor status bits a sound frame carries: DD and EOP. */
#define E1000_TEST_DONE_AND_END_OF_PACKET 0x03u

/**
 * @brief PCI device IDs of the 8254x copper parts this driver drives, written apart from its own table:
 *        the 82540EM and EP and the 82545EM and GM, from Linux's e1000_hw.h.
 */
static const uint16_t e1000_test_device_identifiers[] = {0x100Eu, 0x1015u, 0x1016u, 0x1017u, 0x101Eu, 0x100Fu, 0x1026u};

static bool e1000_test_loopback_carries_frames = false;
static E1000ReceivedFrame_t e1000_test_frames[E1000_TEST_OVERFILL];

/**
 * @brief The first 8254x the bus lists, found without the driver.
 */
static const PeripheralComponentInterconnectDevice_t *e1000_test_bus_controller(void)
{
    const uint32_t device_count = peripheral_component_interconnect_get_device_count();

    for (uint32_t index = 0u; index < device_count; ++index)
    {
        const PeripheralComponentInterconnectDevice_t *const device =
            peripheral_component_interconnect_get_device(index);

        if (device == NULL || device->vendor_id != E1000_TEST_VENDOR)
            continue;
        for (uint32_t model = 0u; model < sizeof(e1000_test_device_identifiers) / sizeof(uint16_t); ++model)
            if (device->device_id == e1000_test_device_identifiers[model])
                return device;
    }
    return NULL;
}

/**
 * @brief Bytes of the test frame numbered @p sequence: the shortest, the longest, then lengths in between.
 */
static uint16_t e1000_test_frame_length(uint32_t sequence)
{
    if (sequence % 4u == 0u)
        return E1000_TEST_FRAME_LEAST_BYTES;
    if (sequence % 4u == 1u)
        return E1000_FRAME_MOST_BYTES;
    return (uint16_t) (E1000_TEST_FRAME_LEAST_BYTES +
                       (sequence * 151u) % (E1000_FRAME_MOST_BYTES - E1000_TEST_FRAME_LEAST_BYTES + 1u));
}

/**
 * @brief The byte the test frame numbered @p sequence carries at @p offset of its pattern.
 */
static uint8_t e1000_test_pattern_byte(uint32_t sequence, uint32_t offset)
{
    return (uint8_t) (sequence * 31u + offset * 7u + 1u);
}

/**
 * @brief Writes the test frame numbered @p sequence, addressed from and to the controller itself.
 *
 * @return Its length.
 */
static uint16_t e1000_test_write_frame(uint8_t *buffer, uint32_t sequence)
{
    const uint8_t *const address = e1000_state()->address;
    const uint16_t length = e1000_test_frame_length(sequence);

    memcpy(buffer, address, E1000_ADDRESS_BYTES);
    memcpy(buffer + E1000_ADDRESS_BYTES, address, E1000_ADDRESS_BYTES);
    buffer[12] = (uint8_t) (E1000_TEST_ETHER_TYPE >> 8);
    buffer[13] = (uint8_t) E1000_TEST_ETHER_TYPE;
    for (uint32_t byte = 0u; byte < 4u; ++byte)
        buffer[E1000_TEST_SEQUENCE_OFFSET + byte] = (uint8_t) (sequence >> (8u * byte));
    for (uint32_t offset = E1000_TEST_PATTERN_OFFSET; offset < length; ++offset)
        buffer[offset] = e1000_test_pattern_byte(sequence, offset - E1000_TEST_PATTERN_OFFSET);
    return length;
}

/**
 * @brief The sequence number a received frame carries, or E1000_TEST_PROBE_SEQUENCE when it is too short to carry one.
 */
static uint32_t e1000_test_sequence_of(const E1000ReceivedFrame_t *frame)
{
    uint32_t sequence = 0u;

    if (frame->length < E1000_TEST_PATTERN_OFFSET)
        return E1000_TEST_PROBE_SEQUENCE;
    for (uint32_t byte = 0u; byte < 4u; ++byte)
        sequence |= (uint32_t) frame->bytes[E1000_TEST_SEQUENCE_OFFSET + byte] << (8u * byte);
    return sequence;
}

/**
 * @brief Whether a received frame holds exactly the bytes of the test frame numbered @p sequence.
 */
static bool e1000_test_frame_holds(const E1000ReceivedFrame_t *frame, uint32_t sequence)
{
    const uint8_t *const address = e1000_state()->address;

    if (frame->length != e1000_test_frame_length(sequence) || memcmp(frame->bytes, address, E1000_ADDRESS_BYTES) != 0 ||
        memcmp(frame->bytes + E1000_ADDRESS_BYTES, address, E1000_ADDRESS_BYTES) != 0 ||
        frame->bytes[12] != (uint8_t) (E1000_TEST_ETHER_TYPE >> 8) ||
        frame->bytes[13] != (uint8_t) E1000_TEST_ETHER_TYPE || e1000_test_sequence_of(frame) != sequence)
        return false;
    for (uint32_t offset = E1000_TEST_PATTERN_OFFSET; offset < frame->length; ++offset)
        if (frame->bytes[offset] != e1000_test_pattern_byte(sequence, offset - E1000_TEST_PATTERN_OFFSET))
            return false;
    return true;
}

/**
 * @brief Whether a received frame lies inside the driver's receive buffers, so that it was read in place.
 */
static bool e1000_test_frame_lies_in_the_receive_buffers(const E1000ReceivedFrame_t *frame)
{
    const E1000State_t *const state = e1000_state();
    const uintptr_t first = (uintptr_t) state->receive_buffers;
    const uintptr_t start = (uintptr_t) frame->bytes;

    return state->receive_buffers != NULL && start >= first &&
           start + frame->length <= first + state->receive_buffers_bytes;
}

/**
 * @brief Waits for the next interrupt when interrupts are on, or spins once when they are off.
 */
static void e1000_test_wait_a_little(void)
{
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();

    asmutils_restore_flags(flags);
    if ((flags & E1000_TEST_INTERRUPT_FLAG) != 0u)
        asmutils_halt();
    else
        asmutils_pause();
}

/**
 * @brief Sends the test frames numbered @p first_sequence on, at most @p count of them, in one publish.
 *
 * @return How many were sent: fewer when the transmit ring had fewer buffers free.
 */
static uint32_t e1000_test_send(uint32_t first_sequence, uint32_t count)
{
    uint8_t *buffers[E1000_TRANSMIT_DESCRIPTORS];
    uint16_t lengths[E1000_TRANSMIT_DESCRIPTORS];
    const uint32_t wanted = count < E1000_TRANSMIT_DESCRIPTORS ? count : E1000_TRANSMIT_DESCRIPTORS;
    const uint32_t reserved = e1000_transmit_reserve(buffers, wanted);

    for (uint32_t frame = 0u; frame < reserved; ++frame)
        lengths[frame] = e1000_test_write_frame(buffers[frame], first_sequence + frame);
    return e1000_transmit_publish(lengths, reserved) ? reserved : 0u;
}

/**
 * @brief Sends the test frames numbered @p first_sequence to @p first_sequence + @p count - 1,
 *        publish after publish, within @p deadline.
 *
 * @return How many were sent.
 */
static uint32_t e1000_test_send_all(uint32_t first_sequence, uint32_t count, InterruptRequestDeadline_t *deadline)
{
    uint32_t sent = 0u;

    while (sent < count && !interrupt_request_deadline_passed(deadline))
    {
        const uint32_t batch = count - sent < E1000_TEST_BATCH ? count - sent : E1000_TEST_BATCH;
        const uint32_t published = e1000_test_send(first_sequence + sent, batch);

        sent += published;
        if (published == 0u)
            e1000_test_wait_a_little();
    }
    return sent;
}

/**
 * @brief Peeks until at least @p least frames are waiting, or @p deadline passes.
 *
 * @return How many frames the last peek handed out, at most E1000_TEST_OVERFILL.
 */
static uint32_t e1000_test_peek_at_least(uint32_t least, InterruptRequestDeadline_t *deadline)
{
    uint32_t peeked = e1000_receive_peek(e1000_test_frames, E1000_TEST_OVERFILL);

    while (peeked < least && !interrupt_request_deadline_passed(deadline))
    {
        e1000_test_wait_a_little();
        peeked = e1000_receive_peek(e1000_test_frames, E1000_TEST_OVERFILL);
    }
    return peeked;
}

/**
 * @brief Releases every frame waiting, until none is left.
 */
static void e1000_test_drain(void)
{
    uint32_t peeked = e1000_receive_peek(e1000_test_frames, E1000_TEST_OVERFILL);

    for (uint32_t round = 0u; peeked != 0u && round < E1000_TEST_DRAIN_ROUNDS; ++round)
    {
        (void) e1000_receive_release(peeked);
        peeked = e1000_receive_peek(e1000_test_frames, E1000_TEST_OVERFILL);
    }
}

/**
 * @brief Sends probe frames until one comes back, within E1000_TEST_LOOPBACK_MICROSECONDS.
 *
 * @details QEMU drops a looped-back frame for 1 s after each write of RCTL, without counting it
 *          (hw/net/e1000.c, set_rx_control and e1000_can_receive; net/net.c, qemu_receive_packet),
 *          where the controller itself would take it: the tests send their frames once a probe made it.
 */
static bool e1000_test_wait_for_the_loopback(void)
{
    InterruptRequestDeadline_t deadline;

    interrupt_request_deadline_start(&deadline, E1000_TEST_LOOPBACK_MICROSECONDS);
    while (!interrupt_request_deadline_passed(&deadline))
    {
        InterruptRequestDeadline_t probe;

        (void) e1000_test_send(E1000_TEST_PROBE_SEQUENCE, 1u);
        interrupt_request_deadline_start(&probe, E1000_TEST_PROBE_MICROSECONDS);
        if (e1000_test_peek_at_least(1u, &probe) != 0u)
        {
            e1000_test_drain();
            return true;
        }
    }
    return false;
}

/**
 * @brief Brings the controller up unless an earlier test left it ready, turns the loopback on, and
 *        waits until it carries frames.
 *
 * @return true when the test may send; false when it skipped or failed.
 */
static bool e1000_test_bring_up_in_loopback(KernelTest_t *test)
{
    if (e1000_test_bus_controller() == NULL)
    {
        kernel_test_skip(test, "no 8254x on the bus");
        return false;
    }
    if (!e1000_state()->controller_ready)
    {
        e1000_test_loopback_carries_frames = false;
        if (!kernel_test_check(test, e1000_initialize(), "the controller the bus lists comes out of reset"))
            return false;
    }
    if (!e1000_state()->loopback)
    {
        e1000_test_loopback_carries_frames = false;
        if (!kernel_test_check(test, e1000_set_loopback(true), "its PHY takes the loopback"))
            return false;
    }
    if (!e1000_test_loopback_carries_frames)
        e1000_test_loopback_carries_frames = e1000_test_wait_for_the_loopback();
    e1000_test_drain();
    return kernel_test_check(test, e1000_test_loopback_carries_frames, "a frame sent comes back through the loopback");
}

KERNEL_TEST_SUITE(e1000, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief The station address decodes from EEPROM words 00h to 02h as the manual's example stores it.
 */
KERNEL_TEST(the_station_address_decodes_as_the_manual_stores_it)
{
    static const uint16_t words[E1000_EEPROM_ADDRESS_WORDS] = {0x3412u, 0x7856u, 0xAB90u};
    static const uint8_t expected[E1000_ADDRESS_BYTES] = {0x12u, 0x34u, 0x56u, 0x78u, 0x90u, 0xABu};
    uint8_t address[E1000_ADDRESS_BYTES] = {0u};

    e1000_station_address_from_eeprom(words, address);
    kernel_test_check(test, memcmp(address, expected, E1000_ADDRESS_BYTES) == 0,
                      "words 3412h, 7856h and AB90h hold the address 12-34-56-78-90-AB (317453-006EN, 5.6.1)");
}

/**
 * @brief A receive descriptor's last dword decodes into its status and errors bytes as the manual lays it out.
 */
KERNEL_TEST(the_receive_status_decodes_as_the_descriptor_lays_it_out)
{
    E1000ReceivedFrame_t frame = {NULL, 0u, 0u, 0u};

    e1000_decode_receive_status(0xBEEF8603u, &frame);
    kernel_test_check(test, frame.status == 0x03u && frame.errors == 0x86u,
                      "the dword BEEF8603h holds the status 03h and the errors 86h (317453-006EN, Table 3-1)");
}

/**
 * @brief The driver finds the 8254x the bus lists, brings it up, and reads its station address.
 *
 * @details Skipped where the bus lists no 8254x, as with `-nic none`; QEMU's `pc` machine attaches an 82540EM.
 */
KERNEL_TEST(the_driver_brings_up_the_controller_the_bus_lists)
{
    const PeripheralComponentInterconnectDevice_t *const device = e1000_test_bus_controller();

    if (device == NULL)
    {
        kernel_test_skip(test, "no 8254x on the bus");
        return;
    }

    e1000_test_loopback_carries_frames = false;
    const bool ready = e1000_initialize();
    const E1000State_t *const state = e1000_state();
    const E1000Counters_t *const counters = e1000_counters();
    static const uint8_t no_address[E1000_ADDRESS_BYTES] = {0u};

    kernel_test_check(test, state->controller_present, "the driver finds the 8254x the bus lists");
    kernel_test_check(test, state->device_identifier == device->device_id, "and gives its device ID");
    kernel_test_check(test, ready && state->controller_ready, "it comes out of reset with both rings");
    kernel_test_check(test, (state->address[0] & 0x01u) == 0u, "its station address is a unicast address");
    kernel_test_check(test, memcmp(state->address, no_address, E1000_ADDRESS_BYTES) != 0, "and not all zeros");
    kernel_test_check(test, !state->loopback, "the bring-up leaves the PHY out of loopback");
    kernel_test_check(test,
                      state->receive_buffers != NULL &&
                          state->receive_buffers_bytes == E1000_RECEIVE_DESCRIPTORS * E1000_BUFFER_BYTES,
                      "each receive descriptor has a buffer of its own");
    kernel_test_check(test,
                      counters->receive_doorbells == 0u && counters->transmit_doorbells == 0u &&
                          counters->frames_received == 0u && counters->frames_sent == 0u,
                      "and the counters start from zero");
    if (state->failure != NULL)
        kernel_test_note(test, state->failure);

    kernel_test_measure_hexadecimal(test, "device_identifier", state->device_identifier);
    kernel_test_measure_hexadecimal(test, "address_high",
                                    (uint32_t) state->address[0] << 8 | (uint32_t) state->address[1]);
    kernel_test_measure_hexadecimal(test, "address_low",
                                    (uint32_t) state->address[2] << 24 | (uint32_t) state->address[3] << 16 |
                                        (uint32_t) state->address[4] << 8 | (uint32_t) state->address[5]);
    kernel_test_measure(test, "link_up", state->link_up ? 1u : 0u);
    kernel_test_measure(test, "bring_up_register_reads", counters->register_reads);
    kernel_test_measure(test, "bring_up_register_writes", counters->register_writes);
}

/**
 * @brief 200 frames sent through the loopback come back in order, byte for byte, where the controller wrote them.
 *
 * @details The frames go in publishes of 16, with at most 48 waiting in the receive ring, and take every
 *          length from 60 to 1514 bytes. Each publish and each release writes its tail once, and the
 *          controller counts as many frames as the driver.
 */
KERNEL_TEST(frames_sent_come_back_in_order_through_the_loopback)
{
    if (!e1000_test_bring_up_in_loopback(test))
        return;

    const E1000Counters_t before = *e1000_counters();
    InterruptRequestDeadline_t deadline;
    uint32_t sent = 0u;
    uint32_t received = 0u;
    uint32_t publishes = 0u;
    uint32_t releases = 0u;
    uint32_t in_order = 0u;
    uint32_t holding_their_bytes = 0u;
    uint32_t in_place = 0u;
    uint32_t sound = 0u;

    interrupt_request_deadline_start(&deadline, E1000_TEST_LOOPBACK_MICROSECONDS);
    while (received < E1000_TEST_FRAMES && !interrupt_request_deadline_passed(&deadline))
    {
        if (sent < E1000_TEST_FRAMES && sent - received + E1000_TEST_BATCH <= E1000_TEST_MOST_IN_FLIGHT)
        {
            const uint32_t batch =
                E1000_TEST_FRAMES - sent < E1000_TEST_BATCH ? E1000_TEST_FRAMES - sent : E1000_TEST_BATCH;
            const uint32_t published = e1000_test_send(sent, batch);

            sent += published;
            publishes += published != 0u ? 1u : 0u;
        }

        const uint32_t peeked = e1000_receive_peek(e1000_test_frames, E1000_TEST_OVERFILL);

        for (uint32_t frame = 0u; frame < peeked; ++frame)
        {
            const E1000ReceivedFrame_t *const received_frame = &e1000_test_frames[frame];

            in_order += e1000_test_sequence_of(received_frame) == received + frame ? 1u : 0u;
            holding_their_bytes += e1000_test_frame_holds(received_frame, received + frame) ? 1u : 0u;
            in_place += e1000_test_frame_lies_in_the_receive_buffers(received_frame) ? 1u : 0u;
            sound +=
                (received_frame->status & E1000_TEST_DONE_AND_END_OF_PACKET) == E1000_TEST_DONE_AND_END_OF_PACKET &&
                        received_frame->errors == 0u ?
                    1u :
                    0u;
        }
        if (peeked == 0u)
        {
            e1000_test_wait_a_little();
            continue;
        }
        releases += e1000_receive_release(peeked) ? 1u : 0u;
        received += peeked;
    }

    const E1000Counters_t *const after = e1000_counters();
    const uint32_t register_accesses =
        (after->register_reads - before.register_reads) + (after->register_writes - before.register_writes);

    kernel_test_check(test, sent == E1000_TEST_FRAMES, "the 200 frames are sent");
    kernel_test_check(test, received == E1000_TEST_FRAMES, "and the 200 come back");
    kernel_test_check(test, in_order == received, "in the order they were sent");
    kernel_test_check(test, holding_their_bytes == received, "each holding the bytes it was sent with");
    kernel_test_check(test, in_place == received, "each read where the controller wrote it, in the receive buffers");
    kernel_test_check(test, sound == received, "each with DD and EOP set and no error");
    kernel_test_check(test, after->transmit_doorbells - before.transmit_doorbells == publishes,
                      "each publish writes the transmit tail once");
    kernel_test_check(test, after->receive_doorbells - before.receive_doorbells == releases,
                      "each release writes the receive tail once");
    kernel_test_check(test,
                      after->frames_sent - before.frames_sent == E1000_TEST_FRAMES &&
                          after->frames_received - before.frames_received == E1000_TEST_FRAMES,
                      "the driver counts 200 frames each way");
    kernel_test_check(test,
                      after->good_transmitted - before.good_transmitted == E1000_TEST_FRAMES &&
                          after->good_received - before.good_received == E1000_TEST_FRAMES,
                      "and so does the controller");
    kernel_test_check(test,
                      after->register_writes - before.register_writes ==
                          (after->transmit_doorbells - before.transmit_doorbells) +
                              (after->receive_doorbells - before.receive_doorbells),
                      "the frames' path writes no register but the two tails");
    kernel_test_check(test, after->register_reads - before.register_reads == E1000_TEST_STATISTICS_REGISTERS,
                      "and reads none: the only reads are the counters' own");

    kernel_test_measure(test, "publishes", publishes);
    kernel_test_measure(test, "releases", releases);
    kernel_test_measure(test, "register_accesses", register_accesses);
    kernel_test_measure(test, "register_accesses_per_1000_frames", register_accesses * 1000u / E1000_TEST_FRAMES);
}

/**
 * @brief Frames sent while nothing is read fill the receive ring to one frame fewer than its
 *        descriptors, and a frame sent once they are released comes back.
 *
 * @details The 17 frames sent past what the ring holds are lost: QEMU drops them before the controller
 *          sees them, uncounted, where a controller keeps them in its FIFO or counts them in MPC. So the
 *          frames read after the release are only required to be among those sent, in order.
 */
KERNEL_TEST(the_receive_ring_holds_one_frame_fewer_than_its_descriptors)
{
    if (!e1000_test_bring_up_in_loopback(test))
        return;

    const E1000Counters_t before = *e1000_counters();
    InterruptRequestDeadline_t deadline;
    uint32_t in_order = 0u;

    interrupt_request_deadline_start(&deadline, E1000_TEST_LOOPBACK_MICROSECONDS);
    const uint32_t sent = e1000_test_send_all(0u, E1000_TEST_OVERFILL, &deadline);
    const uint32_t held = e1000_test_peek_at_least(E1000_RECEIVE_DESCRIPTORS - 1u, &deadline);

    for (uint32_t frame = 0u; frame < held; ++frame)
        in_order += e1000_test_frame_holds(&e1000_test_frames[frame], frame) ? 1u : 0u;
    kernel_test_check(test, sent == E1000_TEST_OVERFILL, "the 80 frames are sent");
    kernel_test_check(test, held == E1000_RECEIVE_DESCRIPTORS - 1u,
                      "the receive ring holds 63 of them, one fewer than its 64 descriptors");
    kernel_test_check(test, in_order == held, "the first ones sent, in order");
    kernel_test_check(test, e1000_receive_release(held), "and gives them all back");

    uint32_t last_sequence = held - 1u;
    uint32_t increasing = 0u;
    uint32_t after_the_overfill = 0u;
    bool came_back = false;

    (void) e1000_test_send(E1000_TEST_AFTER_RELEASE_SEQUENCE, 1u);
    while (!came_back && !interrupt_request_deadline_passed(&deadline))
    {
        const uint32_t peeked = e1000_test_peek_at_least(1u, &deadline);

        for (uint32_t frame = 0u; frame < peeked; ++frame)
        {
            const uint32_t sequence = e1000_test_sequence_of(&e1000_test_frames[frame]);

            increasing += sequence > last_sequence ? 1u : 0u;
            after_the_overfill += sequence < E1000_TEST_OVERFILL ? 1u : 0u;
            came_back = came_back || sequence == E1000_TEST_AFTER_RELEASE_SEQUENCE;
            last_sequence = sequence;
        }
        (void) e1000_receive_release(peeked);
        if (!came_back)
            kernel_test_check(test, peeked != 0u, "frames keep coming until the one sent after the release");
        if (peeked == 0u)
            break;
    }

    const E1000Counters_t *const after = e1000_counters();

    kernel_test_check(test, came_back, "the frame sent after the release comes back");
    kernel_test_check(test, increasing == after_the_overfill + (came_back ? 1u : 0u),
                      "after the frames the controller kept, in the order they were sent");
    kernel_test_measure(test, "frames_kept_past_the_ring", after_the_overfill);
    kernel_test_measure(test, "missed", (uint32_t) (after->missed - before.missed));
    kernel_test_measure(test, "no_buffers", (uint32_t) (after->no_buffers - before.no_buffers));
}

/**
 * @brief A reserve hands a transmit buffer out again only once the controller reported its publish done,
 *        and never the last free descriptor.
 *
 * @details QEMU sends a publish inside the write of its tail, so the test holds the tail back to see a
 *          controller that has not read the descriptors yet, as a real one has not for a while.
 */
KERNEL_TEST(a_reserve_waits_for_the_controller_to_report_a_publish_done)
{
    if (!e1000_test_bring_up_in_loopback(test))
        return;

    uint8_t *buffers[E1000_TRANSMIT_DESCRIPTORS];
    uint16_t lengths[E1000_TRANSMIT_DESCRIPTORS];
    InterruptRequestDeadline_t deadline;
    uint32_t in_order = 0u;
    uint32_t once_done = 0u;

    e1000_debug_hold_transmit_tail(true);
    const uint32_t reserved = e1000_transmit_reserve(buffers, E1000_TRANSMIT_DESCRIPTORS);

    for (uint32_t frame = 0u; frame < reserved; ++frame)
        lengths[frame] = e1000_test_write_frame(buffers[frame], frame);

    const bool published = e1000_transmit_publish(lengths, reserved);
    const uint32_t while_unread = e1000_transmit_reserve(buffers, 1u);

    e1000_debug_hold_transmit_tail(false);
    interrupt_request_deadline_start(&deadline, E1000_TEST_LOOPBACK_MICROSECONDS);

    const uint32_t came_back = e1000_test_peek_at_least(reserved, &deadline);

    for (uint32_t frame = 0u; frame < came_back; ++frame)
        in_order += e1000_test_frame_holds(&e1000_test_frames[frame], frame) ? 1u : 0u;
    while (once_done == 0u && !interrupt_request_deadline_passed(&deadline))
    {
        once_done = e1000_transmit_reserve(buffers, 1u);
        if (once_done == 0u)
            e1000_test_wait_a_little();
    }

    kernel_test_check(test, reserved == E1000_TRANSMIT_DESCRIPTORS - 1u,
                      "a reserve hands out 63 buffers, one fewer than the 64 transmit descriptors");
    kernel_test_check(test, published, "and a publish takes the 63");
    kernel_test_check(test, while_unread == 0u,
                      "while the controller has not read them, a reserve hands out none of their buffers");
    kernel_test_check(test, came_back == reserved && in_order == came_back,
                      "once the tail moves, the 63 frames come back in order");
    kernel_test_check(test, once_done == 1u, "and their buffers are handed out again");
    (void) e1000_receive_release(came_back);
    e1000_test_drain();
}

/**
 * @brief A release of more than was peeked, and a publish of more than was reserved or of a length out
 *        of range, are refused without touching the controller; a release or publish of none writes nothing.
 */
KERNEL_TEST(a_release_or_a_publish_it_cannot_honour_is_refused)
{
    if (!e1000_test_bring_up_in_loopback(test))
        return;

    const E1000Counters_t before = *e1000_counters();
    uint8_t *buffers[2];
    const uint16_t too_long[2] = {E1000_TEST_FRAME_LEAST_BYTES, E1000_FRAME_MOST_BYTES + 1u};
    const uint16_t too_short[2] = {E1000_FRAME_LEAST_BYTES - 1u, E1000_TEST_FRAME_LEAST_BYTES};
    const uint16_t three[3] = {E1000_TEST_FRAME_LEAST_BYTES, E1000_TEST_FRAME_LEAST_BYTES,
                               E1000_TEST_FRAME_LEAST_BYTES};
    const uint32_t peeked = e1000_receive_peek(e1000_test_frames, E1000_TEST_OVERFILL);

    kernel_test_check(test, !e1000_receive_release(peeked + 1u),
                      "a release of one frame more than was peeked is refused");
    kernel_test_check(test, e1000_receive_release(0u), "a release of none is taken");
    kernel_test_check(test, e1000_transmit_reserve(buffers, 2u) == 2u, "a reserve of two buffers is served");
    kernel_test_check(test, !e1000_transmit_publish(too_long, 2u), "a publish of a frame over 1514 bytes is refused");
    kernel_test_check(test, !e1000_transmit_publish(too_short, 2u),
                      "a publish of a frame shorter than its header is refused");
    kernel_test_check(test, !e1000_transmit_publish(three, 3u),
                      "a publish of more frames than were reserved is refused");
    kernel_test_check(test, e1000_transmit_publish(three, 0u), "a publish of none is taken");

    const E1000Counters_t *const refused = e1000_counters();

    kernel_test_check(test, refused->refused - before.refused == 4u, "the four refusals are counted");
    kernel_test_check(test,
                      refused->transmit_doorbells == before.transmit_doorbells &&
                          refused->receive_doorbells == before.receive_doorbells &&
                          refused->frames_sent == before.frames_sent,
                      "and none of the six calls wrote a tail or sent a frame");

    InterruptRequestDeadline_t deadline;
    uint16_t lengths[2];

    lengths[0] = e1000_test_write_frame(buffers[0], 0u);
    lengths[1] = e1000_test_write_frame(buffers[1], 1u);
    kernel_test_check(test, e1000_transmit_publish(lengths, 2u),
                      "the reserve survives the refusals, and a publish sends it");
    interrupt_request_deadline_start(&deadline, E1000_TEST_LOOPBACK_MICROSECONDS);
    kernel_test_check(test,
                      e1000_test_peek_at_least(2u, &deadline) == 2u &&
                          e1000_test_frame_holds(&e1000_test_frames[0], 0u) &&
                          e1000_test_frame_holds(&e1000_test_frames[1], 1u),
                      "and its two frames come back");
    e1000_test_drain();
}

/**
 * @brief With the loopback turned off, a frame the controller sends to its own address does not come back.
 */
KERNEL_TEST(without_the_loopback_a_frame_sent_does_not_come_back)
{
    if (!e1000_test_bring_up_in_loopback(test))
        return;

    InterruptRequestDeadline_t silence;

    kernel_test_check(test, e1000_set_loopback(false), "the PHY leaves the loopback");
    kernel_test_check(test, !e1000_state()->loopback, "and the state says so");
    e1000_test_loopback_carries_frames = false;
    kernel_test_check(test, e1000_test_send(7u, 1u) == 1u, "a frame to the controller's own address is sent");
    interrupt_request_deadline_start(&silence, E1000_TEST_SILENCE_MICROSECONDS);
    kernel_test_check(test, e1000_test_peek_at_least(1u, &silence) == 0u, "and it does not come back");
    e1000_test_drain();
}

/**
 * @brief A second bring-up drops the frames not released and starts both rings and the counters over.
 */
KERNEL_TEST(a_second_bring_up_starts_both_rings_over)
{
    if (!e1000_test_bring_up_in_loopback(test))
        return;

    InterruptRequestDeadline_t deadline;

    interrupt_request_deadline_start(&deadline, E1000_TEST_LOOPBACK_MICROSECONDS);
    kernel_test_check(test, e1000_test_send_all(0u, 3u, &deadline) == 3u, "three frames are sent");
    kernel_test_check(test, e1000_test_peek_at_least(3u, &deadline) == 3u, "and wait in the receive ring");

    e1000_test_loopback_carries_frames = false;
    const bool ready = e1000_initialize();
    const E1000Counters_t *const counters = e1000_counters();

    kernel_test_check(test, ready && e1000_state()->controller_ready, "a second bring-up is ready");
    kernel_test_check(test, !e1000_state()->loopback, "and takes the PHY out of loopback");
    kernel_test_check(test, e1000_receive_peek(e1000_test_frames, E1000_TEST_OVERFILL) == 0u,
                      "the frames not released are gone");
    kernel_test_check(test,
                      counters->frames_sent == 0u && counters->frames_received == 0u &&
                          counters->transmit_doorbells == 0u && counters->receive_doorbells == 0u,
                      "and the counters start from zero again");
    kernel_test_check(test, counters->good_received == 0u && counters->good_transmitted == 0u,
                      "and so do the controller's, which counted the three frames before it");
}
