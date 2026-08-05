/**
 * @file hda.c
 * @brief Intel High Definition Audio controller — capture and playback.
 *
 * The codec QEMU offers with `-device intel-hda`, and the one a development
 * satellite will actually run against. Discovered over the bus enumeration that
 * already exists (class 0x04), so no new probing mechanism is needed.
 *
 * The interrupt handler does what every other driver here does and nothing more:
 * move a buffer into a ring and acknowledge. Ring buffer descriptor lists are set
 * up once at init; a handler that allocated would be a handler that can fail while
 * the sovereign is mid-sentence.
 *
 * Identifiers spell the acronym out — `intel_high_definition_audio_*` — the file
 * name being the documented exemption.
 *
 * WHAT IS VERIFIED. Measured in QEMU with `-device intel-hda -device hda-duplex`, on
 * all five booted artifacts: the controller is found, comes out of reset, reports
 * version 1 with four input and four output stream descriptors, a codec announces
 * itself with vendor identifier 0x1AF40022, all twelve verbs of the walk and the stream
 * setup are answered, the input converter is found at node 4 behind pin 3, and the
 * stream runs and delivers buffers. The delivery rate cross-checks the format word
 * independently: the cyclic buffer is 2560 bytes, which is 80 ms of 16 kHz mono
 * 16-bit audio, and 320 ms of running produced three wraps plus a part-filled fourth.
 *
 * HOW THE COMMAND PATH WAS FIXED, because the shape of the bug is worth keeping. For
 * a while only the FIRST verb was ever answered and every one after it timed out. Two
 * hypotheses were reasoned out from the code and both were wrong — acknowledging
 * RIRBSTS (correct in itself, kept) and a ring-pointer misalignment (imaginary). The
 * third attempt was an instrument instead of a hypothesis: @ref hda_probe_rings, which
 * reads both rings either side of a failing command. It answered immediately. CORBWP
 * was 2, our shadow agreed, the fetch engine was running, CORBSTS showed no error —
 * and CORBRP was frozen at 1. The controller was not failing to answer, it was
 * refusing to FETCH, and only one setting does that: RINTCNT, the response count a
 * controller insists on having acknowledged before it reads more. It was set to one,
 * which for a driver that polls and takes no interrupts is the worst possible value.
 * The probe is kept rather than removed — a timeout without register state is a fact
 * with no diagnosis attached, which is exactly how this managed to be wrong twice.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <kernel/drivers/hda.h>

#include <kernel/cpu/paging.h>
#include <kernel/cpu/pci.h>
#include <kernel/cpu/pmm.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/vmm.h>

#include <stddef.h>

/* ── Controller registers, at offsets from the mapped window ───────────────── */
#define HDA_REG_GLOBAL_CAPABILITIES         0x00u /**< 16-bit: stream descriptor counts. */
#define HDA_REG_MINOR_VERSION               0x02u
#define HDA_REG_MAJOR_VERSION               0x03u
#define HDA_REG_GLOBAL_CONTROL              0x08u /**< 32-bit: bit 0 is controller reset. */
#define HDA_REG_STATE_CHANGE_STATUS         0x0Eu /**< 16-bit: bit per codec that announced itself. */
#define HDA_REG_COMMAND_RING_BASE_LOW       0x40u
#define HDA_REG_COMMAND_RING_BASE_HIGH      0x44u
#define HDA_REG_COMMAND_RING_WRITE_POINTER  0x48u
#define HDA_REG_COMMAND_RING_READ_POINTER   0x4Au
#define HDA_REG_COMMAND_RING_CONTROL        0x4Cu
#define HDA_REG_COMMAND_RING_STATUS         0x4Du /**< 8-bit: bit 0 is a memory error on fetch. */
#define HDA_REG_COMMAND_RING_SIZE           0x4Eu
#define HDA_REG_RESPONSE_RING_BASE_LOW      0x50u
#define HDA_REG_RESPONSE_RING_BASE_HIGH     0x54u
#define HDA_REG_RESPONSE_RING_WRITE_POINTER 0x58u
#define HDA_REG_RESPONSE_INTERRUPT_COUNT    0x5Au
#define HDA_REG_RESPONSE_RING_CONTROL       0x5Cu
#define HDA_REG_RESPONSE_RING_STATUS        0x5Du
#define HDA_REG_RESPONSE_RING_SIZE          0x5Eu

/** Global control bit 0: low holds the controller in reset, high releases it. */
#define HDA_GLOBAL_CONTROL_RESET (1u << 0)

/** Command ring control bit 1: start fetching entries. */
#define HDA_COMMAND_RING_RUN (1u << 1)

/** Response ring control bit 1: start writing responses back. */
#define HDA_RESPONSE_RING_RUN (1u << 1)

/**
 * Response ring status bits: a response arrived (0) and the ring overran (2).
 *
 * Both are write-one-to-clear and both MUST be cleared, which is the failure this
 * driver was measured making: the first command answered and every one after it timed
 * out. A controller that has raised its response flag and never seen it acknowledged
 * stops writing responses — so the symptom is not "the ring is broken" but "the ring
 * worked exactly once", which reads like a codec problem and is not.
 */
#define HDA_RESPONSE_STATUS_INTERRUPT (1u << 0)
#define HDA_RESPONSE_STATUS_OVERRUN   (1u << 2)

/** Read-pointer reset bit, acknowledged by readback on the way in and on the way out. */
#define HDA_RING_POINTER_RESET (1u << 15)

