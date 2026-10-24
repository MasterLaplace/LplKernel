#include <kernel/drivers/nvme.h>

#include <kernel/cpu/irq.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pci.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/pinned_memory.h>
#include <kernel/memory/vmm.h>

#include <stddef.h>
#include <string.h>

/**
 * @name Controller registers, from the start of base address register 0
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CAPABILITIES           0x00u   /**< CAP, 64 bits. */
#define NON_VOLATILE_MEMORY_EXPRESS_REGISTER_VERSION                0x08u   /**< VS. */
#define NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CONFIGURATION          0x14u   /**< CC. */
#define NON_VOLATILE_MEMORY_EXPRESS_REGISTER_STATUS                 0x1Cu   /**< CSTS. */
#define NON_VOLATILE_MEMORY_EXPRESS_REGISTER_ADMIN_QUEUE_ATTRIBUTES 0x24u   /**< AQA. */
#define NON_VOLATILE_MEMORY_EXPRESS_REGISTER_ADMIN_SUBMISSION_QUEUE 0x28u   /**< ASQ, 64 bits. */
#define NON_VOLATILE_MEMORY_EXPRESS_REGISTER_ADMIN_COMPLETION_QUEUE 0x30u   /**< ACQ, 64 bits. */
#define NON_VOLATILE_MEMORY_EXPRESS_REGISTER_DOORBELLS              0x1000u /**< Queue 0's submission tail. */
/** @} */

/**
 * @name Fields of the configuration register
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_CONFIGURATION_ENABLE  (1u << 0)  /**< EN. */
#define NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_ENTRY_SIZE (6u << 16) /**< IOSQES: 64-byte entries. */
#define NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_ENTRY_SIZE (4u << 20) /**< IOCQES: 16-byte entries. */
/** @} */

/**
 * @name Bits of the status register
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_STATUS_READY (1u << 0) /**< RDY. */
#define NON_VOLATILE_MEMORY_EXPRESS_STATUS_FATAL (1u << 1) /**< CFS: the controller hit a fatal error. */
/** @} */

/**
 * @name The queue pairs
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_QUEUE_ENTRIES        64u      /**< Entries in each queue, admin and I/O. */
#define NON_VOLATILE_MEMORY_EXPRESS_ADMIN_QUEUE          0u       /**< The admin queue pair's identifier. */
#define NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_QUEUE   1u       /**< The first I/O queue pair's identifier. */
#define NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_PAIRS   5u       /**< I/O queue pairs: 5 x 63 commands in flight. */
#define NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS    16u      /**< Dwords in a submission entry. */
#define NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_DWORDS    4u       /**< Dwords in a completion entry. */
#define NON_VOLATILE_MEMORY_EXPRESS_COMMAND_MICROSECONDS 1000000u /**< How long a command may take. */
/** @} */

/**
 * @name Admin commands and their fields
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_OPCODE_CREATE_SUBMISSION_QUEUE 0x01u      /**< Create I/O Submission Queue. */
#define NON_VOLATILE_MEMORY_EXPRESS_OPCODE_CREATE_COMPLETION_QUEUE 0x05u      /**< Create I/O Completion Queue. */
#define NON_VOLATILE_MEMORY_EXPRESS_OPCODE_SET_FEATURES            0x09u      /**< Set Features. */
#define NON_VOLATILE_MEMORY_EXPRESS_FEATURE_NUMBER_OF_QUEUES       0x07u      /**< Number of Queues. */
#define NON_VOLATILE_MEMORY_EXPRESS_QUEUE_PHYSICALLY_CONTIGUOUS    (1u << 0)  /**< PC, in dword 11 of both creations. */
#define NON_VOLATILE_MEMORY_EXPRESS_OPCODE_IDENTIFY                0x06u      /**< Identify. */
#define NON_VOLATILE_MEMORY_EXPRESS_IDENTIFY_NAMESPACE             0x00u      /**< CNS 00h: a namespace. */
#define NON_VOLATILE_MEMORY_EXPRESS_IDENTIFY_CONTROLLER            0x01u      /**< CNS 01h: the controller. */
#define NON_VOLATILE_MEMORY_EXPRESS_FIRST_NAMESPACE                1u         /**< The namespace identified. */
#define NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_PHASE               (1u << 16) /**< Phase tag, in dword 3. */
#define NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_TIMED_OUT           0xFFFFu /**< No status: the command never completed. */
/** @} */

