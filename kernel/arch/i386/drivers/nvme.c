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
#define NON_VOLATILE_MEMORY_EXPRESS_REGISTER_DOORBELLS              0x1000u /**< The admin submission tail's. */
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
 * @name The admin queue pair
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_ADMIN_ENTRIES        64u      /**< Entries in each admin queue. */
#define NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS    16u      /**< Dwords in a submission entry. */
#define NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_DWORDS    4u       /**< Dwords in a completion entry. */
#define NON_VOLATILE_MEMORY_EXPRESS_COMMAND_MICROSECONDS 1000000u /**< How long a command may take. */
/** @} */

/**
 * @name Admin commands and their fields
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_OPCODE_IDENTIFY      0x06u      /**< Identify. */
#define NON_VOLATILE_MEMORY_EXPRESS_IDENTIFY_NAMESPACE   0x00u      /**< CNS 00h: a namespace. */
#define NON_VOLATILE_MEMORY_EXPRESS_IDENTIFY_CONTROLLER  0x01u      /**< CNS 01h: the controller. */
#define NON_VOLATILE_MEMORY_EXPRESS_FIRST_NAMESPACE      1u         /**< The namespace identified. */
#define NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_PHASE     (1u << 16) /**< Phase tag, in dword 3. */
#define NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_TIMED_OUT 0xFFFFu    /**< No status: the command never completed. */
/** @} */

/**
 * @name Offsets in the Identify data
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_SERIAL_NUMBER 4u   /**< SN, 20 bytes of ASCII. */
#define NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_MODEL_NUMBER  24u  /**< MN, 40 bytes of ASCII. */
#define NON_VOLATILE_MEMORY_EXPRESS_CONTROLLER_NAMESPACES    516u /**< NN, a dword. */
#define NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_SIZE           0u   /**< NSZE, a qword. */
#define NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_FORMATTED_SIZE 26u  /**< FLBAS. */
#define NON_VOLATILE_MEMORY_EXPRESS_NAMESPACE_BLOCK_FORMATS  128u /**< LBAF0, then one dword per format. */
#define NON_VOLATILE_MEMORY_EXPRESS_BLOCK_FORMAT_DATA_SIZE   2u   /**< LBADS, a power of two, in the format. */
/** @} */

/** Bytes of a memory page, as the controller is told: 2^(12 + CC.MPS) with MPS zero. */
#define NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES 4096u

static NonVolatileMemoryExpressState_t non_volatile_memory_express_found;
static uint32_t non_volatile_memory_express_registers = 0u;
static volatile uint32_t *non_volatile_memory_express_admin_submissions = NULL;
static volatile uint32_t *non_volatile_memory_express_admin_completions = NULL;
static volatile uint8_t *non_volatile_memory_express_identify_data = NULL;
static uint32_t non_volatile_memory_express_admin_submissions_physical = 0u;
static uint32_t non_volatile_memory_express_admin_completions_physical = 0u;
static uint32_t non_volatile_memory_express_identify_physical = 0u;
static uint32_t non_volatile_memory_express_admin_tail = 0u;
static uint32_t non_volatile_memory_express_admin_head = 0u;
static uint32_t non_volatile_memory_express_admin_phase = 0u;
static uint16_t non_volatile_memory_express_next_command = 0u;

/**
 * @brief Reads a 32-bit controller register.
 */
static uint32_t non_volatile_memory_express_read(uint32_t offset)
{
    return *(volatile const uint32_t *) (uintptr_t) (non_volatile_memory_express_registers + offset);
}

/**
 * @brief Writes a 32-bit controller register.
 */
static void non_volatile_memory_express_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *) (uintptr_t) (non_volatile_memory_express_registers + offset) = value;
}

/**
 * @brief Writes a 64-bit controller register whose high half is zero, low half first.
 *
 * @details This kernel's physical addresses fit in 32 bits. Two 32-bit writes, low then high, are
 *          what Linux does on a 32-bit host (lo_hi_writeq).
 */