/** Entries in each ring. 256 is the size every controller implements. */
#define HDA_RING_ENTRIES 256u

/** PCI command register bits: memory space decode and bus mastering. */
#define HDA_PCI_COMMAND_MEMORY_SPACE (1u << 1)
#define HDA_PCI_COMMAND_BUS_MASTER   (1u << 2)

/** Offset of the command register in PCI configuration space. */
#define HDA_PCI_COMMAND_OFFSET 0x04u

/** PCI class and subclass of a High Definition Audio controller. */
#define HDA_PCI_CLASS    0x04u
#define HDA_PCI_SUBCLASS 0x03u

/**
 * Spin budget for anything the controller is supposed to do promptly.
 *
 * Bounded rather than unbounded for the reason every wait in this kernel is: a device
 * in a state nobody anticipated must be reported, not waited for. The specification
 * asks for 521 microseconds after reset before codec announcements are believed; the
 * budget here is counted in loop iterations rather than in time, because at bring-up
 * there is no calibrated clock to trust — and it is generous by orders of magnitude,
 * which is the right direction to be wrong in.
 */
#define HDA_SPIN_BUDGET 500000u

/** Codec verb: read one of the node's parameters. */
#define HDA_VERB_GET_PARAMETER 0xF00u

/** Parameter 0x00: the codec's vendor and device identifier. */
#define HDA_PARAMETER_VENDOR_ID 0x00u

/** Parameter 0x04: first sub-node and how many follow it. */
#define HDA_PARAMETER_SUB_NODE_COUNT 0x04u

/** Parameter 0x09: what kind of widget a node is, and what it can do. */
#define HDA_PARAMETER_WIDGET_CAPABILITIES 0x09u

/** Parameter 0x05: what kind of function group a node is. */
#define HDA_PARAMETER_FUNCTION_GROUP_TYPE 0x05u

/** Function group type 0x01: audio. */
#define HDA_FUNCTION_GROUP_AUDIO 0x01u

/** Widget type 0x0, in bits 23:20 of the capabilities: an output converter. */
#define HDA_WIDGET_TYPE_AUDIO_OUTPUT 0x0u

/** Widget type 0x1: an input converter. */
#define HDA_WIDGET_TYPE_AUDIO_INPUT 0x1u

/** Widget type 0x4: a pin complex, where a converter meets the outside world. */
#define HDA_WIDGET_TYPE_PIN_COMPLEX 0x4u

/**
 * Parameter 0x0C: what a pin complex can do.
 *
 * Needed because a pin's NUMBER says nothing about its direction, and taking the
 * first one the walk meets is how this driver spent a while configuring the codec's
 * OUTPUT pin for input: on the QEMU duplex codec the widgets run output converter,
 * output pin, input converter, input pin, so "first pin complex" is the wrong one.
 * It captured anyway because QEMU is permissive; real hardware would have left the
 * microphone's pin unpowered and delivered silence with every register looking right.
 */
#define HDA_PARAMETER_PIN_CAPABILITIES 0x0Cu

/** Pin capability bit 4: the pin can drive a signal out. */
#define HDA_PIN_CAPABILITY_OUTPUT (1u << 4)

/** Pin capability bit 5: the pin can take a signal in. */
#define HDA_PIN_CAPABILITY_INPUT (1u << 5)

/** Verb 0x2 (four bits): set a converter's sample format. */
#define HDA_VERB_SET_CONVERTER_FORMAT 0x2u

/** Verb 0x3 (four bits): set an amplifier's gain and mute. */
#define HDA_VERB_SET_AMPLIFIER_GAIN 0x3u

/**
 * Amplifier payload that mutes an output, both channels.
 *
 * Bit 15 addresses the output amplifier, 13 and 12 the left and right channels, and
 * bit 7 is the mute. Gain bits are left at zero, so even a codec that ignored the
 * mute bit would be at its quietest setting rather than its loudest.
 *
 * Every output amplifier the walk finds is set to this at bring-up, and nothing in
 * this driver ever clears it. That is deliberate and it is the SECOND of two
 * independent guarantees that this kernel cannot make a development machine emit
 * noise — the first being the emulator's null audio backend, which removes the path
 * to the host's sound device entirely. Two guarantees rather than one because they
 * fail in different ways: a backend is chosen by whoever launches the machine, and a
 * mute is chosen here.
 */
#define HDA_AMPLIFIER_MUTE_OUTPUT 0xB080u

/** Verb 0x706: bind a converter to a stream number and channel. */
#define HDA_VERB_SET_STREAM_CHANNEL 0x706u

/** Verb 0x707: set what a pin complex is doing. */
#define HDA_VERB_SET_PIN_CONTROL 0x707u

/** Pin control bit 5: the pin is an input. */
#define HDA_PIN_CONTROL_INPUT_ENABLE 0x20u

/** Stream number the capture converter is bound to. Any non-zero value will do. */
#define HDA_CAPTURE_STREAM_NUMBER 1u

/**
 * Sample format: 16 kHz, 16-bit, one channel.
 *
 * Bits 10:8 hold a divisor selector where 2 means divide by three, and the base rate
 * is 48 kHz — so 48/3 is the sixteen the satellite protocol asks for. Bits 6:4 hold
 * 001 for sixteen bits, and bits 3:0 hold channels minus one.
 */
#define HDA_CAPTURE_FORMAT 0x0210u