/**
 * @name Commands of the NVM command set
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_OPCODE_WRITE 0x01u    /**< Write. */
#define NON_VOLATILE_MEMORY_EXPRESS_OPCODE_READ  0x02u    /**< Read. */
#define NON_VOLATILE_MEMORY_EXPRESS_MOST_BLOCKS  0x10000u /**< NLB is 16 bits and 0's based. */
/** @} */

/**
 * @name Offsets in the Identify data
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_SERIAL_NUMBER 4u  /**< SN, 20 bytes of ASCII. */
#define NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_MODEL_NUMBER  24u /**< MN, 40 bytes of ASCII. */
#define NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_TRANSFER_SIZE                                                           \
    77u                                                           /**< MDTS, a power of two of pages, 0 for no limit.  \
                                                                   */
#define NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_NAMESPACES    516u /**< NN, a dword. */
#define NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_SIZE           0u   /**< NSZE, a qword. */
#define NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_FORMATTED_SIZE 26u  /**< FLBAS. */
#define NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_BLOCK_FORMATS  128u /**< LBAF0, then one dword per format. */
#define NON_VOLATILE_MEMORY_EXPRESS_BLOCK_FORMAT_DATA_SIZE   2u   /**< LBADS, a power of two, in the format. */
/** @} */

/** Bytes of a memory page, as the controller is told: 2^(12 + CC.MPS) with MPS zero. */
#define NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES 4096u

/** Pages one read or write may span: the first, and the 128 that one page of PRP entries lists. */
#define NON_VOLATILE_MEMORY_EXPRESS_TRANSFER_PAGES 129u

/**
 * @brief A submission queue, the completion queue it posts to, and where both sides stand.
 */
typedef struct NonVolatileMemoryExpressQueuePair {
    volatile uint32_t *submissions; /**< The submission entries, one page. */
    volatile uint32_t *completions; /**< The completion entries, one page. */
    uint32_t submissions_physical;  /**< Where the controller reads the submissions. */
    uint32_t completions_physical;  /**< Where the controller writes the completions. */
    uint32_t submission_doorbell;   /**< Offset of the submission tail doorbell. */
    uint32_t completion_doorbell;   /**< Offset of the completion head doorbell. */
    uint32_t tail;                  /**< Next submission entry the host fills. */
    uint32_t head;                  /**< Next completion entry the host reads. */
    uint32_t phase;                 /**< Phase tag a new completion carries on this pass. */
} NonVolatileMemoryExpressQueuePair_t;

static NonVolatileMemoryExpressState_t non_volatile_memory_express_found;
static uint32_t non_volatile_memory_express_registers = 0u;
static NonVolatileMemoryExpressQueuePair_t non_volatile_memory_express_admin;
static NonVolatileMemoryExpressQueuePair_t
    non_volatile_memory_express_input_output[NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_PAIRS];
static volatile uint8_t *non_volatile_memory_express_identify_data = NULL;
static uint32_t non_volatile_memory_express_identify_physical = 0u;
static volatile uint32_t *non_volatile_memory_express_page_list = NULL;
static uint32_t non_volatile_memory_express_page_list_physical = 0u;
static uint16_t non_volatile_memory_express_next_command = 0u;

/**
 * @brief Reads a 32-bit controller register.
 */
static uint32_t non_volatile_memory_express_read_register(uint32_t offset)
{
    return *(volatile const uint32_t *) (uintptr_t) (non_volatile_memory_express_registers + offset);
}

/**
 * @brief Writes a 32-bit controller register.
 */
static void non_volatile_memory_express_write_register(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *) (uintptr_t) (non_volatile_memory_express_registers + offset) = value;
}

/**
 * @brief Writes a 64-bit controller register whose high half is zero, low half first.
 *
 * @details This kernel's physical addresses fit in 32 bits. Two 32-bit writes, low then high, are
 *          what Linux does on a 32-bit host (lo_hi_writeq).
 */
static void non_volatile_memory_express_write_register_address(uint32_t offset, uint32_t low)
{
    non_volatile_memory_express_write_register(offset, low);
    non_volatile_memory_express_write_register(offset + 4u, 0u);
}

/**
 * @brief Records why the bring-up stopped.
 *
 * @param step The step that failed.
 * @return false, for the caller to return.
 */
static bool non_volatile_memory_express_fail(const char *step)
{
    non_volatile_memory_express_found.failure = step;
    return false;
}

/**
 * @brief Waits until the ready bit reads @p ready, within the timeout the controller reports.
 *
 * @param ready The value awaited.
 * @return false on a timeout, or when the controller reports a fatal error.
 */