static void non_volatile_memory_express_write_address(uint32_t offset, uint32_t low)
{
    non_volatile_memory_express_write(offset, low);
    non_volatile_memory_express_write(offset + 4u, 0u);
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
        const uint32_t status = non_volatile_memory_express_read(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_STATUS);

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
 * @brief Submits one admin command and polls its completion.
 *
 * @details The entry is written, then the tail doorbell: both are volatile stores, which the
 *          compiler keeps in order and an x86 processor makes visible in order, so the controller
 *          never fetches an entry before it is whole. The completion is the entry at the head whose
 *          phase tag is the one this pass of the queue expects; the head doorbell then frees it.
 *
 * @param command The 16 dwords of the command; its identifier is filled in here.
 * @return The status field of the completion, 0 on success, or
 *         NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_TIMED_OUT.
 */
static uint32_t non_volatile_memory_express_run_admin(uint32_t command[NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS])
{
    volatile uint32_t *const entry =
        non_volatile_memory_express_admin_submissions +
        non_volatile_memory_express_admin_tail * NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS;
    volatile const uint32_t *const completion =
        non_volatile_memory_express_admin_completions +
        non_volatile_memory_express_admin_head * NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_DWORDS;
    InterruptRequestDeadline_t deadline;

    command[0] = (command[0] & 0xFFFFu) | ((uint32_t) non_volatile_memory_express_next_command++ << 16);
    for (uint32_t i = 0u; i < NON_VOLATILE_MEMORY_EXPRESS_SUBMISSION_DWORDS; ++i)
        entry[i] = command[i];
    non_volatile_memory_express_admin_tail =
        (non_volatile_memory_express_admin_tail + 1u) % NON_VOLATILE_MEMORY_EXPRESS_ADMIN_ENTRIES;
    non_volatile_memory_express_write(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_DOORBELLS,
                                      non_volatile_memory_express_admin_tail);

    interrupt_request_deadline_start(&deadline, NON_VOLATILE_MEMORY_EXPRESS_COMMAND_MICROSECONDS);
    while ((completion[3] & NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_PHASE) != non_volatile_memory_express_admin_phase)
    {
        if (interrupt_request_deadline_passed(&deadline))
            return NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_TIMED_OUT;
        asmutils_pause();
    }

    const uint32_t status = (completion[3] >> 17) & 0x7FFu;

    non_volatile_memory_express_admin_head =
        (non_volatile_memory_express_admin_head + 1u) % NON_VOLATILE_MEMORY_EXPRESS_ADMIN_ENTRIES;
    if (non_volatile_memory_express_admin_head == 0u)
        non_volatile_memory_express_admin_phase ^= NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_PHASE;
    non_volatile_memory_express_write(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_DOORBELLS +
                                          non_volatile_memory_express_found.doorbell_stride_bytes,
                                      non_volatile_memory_express_admin_head);
    return status;
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
    return non_volatile_memory_express_run_admin(command) == 0u;
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
        window.is_io || (window.base >> 32) != 0u || window.size < 2u * NON_VOLATILE_MEMORY_EXPRESS_PAGE_BYTES)
        return non_volatile_memory_express_fail("its register window is out of reach");

    peripheral_component_interconnect_enable_memory_and_bus_mastering(device);
    if (non_volatile_memory_express_registers == 0u)
        non_volatile_memory_express_registers =
            kernel_vmm_map_device_window((uint32_t) window.base, (uint32_t) window.size);
    if (non_volatile_memory_express_registers == 0u)
        return non_volatile_memory_express_fail("its register window could not be mapped");

    const uint32_t capabilities_low =
        non_volatile_memory_express_read(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CAPABILITIES);
    const uint32_t capabilities_high =
        non_volatile_memory_express_read(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CAPABILITIES + 4u);

    non_volatile_memory_express_found.version =
        non_volatile_memory_express_read(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_VERSION);
    non_volatile_memory_express_found.maximum_queue_entries = (capabilities_low & 0xFFFFu) + 1u;
    non_volatile_memory_express_found.ready_timeout_milliseconds = ((capabilities_low >> 24) & 0xFFu) * 500u;
    non_volatile_memory_express_found.doorbell_stride_bytes = 4u << (capabilities_high & 0xFu);

    if ((capabilities_high & (1u << 5)) == 0u)
        return non_volatile_memory_express_fail("it does not offer the NVM command set");
    if (((capabilities_high >> 16) & 0xFu) != 0u)
        return non_volatile_memory_express_fail("it does not take 4 KiB pages");
    if (non_volatile_memory_express_found.maximum_queue_entries < NON_VOLATILE_MEMORY_EXPRESS_ADMIN_ENTRIES)
        return non_volatile_memory_express_fail("its queues are shorter than the admin queues");
    return true;
}

/**
 * @brief Disables the controller, gives it the admin queues, and enables it again.
 *
 * @return false when it does not stop, does not start, or no page is left for the queues.
 */
static bool non_volatile_memory_express_reset(void)
{
    if (!non_volatile_memory_express_claim_page((volatile void **) &non_volatile_memory_express_admin_submissions,
                                                &non_volatile_memory_express_admin_submissions_physical) ||
        !non_volatile_memory_express_claim_page((volatile void **) &non_volatile_memory_express_admin_completions,
                                                &non_volatile_memory_express_admin_completions_physical) ||
        !non_volatile_memory_express_claim_page((volatile void **) &non_volatile_memory_express_identify_data,
                                                &non_volatile_memory_express_identify_physical))
        return non_volatile_memory_express_fail("no page left for the queues");

    non_volatile_memory_express_write(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CONFIGURATION, 0u);
    if (!non_volatile_memory_express_wait_ready(false))
        return non_volatile_memory_express_fail("it did not stop");

    non_volatile_memory_express_admin_tail = 0u;
    non_volatile_memory_express_admin_head = 0u;
    non_volatile_memory_express_admin_phase = NON_VOLATILE_MEMORY_EXPRESS_COMPLETION_PHASE;
    non_volatile_memory_express_write(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_ADMIN_QUEUE_ATTRIBUTES,
                                      ((NON_VOLATILE_MEMORY_EXPRESS_ADMIN_ENTRIES - 1u) << 16) |
                                          (NON_VOLATILE_MEMORY_EXPRESS_ADMIN_ENTRIES - 1u));
    non_volatile_memory_express_write_address(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_ADMIN_SUBMISSION_QUEUE,
                                              non_volatile_memory_express_admin_submissions_physical);
    non_volatile_memory_express_write_address(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_ADMIN_COMPLETION_QUEUE,
                                              non_volatile_memory_express_admin_completions_physical);
    non_volatile_memory_express_write(NON_VOLATILE_MEMORY_EXPRESS_REGISTER_CONFIGURATION,
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

bool non_volatile_memory_express_initialize(void)
{
    memset(&non_volatile_memory_express_found, 0, sizeof(non_volatile_memory_express_found));
    return non_volatile_memory_express_find() && non_volatile_memory_express_reset() &&
           non_volatile_memory_express_identify_controller_and_namespace();
}

const NonVolatileMemoryExpressState_t *non_volatile_memory_express_state(void)
{
    return &non_volatile_memory_express_found;
}