/** Bytes in one buffer half. Ten times 128, because the length must be a multiple. */
#define HDA_CAPTURE_HALF_BYTES 1280u

/** Halves in the cyclic buffer. Two is the minimum that lets one be read while the
 *  other fills, which is the whole reason the buffer is cyclic. */
#define HDA_CAPTURE_HALVES 2u

/** Input stream descriptors start here; each is 0x20 bytes wide. */
#define HDA_REG_STREAM_DESCRIPTOR_BASE 0x80u

/* Offsets inside one stream descriptor. */
#define HDA_STREAM_CONTROL              0x00u
#define HDA_STREAM_STATUS               0x03u
#define HDA_STREAM_LINK_POSITION        0x04u
#define HDA_STREAM_CYCLIC_BUFFER_LENGTH 0x08u
#define HDA_STREAM_LAST_VALID_INDEX     0x0Cu
#define HDA_STREAM_FORMAT               0x12u
#define HDA_STREAM_DESCRIPTOR_LIST_LOW  0x18u
#define HDA_STREAM_DESCRIPTOR_LIST_HIGH 0x1Cu

/** Stream control bit 0: reset. */
#define HDA_STREAM_CONTROL_RESET (1u << 0)

/** Stream control bit 1: run. */
#define HDA_STREAM_CONTROL_RUN (1u << 1)

static IntelHighDefinitionAudioState_t hda_state;
static volatile uint32_t *hda_command_ring = NULL;
static volatile uint64_t *hda_response_ring = NULL;
static uint16_t hda_command_write_pointer = 0u;
static uint16_t hda_response_read_pointer = 0u;
static volatile uint32_t *hda_buffer_descriptor_list = NULL;
static volatile int16_t *hda_capture_buffer = NULL;
static uint32_t hda_capture_half_read = 0u;

/**
 * @brief Reads a byte from the register window.
 * @param offset Register offset.
 * @return The value.
 */
static uint8_t hda_read8(uint32_t offset) { return *(volatile uint8_t *) (uintptr_t) (hda_state.bar_virtual + offset); }

/**
 * @brief Writes a byte to the register window.
 * @param offset Register offset.
 * @param value  The value.
 */
static void hda_write8(uint32_t offset, uint8_t value)
{
    *(volatile uint8_t *) (uintptr_t) (hda_state.bar_virtual + offset) = value;
}

/**
 * @brief Reads a 16-bit register.
 * @param offset Register offset.
 * @return The value.
 */
static uint16_t hda_read16(uint32_t offset)
{
    return *(volatile uint16_t *) (uintptr_t) (hda_state.bar_virtual + offset);
}

/**
 * @brief Writes a 16-bit register.
 * @param offset Register offset.
 * @param value  The value.
 */
static void hda_write16(uint32_t offset, uint16_t value)
{
    *(volatile uint16_t *) (uintptr_t) (hda_state.bar_virtual + offset) = value;
}

/**
 * @brief Reads a 32-bit register.
 * @param offset Register offset.
 * @return The value.
 */
static uint32_t hda_read32(uint32_t offset)
{
    return *(volatile uint32_t *) (uintptr_t) (hda_state.bar_virtual + offset);
}

/**
 * @brief Writes a 32-bit register.
 * @param offset Register offset.
 * @param value  The value.
 */
static void hda_write32(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *) (uintptr_t) (hda_state.bar_virtual + offset) = value;
}

/**
 * @brief Maps a device window into virtual space, cache-disabled.
 *
 * The same shape the virtio GPU uses. Cache-disabled and write-through because these
 * are registers: a cached read would return whatever the line held when it was
 * filled, which for a status register is the answer to a question nobody asked.
 *
 * @param phys_base_raw Physical base, as read from the base address register.
 * @param size          Window length in bytes.
 * @return The virtual address of the base, or 0.
 */
static uint32_t hda_map_window(uint32_t phys_base_raw, uint32_t size)
{
    if (phys_base_raw == 0u || size == 0u)
        return 0u;

    const uint32_t page_offset = phys_base_raw & 0xFFFu;
    const uint32_t phys_base = phys_base_raw & 0xFFFFF000u;
    const uint32_t page_count = ((page_offset + size) + 0xFFFu) >> 12;

    void *const virt = kernel_vmm_reserve_pages(page_count);
    if (virt == NULL)
        return 0u;

    const PageDirectoryEntry_t pde_flags = {.present = 1u, .read_write = 1u};
    const PageTableEntry_t pte_flags = {.present = 1u, .read_write = 1u, .cache_disable = 1u, .write_through = 1u};

    const uint32_t virt_base = (uint32_t) virt;
    for (uint32_t i = 0u; i < page_count; ++i)
    {
        if (!paging_map_page(virt_base + (i << 12), phys_base + (i << 12), pde_flags, pte_flags))
        {
            kernel_vmm_free_pages(virt, page_count);
            return 0u;
        }
    }
    return virt_base + page_offset;
}

/**
 * @brief Claims one physical page and maps it for uncached device access.
 *
 * The rings are read and written by the controller's bus master, so the memory has to
 * be physically contiguous and its physical address has to be known — neither of
 * which the kernel heap promises. A page frame is both by construction.
 *
 * @param out_physical Receives the physical address.
 * @return The virtual address, or NULL.
 */