static bool non_volatile_memory_express_wait_ready(bool ready)
{
    InterruptRequestDeadline_t deadline;

    interrupt_request_deadline_start(&deadline, non_volatile_memory_express_found.ready_timeout_milliseconds * 1000u);
    for (;;)
    {
        const uint32_t status = non_volatile_memory_express_read_register(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_STATUS);

        if ((status & NON_VOLATILE_MEMORY_EXPRESS_STATUS_FATAL) != 0u)
            return false;
        if (((status & NON_VOLATILE_MEMORY_EXPRESS_STATUS_READY) != 0u) == ready)
            return true;
        if (interrupt_request_deadline_passed(&deadline))
            return false;
        asmutils_pause();
    }
}

/**
 * @brief Claims a zeroed page the controller can reach, and its physical address.
 *
 * @param out_virtual  Receives the page, or keeps it when it was claimed by an earlier bring-up.
 * @param out_physical Receives its physical address.
 * @return false when no page could be claimed.
 */
static bool non_volatile_memory_express_claim_page(volatile void **out_virtual, uint32_t *out_physical)
{
    if (*out_virtual == NULL)
    {
        void *const page = kernel_pinned_alloc(NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES);

        if (page == NULL || !paging_get_physical_address((uint32_t) (uintptr_t) page, out_physical))
            return false;
        *out_virtual = page;
    }
    memset((void *) *out_virtual, 0, NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES);
    return true;
}

/**
 * @brief Copies @p length bytes of padded ASCII into @p out, without the trailing spaces.
 */
static void non_volatile_memory_express_copy_text(char *out, uint32_t capacity, uint32_t offset, uint32_t length)
{
    uint32_t kept = 0u;

    for (uint32_t i = 0u; i < length && i + 1u < capacity; ++i)
    {
        out[i] = (char) non_volatile_memory_express_identify_data[offset + i];
        if (out[i] != ' ')
            kept = i + 1u;
    }
    out[kept] = '\0';
}

/**
 * @brief Reads a little-endian dword of the Identify data.
 */
static uint32_t non_volatile_memory_express_identify_dword(uint32_t offset)
{
    return (uint32_t) non_volatile_memory_express_identify_data[offset] |
           ((uint32_t) non_volatile_memory_express_identify_data[offset + 1u] << 8) |
           ((uint32_t) non_volatile_memory_express_identify_data[offset + 2u] << 16) |
           ((uint32_t) non_volatile_memory_express_identify_data[offset + 3u] << 24);
}

/**
 * @brief Gives a queue pair its pages and its doorbells, and empties it.
 *
 * @param pair       The pair.
 * @param identifier Its queue identifier: 0 for the admin pair.
 * @return false when no page could be claimed.
 */
static bool non_volatile_memory_express_prepare_pair(NonVolatileMemoryExpressQueuePair_t *pair, uint32_t identifier)
{
    if (!non_volatile_memory_express_claim_page((volatile void **) &pair->submissions, &pair->submissions_physical) ||
        !non_volatile_memory_express_claim_page((volatile void **) &pair->completions, &pair->completions_physical))
        return false;
    pair->submission_doorbell = NON_VOLATILE_MEMORY_EXPRESS_REGISTER_DOORBELLS +
                                2u * identifier * non_volatile_memory_express_found.doorbell_stride_bytes;
    pair->completion_doorbell = NON_VOLATILE_MEMORY_EXPRESS_REGISTER_DOORBELLS +
                                (2u * identifier + 1u) * non_volatile_memory_express_found.doorbell_stride_bytes;
    pair->tail = 0u;
    pair->head = 0u;
    pair->phase = NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_PHASE;
    return true;
}

/**
 * @brief Writes @p command into the next submission entry of @p pair, without ringing.
 *
 * @details The entry is whole before the doorbell names it: both are volatile stores, which the
 *          compiler keeps in order and an x86 processor makes visible in order.
 */
static void non_volatile_memory_express_place(NonVolatileMemoryExpressQueuePair_t *pair,
                                              const uint32_t command[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS])
{
    volatile uint32_t *const entry = pair->submissions + pair->tail * NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS;

    for (uint32_t i = 0u; i < NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS; ++i)
        entry[i] = command[i];
    pair->tail = (pair->tail + 1u) % NON_VOLATILE_MEMORY_EXPRESS_QUEUE_ENTRIES;
}