static void *hda_claim_dma_page(uint32_t *out_physical)
{
    const uint32_t physical = physical_memory_manager_page_frame_allocate();
    if (physical == 0u)
        return NULL;

    void *const virt = kernel_vmm_reserve_pages(1u);
    if (virt == NULL)
    {
        physical_memory_manager_page_frame_free(physical);
        return NULL;
    }

    const PageDirectoryEntry_t pde_flags = {.present = 1u, .read_write = 1u};
    const PageTableEntry_t pte_flags = {.present = 1u, .read_write = 1u, .cache_disable = 1u, .write_through = 1u};
    if (!paging_map_page((uint32_t) virt, physical, pde_flags, pte_flags))
    {
        kernel_vmm_free_pages(virt, 1u);
        physical_memory_manager_page_frame_free(physical);
        return NULL;
    }

    for (uint32_t i = 0u; i < 1024u; ++i)
        ((volatile uint32_t *) virt)[i] = 0u;

    if (out_physical != NULL)
        *out_physical = physical;
    return virt;
}

/**
 * @brief Takes the controller out of reset.
 *
 * Both edges are waited on, and both matter. Driving reset low and immediately high
 * again leaves the controller part-way through its own sequence, and what it does
 * then is not specified — it simply never announces a codec, which reads as "no
 * hardware" rather than as "driver bug".
 *
 * @return true when the controller reports itself out of reset.
 */
static bool hda_reset_controller(void)
{
    hda_write32(HDA_REG_GLOBAL_CONTROL, hda_read32(HDA_REG_GLOBAL_CONTROL) & ~(uint32_t) HDA_GLOBAL_CONTROL_RESET);
    for (uint32_t spin = 0u; spin < HDA_SPIN_BUDGET; ++spin)
        if ((hda_read32(HDA_REG_GLOBAL_CONTROL) & HDA_GLOBAL_CONTROL_RESET) == 0u)
            break;
    if ((hda_read32(HDA_REG_GLOBAL_CONTROL) & HDA_GLOBAL_CONTROL_RESET) != 0u)
        return false;

    hda_write32(HDA_REG_GLOBAL_CONTROL, hda_read32(HDA_REG_GLOBAL_CONTROL) | HDA_GLOBAL_CONTROL_RESET);
    for (uint32_t spin = 0u; spin < HDA_SPIN_BUDGET; ++spin)
        if ((hda_read32(HDA_REG_GLOBAL_CONTROL) & HDA_GLOBAL_CONTROL_RESET) != 0u)
            break;
    if ((hda_read32(HDA_REG_GLOBAL_CONTROL) & HDA_GLOBAL_CONTROL_RESET) == 0u)
        return false;

    for (uint32_t spin = 0u; spin < HDA_SPIN_BUDGET; ++spin)
        asmutils_no_operation();

    return true;
}

/**
 * @brief Points the controller at the rings and starts them.
 * @return true when both rings are running.
 */
static bool hda_start_rings(void)
{
    uint32_t command_physical = 0u;
    uint32_t response_physical = 0u;

    hda_command_ring = (volatile uint32_t *) hda_claim_dma_page(&command_physical);
    hda_response_ring = (volatile uint64_t *) hda_claim_dma_page(&response_physical);
    if (hda_command_ring == NULL || hda_response_ring == NULL)
        return false;

    hda_write8(HDA_REG_COMMAND_RING_CONTROL, 0u);
    hda_write8(HDA_REG_RESPONSE_RING_CONTROL, 0u);

    hda_write32(HDA_REG_COMMAND_RING_BASE_LOW, command_physical);
    hda_write32(HDA_REG_COMMAND_RING_BASE_HIGH, 0u);
    hda_write32(HDA_REG_RESPONSE_RING_BASE_LOW, response_physical);
    hda_write32(HDA_REG_RESPONSE_RING_BASE_HIGH, 0u);

    /* Size 0x02 selects 256 entries, the only size every controller implements. */
    hda_write8(HDA_REG_COMMAND_RING_SIZE, 0x02u);
    hda_write8(HDA_REG_RESPONSE_RING_SIZE, 0x02u);

    /* Resetting the read pointer is a two-step handshake: set the bit and wait for the
       controller to acknowledge by reading it back set, then clear it and wait for the
       readback to clear. Skipping the second half leaves the pointer in reset and the
       ring never advances — which looks exactly like a codec that is not answering. */
    hda_write16(HDA_REG_COMMAND_RING_READ_POINTER, (uint16_t) HDA_RING_POINTER_RESET);
    for (uint32_t spin = 0u; spin < HDA_SPIN_BUDGET; ++spin)
        if ((hda_read16(HDA_REG_COMMAND_RING_READ_POINTER) & HDA_RING_POINTER_RESET) != 0u)
            break;
    hda_write16(HDA_REG_COMMAND_RING_READ_POINTER, 0u);
    for (uint32_t spin = 0u; spin < HDA_SPIN_BUDGET; ++spin)
        if ((hda_read16(HDA_REG_COMMAND_RING_READ_POINTER) & HDA_RING_POINTER_RESET) == 0u)
            break;

    hda_write16(HDA_REG_COMMAND_RING_WRITE_POINTER, 0u);
    hda_command_write_pointer = 0u;

    hda_write16(HDA_REG_RESPONSE_RING_WRITE_POINTER, (uint16_t) HDA_RING_POINTER_RESET);
    hda_response_read_pointer = 0u;

    /* How many responses may accumulate before the controller insists on being
       acknowledged. This is an interrupt-coalescing threshold, and a polling driver
       takes no interrupts — so it wants the threshold OUT of the way, not at one.
       Measured: at one, the controller answers the first verb and then stops fetching
       the ring entirely (CORBRP frozen one behind CORBWP with the fetch engine running
       and no error posted), because the count it is waiting to have acknowledged has
       already been reached. Zero is avoided for the opposite reason — some controllers
       read it as "never write a response at all". */
    hda_write16(HDA_REG_RESPONSE_INTERRUPT_COUNT, 0xFFu);
    hda_write8(HDA_REG_RESPONSE_RING_STATUS, HDA_RESPONSE_STATUS_INTERRUPT | HDA_RESPONSE_STATUS_OVERRUN);

    hda_write8(HDA_REG_COMMAND_RING_CONTROL, HDA_COMMAND_RING_RUN);
    hda_write8(HDA_REG_RESPONSE_RING_CONTROL, HDA_RESPONSE_RING_RUN);

    return (hda_read8(HDA_REG_COMMAND_RING_CONTROL) & HDA_COMMAND_RING_RUN) != 0u &&
           (hda_read8(HDA_REG_RESPONSE_RING_CONTROL) & HDA_RESPONSE_RING_RUN) != 0u;
}