/**
 * @brief Takes the next completion of @p pair if the controller posted it, and frees its entry.
 *
 * @details A new completion is the entry at the head whose phase tag is the one this pass of the
 *          queue expects. The head doorbell is written for each one taken.
 *
 * @param pair       The queue pair.
 * @param out_dword0 Receives dword 0 of the completion, when not NULL.
 * @param out_dword3 Receives dword 3: the command identifier in bits 15:0, the status above.
 * @return false when no new completion is there.
 */
static bool non_volatile_memory_express_take(NonVolatileMemoryExpressQueuePair_t *pair, uint32_t *out_dword0,
                                             uint32_t *out_dword3)
{
    volatile const uint32_t *const completion =
        pair->completions + pair->head * NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_DWORDS;
    const uint32_t dword3 = completion[3];

    if ((dword3 & NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_PHASE) != pair->phase)
        return false;
    if (out_dword0 != NULL)
        *out_dword0 = completion[0];
    *out_dword3 = dword3;
    pair->head = (pair->head + 1u) % NON_VOLATILE_MEMORY_EXPRESS_QUEUE_ENTRIES;
    if (pair->head == 0u)
        pair->phase ^= NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_PHASE;
    non_volatile_memory_express_write_register(pair->completion_doorbell, pair->head);
    return true;
}

/**
 * @brief Submits one command to @p pair and polls its completion.
 *
 * @param pair       The queue pair, with nothing else in flight.
 * @param command    The 16 dwords of the command; its identifier is filled in here.
 * @param out_dword0 Receives dword 0 of the completion, when not NULL.
 * @return The status field of the completion, 0 on success, or
 *         NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_TIMED_OUT.
 */
static uint32_t non_volatile_memory_express_run(NonVolatileMemoryExpressQueuePair_t *pair,
                                                uint32_t command[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS],
                                                uint32_t *out_dword0)
{
    InterruptRequestDeadline_t deadline;
    uint32_t dword3 = 0u;

    command[0] = (command[0] & 0xFFFFu) | ((uint32_t) non_volatile_memory_express_next_command++ << 16);
    non_volatile_memory_express_place(pair, command);
    non_volatile_memory_express_write_register(pair->submission_doorbell, pair->tail);

    interrupt_request_deadline_start(&deadline, NON_VOLATILE_MEMORY_EXPRESS_COMMAND_MICROSECONDS);
    while (!non_volatile_memory_express_take(pair, out_dword0, &dword3))
    {
        if (interrupt_request_deadline_passed(&deadline))
            return NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_TIMED_OUT;
        asmutils_pause();
    }
    return (dword3 >> 17) & 0x7FFu;
}

/**
 * @brief Asks Identify for @p structure of @p namespace_identifier, into the Identify page.
 *
 * @return true when the command completed with a success status.
 */
static bool non_volatile_memory_express_identify(uint32_t structure, uint32_t namespace_identifier)
{
    uint32_t command[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS] = {0u};

    memset((void *) non_volatile_memory_express_identify_data, 0, NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES);
    command[0] = NON_VOLATILE_MEMORY_EXPRESS_OPCODE_IDENTIFY;
    command[1] = namespace_identifier;
    command[6] = non_volatile_memory_express_identify_physical;
    command[10] = structure;
    return non_volatile_memory_express_run(&non_volatile_memory_express_admin, command, NULL) == 0u;
}

/**
 * @brief Points the data pointer of @p command at @p bytes of @p buffer, page by page.
 *
 * @details PRP entry 1 is the first page, at the buffer's offset in it. PRP entry 2 is the second
 *          page when the transfer ends there, or the page of PRP entries listing every page after
 *          the first when it goes further. Each page is translated on its own, so the buffer's
 *          pages need not be contiguous in physical memory.
 *
 * @return false when a page is not mapped, or the transfer spans more pages than one list holds.
 */
static bool non_volatile_memory_express_describe(uint32_t command[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS],
                                                 uint32_t buffer, uint32_t bytes)
{
    const uint32_t offset = buffer & (NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES - 1u);
    const uint32_t pages =
        (offset + bytes + NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES - 1u) / NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES;
    const uint32_t first_page = buffer - offset;
    uint32_t physical = 0u;

    if (pages > NON_VOLATILE_MEMORY_EXPRESS_TRANSFER_PAGES || !paging_get_physical_address(buffer, &physical))
        return false;
    command[6] = physical;
    command[8] = 0u;
    if (pages == 2u)
    {
        if (!paging_get_physical_address(first_page + NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES, &physical))
            return false;
        command[8] = physical;
    }
    else if (pages > 2u)
    {
        for (uint32_t page = 1u; page < pages; ++page)
        {
            if (!paging_get_physical_address(first_page + page * NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES, &physical))
                return false;
            non_volatile_memory_express_page_list[2u * (page - 1u)] = physical;
            non_volatile_memory_express_page_list[2u * (page - 1u) + 1u] = 0u;
        }
        command[8] = non_volatile_memory_express_page_list_physical;
    }
    return true;
}

/**
 * @brief Builds the command that reads or writes @p block_count blocks of namespace 1.
 *
 * @return false when the I/O queues are not ready, or the range, the size or the buffer is out of
 *         bounds.
 */
static bool non_volatile_memory_express_build_transfer(uint32_t command[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS],
                                                       uint32_t opcode, uint64_t first_block, uint32_t block_count,
                                                       uint32_t buffer)
{
    const NonVolatileMemoryExpressState_t *const found = &non_volatile_memory_express_found;

    if (!found->input_output_queues_ready || block_count == 0u ||
        block_count > NON_VOLATILE_MEMORY_EXPRESS_MOST_BLOCKS ||
        block_count > found->maximum_transfer_bytes / found->block_bytes || (buffer & 3u) != 0u ||
        first_block >= found->namespace_blocks || block_count > found->namespace_blocks - first_block ||
        !non_volatile_memory_express_describe(command, buffer, block_count * found->block_bytes))
        return false;
    command[0] = opcode;
    command[1] = NON_VOLATILE_MEMORY_EXPRESS_FIRST_NAMESPACE;
    command[10] = (uint32_t) first_block;
    command[11] = (uint32_t) (first_block >> 32);
    command[12] = block_count - 1u;
    return true;
}

/**
 * @brief Reads or writes @p block_count blocks of namespace 1 through the first I/O queue pair.
 *
 * @return true when the controller completed the command with a success status.
 */
static bool non_volatile_memory_express_transfer(uint32_t opcode, uint64_t first_block, uint32_t block_count,
                                                 uint32_t buffer)
{
    uint32_t command[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS] = {0u};

    return non_volatile_memory_express_build_transfer(command, opcode, first_block, block_count, buffer) &&
           non_volatile_memory_express_run(&non_volatile_memory_express_input_output[0], command, NULL) == 0u;
}

/**
 * @brief Finds the controller, maps its registers and reads its capabilities.
 *
 * @return false when there is none, or when this kernel cannot drive it.
 */
static bool non_volatile_memory_express_find(void)
{
    const PeripheralComponentInterconnectDevice_t *const device = peripheral_component_interconnect_find_by_class(
        NON_VOLATILE_MEMORY_EXPRESS_CLASS, NON_VOLATILE_MEMORY_EXPRESS_SUBCLASS);
    PeripheralComponentInterconnectBaseAddressRegister_t window;

    if (device == NULL || device->prog_interface != NON_VOLATILE_MEMORY_EXPRESS_INTERFACE)
        return non_volatile_memory_express_fail("no controller on the bus");
    non_volatile_memory_express_found.controller_present = true;

    if (!peripheral_component_interconnect_read_base_address_register(device->bus, device->device, device->function, 0u,
                                                                      &window) ||
        !peripheral_component_interconnect_memory_window_is_reachable(&window) ||
        window.size < 2u * NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES)
        return non_volatile_memory_express_fail("its register window is out of reach");

    peripheral_component_interconnect_enable_memory_and_bus_mastering(device);
    peripheral_component_interconnect_disable_pin_interrupt(device);
    if (non_volatile_memory_express_registers == 0u)
        non_volatile_memory_express_registers =
            kernel_vmm_map_device_window((uint32_t) window.base, (uint32_t) window.size);
    if (non_volatile_memory_express_registers == 0u)
        return non_volatile_memory_express_fail("its register window could not be mapped");

    const uint32_t capabilities_low =
        non_volatile_memory_express_read_register(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CAPABILITIES);
    const uint32_t capabilities_high =
        non_volatile_memory_express_read_register(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CAPABILITIES + 4u);

    non_volatile_memory_express_found.version =
        non_volatile_memory_express_read_register(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_VERSION);
    non_volatile_memory_express_found.maximum_queue_entries = (capabilities_low & 0xFFFFu) + 1u;
    non_volatile_memory_express_found.ready_timeout_milliseconds = ((capabilities_low >> 24) & 0xFFu) * 500u;
    non_volatile_memory_express_found.doorbell_stride_bytes = 4u << (capabilities_high & 0xFu);

    if ((capabilities_high & (1u << 5)) == 0u)
        return non_volatile_memory_express_fail("it does not offer the NVM command set");
    if (((capabilities_high >> 16) & 0xFu) != 0u)
        return non_volatile_memory_express_fail("it does not take 4 KiB pages");
    if (non_volatile_memory_express_found.maximum_queue_entries < NON_VOLATILE_MEMORY_EXPRESS_QUEUE_ENTRIES)
        return non_volatile_memory_express_fail("its queues are shorter than this driver's");
    return true;
}