/**
 * @brief Reads both rings' pointers, controls and statuses into a probe.
 *
 * @param out Receives the registers.
 */
static void hda_probe_rings(IntelHighDefinitionAudioRingProbe_t *out)
{
    out->command_write_pointer_shadow = hda_command_write_pointer;
    out->command_write_pointer = hda_read16(HDA_REG_COMMAND_RING_WRITE_POINTER);
    out->command_read_pointer = hda_read16(HDA_REG_COMMAND_RING_READ_POINTER);
    out->response_write_pointer = hda_read16(HDA_REG_RESPONSE_RING_WRITE_POINTER);
    out->response_read_pointer_shadow = hda_response_read_pointer;
    out->command_ring_control = hda_read8(HDA_REG_COMMAND_RING_CONTROL);
    out->command_ring_status = hda_read8(HDA_REG_COMMAND_RING_STATUS);
    out->response_ring_control = hda_read8(HDA_REG_RESPONSE_RING_CONTROL);
    out->response_ring_status = hda_read8(HDA_REG_RESPONSE_RING_STATUS);
}

/**
 * @brief Puts one already-encoded command on the ring and waits for its answer.
 *
 * The single wait path. It used to be written once per verb encoding, which is one
 * copy too many for something whose correctness depends on acknowledging the response
 * flag every single time: two copies are two chances for one of them to stop doing it.
 * The encodings differ only in how the command word is packed, so that is all the
 * callers do.
 *
 * @param command      The packed command word.
 * @param out_response Receives the answer; may be NULL.
 * @return true when an answer came back inside the spin budget.
 */
static bool hda_submit(uint32_t command, uint32_t *out_response)
{
    if (!hda_state.rings_running || hda_command_ring == NULL || hda_response_ring == NULL)
        return false;

    IntelHighDefinitionAudioRingProbe_t before;
    hda_probe_rings(&before);

    /* The write pointer names the LAST entry written, so it is advanced before the
       entry is placed rather than after. Writing the entry at the current pointer and
       then advancing would hand the controller an entry it has already read past. */
    hda_command_write_pointer = (uint16_t) ((hda_command_write_pointer + 1u) % HDA_RING_ENTRIES);
    hda_command_ring[hda_command_write_pointer] = command;
    hda_write16(HDA_REG_COMMAND_RING_WRITE_POINTER, hda_command_write_pointer);
    ++hda_state.verbs_sent;

    for (uint32_t spin = 0u; spin < HDA_SPIN_BUDGET; ++spin)
    {
        const uint16_t written = hda_read16(HDA_REG_RESPONSE_RING_WRITE_POINTER) & 0xFFu;
        if (written == hda_response_read_pointer)
            continue;

        hda_response_read_pointer = (uint16_t) ((hda_response_read_pointer + 1u) % HDA_RING_ENTRIES);
        /* Each response is two words: the value, then which codec sent it. Only the
           first is wanted here, and reading the pair as one 64-bit load is what the
           ring's own layout asks for. */
        const uint64_t response = hda_response_ring[hda_response_read_pointer];
        ++hda_state.responses_read;
        /* Acknowledge, or this is the last response that ever arrives. */
        hda_write8(HDA_REG_RESPONSE_RING_STATUS, HDA_RESPONSE_STATUS_INTERRUPT | HDA_RESPONSE_STATUS_OVERRUN);
        if (out_response != NULL)
            *out_response = (uint32_t) (response & 0xFFFFFFFFu);
        return true;
    }

    ++hda_state.verb_timeouts;
    /* Only the FIRST failure is kept. Later ones are taken with the rings already in
       whatever state the first failure left them, so they describe the consequence
       rather than the cause. */
    if (!hda_state.probe_captured)
    {
        hda_state.probe_captured = true;
        hda_state.probe_command = command;
        hda_state.probe_before = before;
        hda_probe_rings(&hda_state.probe_after);
    }
    return false;
}

bool intel_high_definition_audio_command(uint8_t codec, uint8_t node, uint16_t verb, uint8_t payload,
                                         uint32_t *out_response)
{
    if (codec >= INTEL_HIGH_DEFINITION_AUDIO_MAX_CODECS)
        return false;

    return hda_submit(((uint32_t) codec << 28) | ((uint32_t) node << 20) | (((uint32_t) verb & 0xFFFu) << 8) |
                          (uint32_t) payload,
                      out_response);
}

bool intel_high_definition_audio_command_wide(uint8_t codec, uint8_t node, uint8_t verb, uint16_t payload,
                                              uint32_t *out_response)
{
    if (codec >= INTEL_HIGH_DEFINITION_AUDIO_MAX_CODECS)
        return false;

    /* The four-bit form puts the verb at bits 19:16 and the payload at 15:0. The
       twelve-bit form puts the verb at 19:8 and the payload at 7:0. Same word, two
       layouts — which is why these are two functions and not one with a wider
       argument. */
    return hda_submit(((uint32_t) codec << 28) | ((uint32_t) node << 20) | (((uint32_t) verb & 0xFu) << 16) |
                          (uint32_t) payload,
                      out_response);
}

/**
 * @brief Mutes every output amplifier the walk identified.
 *
 * Both the converter's amplifier and the pin's, because either one alone can carry a
 * signal to a speaker and a codec is free to implement one, the other, or both.
 * Counted rather than assumed: a mute that silently failed to be sent would look
 * exactly like a mute that worked.
 *
 * @param codec Slot the widgets belong to.
 */
static void hda_mute_outputs(uint8_t codec)
{
    uint32_t ignored = 0u;

    if (hda_state.playback_converter != 0u &&
        intel_high_definition_audio_command_wide(codec, hda_state.playback_converter, HDA_VERB_SET_AMPLIFIER_GAIN,
                                                 HDA_AMPLIFIER_MUTE_OUTPUT, &ignored))
        ++hda_state.outputs_muted;

    if (hda_state.playback_pin != 0u &&
        intel_high_definition_audio_command_wide(codec, hda_state.playback_pin, HDA_VERB_SET_AMPLIFIER_GAIN,
                                                 HDA_AMPLIFIER_MUTE_OUTPUT, &ignored))
        ++hda_state.outputs_muted;
}

/**
 * @brief Walks the codec's node tree looking for an input converter and its pin.
 *
 * The tree is walked rather than assumed, because a codec's node numbering is its
 * own business: the QEMU codec, a laptop's Realtek and a USB dongle put their
 * converters at different identifiers, and a driver that hard-coded one would work on
 * exactly the machine it was written on.
 *
 * @param codec Slot to walk.
 */
static void hda_find_capture_path(uint8_t codec)
{
    uint32_t response = 0u;

    if (!intel_high_definition_audio_command(codec, 0u, HDA_VERB_GET_PARAMETER, HDA_PARAMETER_SUB_NODE_COUNT,
                                             &response))
        return;

    const uint8_t first_group = (uint8_t) ((response >> 16) & 0xFFu);
    const uint8_t group_count = (uint8_t) (response & 0xFFu);

    for (uint8_t group = 0u; group < group_count; ++group)
    {
        const uint8_t node = (uint8_t) (first_group + group);
        if (!intel_high_definition_audio_command(codec, node, HDA_VERB_GET_PARAMETER, HDA_PARAMETER_FUNCTION_GROUP_TYPE,
                                                 &response))
            continue;
        if ((response & 0x7Fu) != HDA_FUNCTION_GROUP_AUDIO)
            continue;

        hda_state.audio_function_group = node;

        if (!intel_high_definition_audio_command(codec, node, HDA_VERB_GET_PARAMETER, HDA_PARAMETER_SUB_NODE_COUNT,
                                                 &response))
            return;

        const uint8_t first_widget = (uint8_t) ((response >> 16) & 0xFFu);
        const uint8_t widget_count = (uint8_t) (response & 0xFFu);

        for (uint8_t index = 0u; index < widget_count; ++index)
        {
            const uint8_t widget = (uint8_t) (first_widget + index);
            ++hda_state.widgets_walked;

            if (!intel_high_definition_audio_command(codec, widget, HDA_VERB_GET_PARAMETER,
                                                     HDA_PARAMETER_WIDGET_CAPABILITIES, &response))
                continue;

            const uint32_t type = (response >> 20) & 0xFu;
            if (type == HDA_WIDGET_TYPE_AUDIO_INPUT && hda_state.capture_converter == 0u)
            {
                hda_state.capture_converter = widget;
            }
            else if (type == HDA_WIDGET_TYPE_AUDIO_OUTPUT && hda_state.playback_converter == 0u)
            {
                hda_state.playback_converter = widget;
            }
            else if (type == HDA_WIDGET_TYPE_PIN_COMPLEX)
            {
                /* A pin is classified by what it can DO, not by where it sits in the
                   numbering. Asking costs one verb and is the difference between
                   powering the microphone's pin and powering the speaker's. */
                uint32_t pin_capabilities = 0u;
                if (!intel_high_definition_audio_command(codec, widget, HDA_VERB_GET_PARAMETER,
                                                         HDA_PARAMETER_PIN_CAPABILITIES, &pin_capabilities))
                    continue;

                if ((pin_capabilities & HDA_PIN_CAPABILITY_INPUT) != 0u && hda_state.capture_pin == 0u)
                    hda_state.capture_pin = widget;
                if ((pin_capabilities & HDA_PIN_CAPABILITY_OUTPUT) != 0u && hda_state.playback_pin == 0u)
                    hda_state.playback_pin = widget;
            }
        }

        /* Silence the output side before anything else can drive it. Done here, at the
           moment the widgets become known, rather than wherever playback is eventually
           written: a mute added alongside the code that could break it is a mute that
           was missing for however long that code existed first. */
        hda_mute_outputs(codec);
        return;
    }
}