/**
 * @brief Disables the controller, gives it the admin queues, and enables it again.
 *
 * @return false when it does not stop, does not start, or no page is left for the queues.
 */
static bool non_volatile_memory_express_reset(void)
{
    if (!non_volatile_memory_express_prepare_pair(&non_volatile_memory_express_admin,
                                                  NON_VOLATILE_MEMORY_EXPRESS_ADMIN_QUEUE) ||
        !non_volatile_memory_express_claim_page((volatile void **) &non_volatile_memory_express_identify_data,
                                                &non_volatile_memory_express_identify_physical) ||
        !non_volatile_memory_express_claim_page((volatile void **) &non_volatile_memory_express_page_list,
                                                &non_volatile_memory_express_page_list_physical))
        return non_volatile_memory_express_fail("no page left for the queues");

    non_volatile_memory_express_write_register(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CONFIGURATION, 0u);
    if (!non_volatile_memory_express_wait_ready(false))
        return non_volatile_memory_express_fail("it did not stop");

    non_volatile_memory_express_write_register(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_ADMIN_QUEUE_ATTRIBUTES,
                                               ((NON_VOLATILE_MEMORY_EXPRESS_QUEUE_ENTRIES - 1u) << 16) |
                                                   (NON_VOLATILE_MEMORY_EXPRESS_QUEUE_ENTRIES - 1u));
    non_volatile_memory_express_write_register_address(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_ADMIN_SUBMISSION_QUEUE,
                                                       non_volatile_memory_express_admin.submissions_physical);
    non_volatile_memory_express_write_register_address(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_ADMIN_COMPLETION_QUEUE,
                                                       non_volatile_memory_express_admin.completions_physical);
    non_volatile_memory_express_write_register(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CONFIGURATION,
                                               NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_ENTRY_SIZE |
                                                   NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_ENTRY_SIZE |
                                                   NON_VOLATILE_MEMORY_EXPRESS_CONFIGURATION_ENABLE);
    if (!non_volatile_memory_express_wait_ready(true))
        return non_volatile_memory_express_fail("it did not become ready");
    non_volatile_memory_express_found.controller_ready = true;
    return true;
}

/**
 * @brief Identifies the controller, then namespace 1 and the size of its blocks.
 *
 * @return false when either Identify fails, or the block size is not one this driver takes.
 */
static bool non_volatile_memory_express_identify_controller_and_namespace(void)
{
    if (!non_volatile_memory_express_identify(NON_VOLATILE_MEMORY_EXPRESS_IDENTIFY_CONTROLLER, 0u))
        return non_volatile_memory_express_fail("Identify Controller failed");
    non_volatile_memory_express_copy_text(non_volatile_memory_express_found.serial_number,
                                          NON_VOLATILE_MEMORY_EXPRESS_SERIAL_NUMBER_CAPACITY,
                                          NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_SERIAL_NUMBER, 20u);
    non_volatile_memory_express_copy_text(non_volatile_memory_express_found.model_number,
                                          NON_VOLATILE_MEMORY_EXPRESS_MODEL_NUMBER_CAPACITY,
                                          NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_MODEL_NUMBER, 40u);
    non_volatile_memory_express_found.namespace_count =
        non_volatile_memory_express_identify_dword(NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_NAMESPACES);

    const uint32_t transfer_size =
        non_volatile_memory_express_identify_data[NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_TRANSFER_SIZE];
    const uint32_t most_pages = NON_VOLATILE_MEMORY_EXPRESS_TRANSFER_PAGES - 1u;

    non_volatile_memory_express_found.maximum_transfer_bytes =
        (transfer_size == 0u || transfer_size > 7u || (1u << transfer_size) > most_pages ? most_pages :
                                                                                           (1u << transfer_size)) *
        NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES;

    if (!non_volatile_memory_express_identify(NON_VOLATILE_MEMORY_EXPRESS_IDENTIFY_NAMESPACE,
                                              NON_VOLATILE_MEMORY_EXPRESS_FIRST_NAMESPACE))
        return non_volatile_memory_express_fail("Identify Namespace failed");

    const uint32_t format =
        non_volatile_memory_express_identify_data[NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_FORMATTED_SIZE] & 0x0Fu;
    const uint32_t data_size =
        non_volatile_memory_express_identify_data[NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_BLOCK_FORMATS + format * 4u +
                                                  NON_VOLATILE_MEMORY_EXPRESS_BLOCK_FORMAT_DATA_SIZE];

    non_volatile_memory_express_found.namespace_blocks =
        (uint64_t) non_volatile_memory_express_identify_dword(NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_SIZE) |
        ((uint64_t) non_volatile_memory_express_identify_dword(NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_SIZE + 4u) << 32);
    if (data_size < 9u || data_size > 12u)
        return non_volatile_memory_express_fail("namespace 1 has a block size this driver does not take");
    non_volatile_memory_express_found.block_bytes = 1u << data_size;
    non_volatile_memory_express_found.identified = true;
    return true;
}