bool intel_high_definition_audio_start_capture(void)
{
    if (!hda_state.rings_running || hda_state.capture_converter == 0u)
        return false;

    uint32_t descriptor_physical = 0u;
    uint32_t buffer_physical = 0u;
    hda_buffer_descriptor_list = (volatile uint32_t *) hda_claim_dma_page(&descriptor_physical);
    hda_capture_buffer = (volatile int16_t *) hda_claim_dma_page(&buffer_physical);
    if (hda_buffer_descriptor_list == NULL || hda_capture_buffer == NULL)
        return false;

    /* Two entries, each one half of the cyclic buffer. Each is four words: address
       low, address high, length, flags — and the flag bit asks for an interrupt on
       completion, which is harmless while nothing is listening for one and is what a
       later handler will need. */
    for (uint32_t half = 0u; half < HDA_CAPTURE_HALVES; ++half)
    {
        hda_buffer_descriptor_list[half * 4u + 0u] = buffer_physical + half * HDA_CAPTURE_HALF_BYTES;
        hda_buffer_descriptor_list[half * 4u + 1u] = 0u;
        hda_buffer_descriptor_list[half * 4u + 2u] = HDA_CAPTURE_HALF_BYTES;
        hda_buffer_descriptor_list[half * 4u + 3u] = 1u;
    }

    const uint32_t stream = HDA_REG_STREAM_DESCRIPTOR_BASE;

    /* Reset the descriptor, both edges, for the same reason the controller's own
       reset waits on both: half a reset leaves it in a state the specification does
       not describe. */
    hda_write8(stream + HDA_STREAM_CONTROL, HDA_STREAM_CONTROL_RESET);
    for (uint32_t spin = 0u; spin < HDA_SPIN_BUDGET; ++spin)
        if ((hda_read8(stream + HDA_STREAM_CONTROL) & HDA_STREAM_CONTROL_RESET) != 0u)
            break;
    hda_write8(stream + HDA_STREAM_CONTROL, 0u);
    for (uint32_t spin = 0u; spin < HDA_SPIN_BUDGET; ++spin)
        if ((hda_read8(stream + HDA_STREAM_CONTROL) & HDA_STREAM_CONTROL_RESET) == 0u)
            break;

    hda_write32(stream + HDA_STREAM_CYCLIC_BUFFER_LENGTH, HDA_CAPTURE_HALVES * HDA_CAPTURE_HALF_BYTES);
    hda_write16(stream + HDA_STREAM_LAST_VALID_INDEX, (uint16_t) (HDA_CAPTURE_HALVES - 1u));
    hda_write16(stream + HDA_STREAM_FORMAT, HDA_CAPTURE_FORMAT);
    hda_write32(stream + HDA_STREAM_DESCRIPTOR_LIST_LOW, descriptor_physical);
    hda_write32(stream + HDA_STREAM_DESCRIPTOR_LIST_HIGH, 0u);

    /* The stream number lives in bits 23:20 of the control register, which is why it
       is written as a 32-bit access even though the low byte carries the run bit. The
       converter is bound to the same number below; a mismatch there is a stream that
       runs and captures nothing. */
    hda_write32(stream + HDA_STREAM_CONTROL, (HDA_CAPTURE_STREAM_NUMBER << 20));

    const uint8_t codec = 0u;
    uint32_t ignored = 0u;
    (void) intel_high_definition_audio_command_wide(codec, hda_state.capture_converter, HDA_VERB_SET_CONVERTER_FORMAT,
                                                    HDA_CAPTURE_FORMAT, &ignored);
    (void) intel_high_definition_audio_command(codec, hda_state.capture_converter, HDA_VERB_SET_STREAM_CHANNEL,
                                               (uint8_t) (HDA_CAPTURE_STREAM_NUMBER << 4), &ignored);
    /* Unmute the input amplifier at its loudest setting the codec offers. Bit 15 sets
       the output side, 14 the input side, 13 the left channel and 12 the right; the
       low seven bits are the gain. */
    (void) intel_high_definition_audio_command_wide(codec, hda_state.capture_converter, HDA_VERB_SET_AMPLIFIER_GAIN,
                                                    0x7000u | 0x0027u, &ignored);
    if (hda_state.capture_pin != 0u)
        (void) intel_high_definition_audio_command(codec, hda_state.capture_pin, HDA_VERB_SET_PIN_CONTROL,
                                                   HDA_PIN_CONTROL_INPUT_ENABLE, &ignored);

    hda_write32(stream + HDA_STREAM_CONTROL, (HDA_CAPTURE_STREAM_NUMBER << 20) | HDA_STREAM_CONTROL_RUN);

    hda_capture_half_read = 0u;
    hda_state.capture_running = (hda_read8(stream + HDA_STREAM_CONTROL) & HDA_STREAM_CONTROL_RUN) != 0u;
    return hda_state.capture_running;
}