/**
 * @brief Creates I/O queue pair @p identifier: its completion queue, then its submission queue.
 *
 * @details The completion queue comes first: the submission queue names it. Both are physically
 *          contiguous, one page each, and the completion queue raises no interrupt.
 *
 * @return false when a command fails or no page is left.
 */
static bool non_volatile_memory_express_create_pair(NonVolatileMemoryExpressQueuePair_t *pair, uint32_t identifier)
{
    const uint32_t size_and_identifier = ((NON_VOLATILE_MEMORY_EXPRESS_QUEUE_ENTRIES - 1u) << 16) | identifier;
    uint32_t completion_queue[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS] = {0u};
    uint32_t submission_queue[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS] = {0u};

    if (!non_volatile_memory_express_prepare_pair(pair, identifier))
        return non_volatile_memory_express_fail("no page left for the I/O queues");

    completion_queue[0] = NON_VOLATILE_MEMORY_EXPRESS_OPCODE_CREATE_COMPLETION_QUEUE;
    completion_queue[6] = pair->completions_physical;
    completion_queue[10] = size_and_identifier;
    completion_queue[11] = NON_VOLATILE_MEMORY_EXPRESS_QUEUE_PHYSICALLY_CONTIGUOUS;
    if (non_volatile_memory_express_run(&non_volatile_memory_express_admin, completion_queue, NULL) != 0u)
        return non_volatile_memory_express_fail("Create I/O Completion Queue failed");

    submission_queue[0] = NON_VOLATILE_MEMORY_EXPRESS_OPCODE_CREATE_SUBMISSION_QUEUE;
    submission_queue[6] = pair->submissions_physical;
    submission_queue[10] = size_and_identifier;
    submission_queue[11] = (identifier << 16) | NON_VOLATILE_MEMORY_EXPRESS_QUEUE_PHYSICALLY_CONTIGUOUS;
    if (non_volatile_memory_express_run(&non_volatile_memory_express_admin, submission_queue, NULL) != 0u)
        return non_volatile_memory_express_fail("Create I/O Submission Queue failed");
    return true;
}

/**
 * @brief Asks for the I/O queue pairs, and creates each.
 *
 * @details Each pair is one page per queue: QEMU, like many controllers, requires physically
 *          contiguous queues, and one page is the contiguous memory this kernel can always give.
 *          Depth comes from several pairs instead of one long queue.
 *
 * @return false when the controller allocates fewer pairs, a command fails, or no page is left.
 */
static bool non_volatile_memory_express_create_input_output_queues(void)
{
    const uint32_t wanted = NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_PAIRS - 1u;
    uint32_t features[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS] = {0u};
    uint32_t allocated = 0u;

    features[0] = NON_VOLATILE_MEMORY_EXPRESS_OPCODE_SET_FEATURES;
    features[10] = NON_VOLATILE_MEMORY_EXPRESS_FEATURE_NUMBER_OF_QUEUES;
    features[11] = (wanted << 16) | wanted;
    if (non_volatile_memory_express_run(&non_volatile_memory_express_admin, features, &allocated) != 0u)
        return non_volatile_memory_express_fail("Set Features Number of Queues failed");
    if ((allocated & 0xFFFFu) < wanted || (allocated >> 16) < wanted)
        return non_volatile_memory_express_fail("the controller allocates fewer I/O queues than this driver uses");

    for (uint32_t pair = 0u; pair < NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_PAIRS; ++pair)
        if (!non_volatile_memory_express_create_pair(&non_volatile_memory_express_input_output[pair],
                                                     NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_QUEUE + pair))
            return false;
    non_volatile_memory_express_found.input_output_queues_ready = true;
    return true;
}

bool non_volatile_memory_express_initialize(void)
{
    memset(&non_volatile_memory_express_found, 0, sizeof(non_volatile_memory_express_found));
    return non_volatile_memory_express_find() && non_volatile_memory_express_reset() &&
           non_volatile_memory_express_identify_controller_and_namespace() &&
           non_volatile_memory_express_create_input_output_queues();
}

const NonVolatileMemoryExpressState_t *non_volatile_memory_express_state(void)
{
    return &non_volatile_memory_express_found;
}

bool non_volatile_memory_express_read_blocks(uint64_t first_block, uint32_t block_count, void *buffer)
{
    return non_volatile_memory_express_transfer(NON_VOLATILE_MEMORY_EXPRESS_OPCODE_READ, first_block, block_count,
                                                (uint32_t) (uintptr_t) buffer);
}

bool non_volatile_memory_express_write_blocks(uint64_t first_block, uint32_t block_count, const void *buffer)
{
    return non_volatile_memory_express_transfer(NON_VOLATILE_MEMORY_EXPRESS_OPCODE_WRITE, first_block, block_count,
                                                (uint32_t) (uintptr_t) buffer);
}

uint32_t non_volatile_memory_express_read_batch(NonVolatileMemoryExpressRead_t *reads, uint32_t count)
{
    uint32_t in_flight[NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_PAIRS] = {0u};
    uint32_t submitted = 0u;
    uint32_t completed = 0u;
    uint32_t succeeded = 0u;
    uint32_t next = 0u;
    InterruptRequestDeadline_t deadline;

    if (reads == NULL || count > NON_VOLATILE_MEMORY_EXPRESS_MOST_BLOCKS)
        return 0u;
    for (uint32_t i = 0u; i < count; ++i)
        reads[i].done = false;

    interrupt_request_deadline_start(&deadline, NON_VOLATILE_MEMORY_EXPRESS_COMMAND_MICROSECONDS);
    while (next < count || completed < submitted)
    {
        for (; next < count; ++next)
        {
            NonVolatileMemoryExpressRead_t *const read = &reads[next];
            const uint32_t pair = next % NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_PAIRS;
            const uint32_t buffer = (uint32_t) (uintptr_t) read->buffer;
            const uint32_t pages = ((buffer & (NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES - 1u)) +
                                    read->block_count * non_volatile_memory_express_found.block_bytes +
                                    NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES - 1u) /
                                   NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES;
            uint32_t command[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS] = {0u};

            if (in_flight[pair] == NON_VOLATILE_MEMORY_EXPRESS_QUEUE_ENTRIES - 1u)
                break;
            if (pages > 2u ||
                !non_volatile_memory_express_build_transfer(command, NON_VOLATILE_MEMORY_EXPRESS_OPCODE_READ,
                                                            read->first_block, read->block_count, buffer))
                continue;
            command[0] |= next << 16;
            non_volatile_memory_express_place(&non_volatile_memory_express_input_output[pair], command);
            ++in_flight[pair];
            ++submitted;
        }
        for (uint32_t pair = 0u; pair < NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_PAIRS; ++pair)
            non_volatile_memory_express_write_register(
                non_volatile_memory_express_input_output[pair].submission_doorbell,
                non_volatile_memory_express_input_output[pair].tail);
        if (submitted - completed > non_volatile_memory_express_found.most_in_flight)
            non_volatile_memory_express_found.most_in_flight = submitted - completed;

        bool took = false;
        for (uint32_t pair = 0u; pair < NON_VOLATILE_MEMORY_EXPRESS_INPUT_OUTPUT_PAIRS; ++pair)
        {
            uint32_t dword3 = 0u;

            while (non_volatile_memory_express_take(&non_volatile_memory_express_input_output[pair], NULL, &dword3))
            {
                const uint32_t identifier = dword3 & 0xFFFFu;

                if (identifier < count && ((dword3 >> 17) & 0x7FFu) == 0u)
                {
                    reads[identifier].done = true;
                    ++succeeded;
                }
                --in_flight[pair];
                ++completed;
                took = true;
            }
        }
        if (took)
            interrupt_request_deadline_start(&deadline, NON_VOLATILE_MEMORY_EXPRESS_COMMAND_MICROSECONDS);
        else if (interrupt_request_deadline_passed(&deadline))
            break;
        else
            asmutils_pause();
    }
    return succeeded;
}