uint32_t intel_high_definition_audio_poll_capture(int16_t *out, uint32_t capacity)
{
    if (!hda_state.capture_running || out == NULL || hda_capture_buffer == NULL)
        return 0u;

    const uint32_t samples_per_half = HDA_CAPTURE_HALF_BYTES / 2u;
    if (capacity < samples_per_half)
        return 0u;

    const uint32_t position = hda_read32(HDA_REG_STREAM_DESCRIPTOR_BASE + HDA_STREAM_LINK_POSITION);
    if (position < hda_state.capture_position)
        ++hda_state.capture_wraps;
    hda_state.capture_position = position;

    /* Which half the controller is writing right now. The OTHER one is complete and
       safe to read — which is the entire reason there are two. */
    const uint32_t writing = position / HDA_CAPTURE_HALF_BYTES;
    if (writing == hda_capture_half_read)
        return 0u;

    const uint32_t ready = hda_capture_half_read;
    hda_capture_half_read = writing % HDA_CAPTURE_HALVES;

    for (uint32_t i = 0u; i < samples_per_half; ++i)
        out[i] = hda_capture_buffer[ready * samples_per_half + i];
    return samples_per_half;
}

bool intel_high_definition_audio_initialize(IntelHighDefinitionAudioState_t *out)
{
    for (uint32_t i = 0u; i < sizeof(hda_state); ++i)
        ((volatile uint8_t *) &hda_state)[i] = 0u;
    hda_command_ring = NULL;
    hda_response_ring = NULL;

    const PeripheralComponentInterconnectDevice_t *const device =
        peripheral_component_interconnect_find_by_class(HDA_PCI_CLASS, HDA_PCI_SUBCLASS);
    if (device == NULL)
    {
        if (out != NULL)
            *out = hda_state;
        return false;
    }
    hda_state.controller_present = true;

    /* Memory decode and bus mastering, both. Bus mastering is the one that is easy to
       forget and hard to diagnose: without it every register reads correctly and the
       controller silently never touches the rings. */
    uint16_t command = peripheral_component_interconnect_config_read_word(device->bus, device->device, device->function,
                                                                          HDA_PCI_COMMAND_OFFSET);
    command |= (uint16_t) (HDA_PCI_COMMAND_MEMORY_SPACE | HDA_PCI_COMMAND_BUS_MASTER);
    peripheral_component_interconnect_config_write_word(device->bus, device->device, device->function,
                                                        HDA_PCI_COMMAND_OFFSET, command);

    PeripheralComponentInterconnectBaseAddressRegister_t bar;
    if (!peripheral_component_interconnect_read_base_address_register(device->bus, device->device, device->function, 0u,
                                                                      &bar))
    {
        if (out != NULL)
            *out = hda_state;
        return false;
    }

    /* The window is 32-bit even where the register pair is 64: this kernel maps into
       a 32-bit address space, so a controller placed above four gibibytes is one it
       cannot reach and must say so rather than truncate the address and write into
       whatever happens to live at the low half. */
    if (bar.is_io || (bar.base >> 32) != 0u)
    {
        if (out != NULL)
            *out = hda_state;
        return false;
    }

    hda_state.bar_virtual = hda_map_window((uint32_t) bar.base, bar.size != 0u ? (uint32_t) bar.size : 0x4000u);
    if (hda_state.bar_virtual == 0u)
    {
        if (out != NULL)
            *out = hda_state;
        return false;
    }

    if (!hda_reset_controller())
    {
        if (out != NULL)
            *out = hda_state;
        return false;
    }
    hda_state.controller_running = true;

    hda_state.major_version = hda_read8(HDA_REG_MAJOR_VERSION);
    hda_state.minor_version = hda_read8(HDA_REG_MINOR_VERSION);

    const uint16_t capabilities = hda_read16(HDA_REG_GLOBAL_CAPABILITIES);
    hda_state.input_streams = (uint8_t) ((capabilities >> 8) & 0x0Fu);
    hda_state.output_streams = (uint8_t) ((capabilities >> 12) & 0x0Fu);

    /* Which codec slots announced themselves during the reset. Written back to clear
       it, because these bits are sticky and a later state change would otherwise be
       indistinguishable from this one. */
    hda_state.codec_mask = hda_read16(HDA_REG_STATE_CHANGE_STATUS);
    hda_write16(HDA_REG_STATE_CHANGE_STATUS, hda_state.codec_mask);

    hda_state.rings_running = hda_start_rings();
    if (!hda_state.rings_running)
    {
        if (out != NULL)
            *out = hda_state;
        return false;
    }

    bool answered = false;
    for (uint8_t slot = 0u; slot < INTEL_HIGH_DEFINITION_AUDIO_MAX_CODECS; ++slot)
    {
        if ((hda_state.codec_mask & (uint16_t) (1u << slot)) == 0u)
            continue;
        uint32_t vendor = 0u;
        if (intel_high_definition_audio_command(slot, 0u, HDA_VERB_GET_PARAMETER, HDA_PARAMETER_VENDOR_ID, &vendor))
        {
            hda_state.codec_vendor[slot] = vendor;
            answered = true;
        }
    }

    /* With a codec talking, look for the path a satellite needs: an input converter
       and the pin that feeds it. Failing to find one is reported, not fatal — a codec
       with no capture path is a legitimate machine, just not a satellite. */
    if (answered)
    {
        for (uint8_t slot = 0u; slot < INTEL_HIGH_DEFINITION_AUDIO_MAX_CODECS; ++slot)
        {
            if ((hda_state.codec_mask & (uint16_t) (1u << slot)) == 0u)
                continue;
            hda_find_capture_path(slot);
            if (hda_state.capture_converter != 0u)
                break;
        }
        (void) intel_high_definition_audio_start_capture();
    }

    if (out != NULL)
        *out = hda_state;
    return answered;
}

const IntelHighDefinitionAudioState_t *intel_high_definition_audio_state(void) { return &hda_state; }
