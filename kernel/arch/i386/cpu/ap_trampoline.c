/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** AP trampoline in low memory (real-mode SIPI target)
*/

#include <kernel/cpu/ap_trampoline.h>

#include <stdint.h>

#define KERNEL_VIRTUAL_BASE                  0xC0000000u
#define AP_TRAMPOLINE_PHYSICAL_BASE          0x00008000u
#define AP_TRAMPOLINE_STARTUP_VECTOR         0x08u
#define AP_TRAMPOLINE_MAILBOX_PHYSICAL       0x00007D00u
#define AP_TRAMPOLINE_ACK_PHYSICAL_WORD      0x00007DF0u
#define AP_TRAMPOLINE_C_ENTRY_PHYSICAL_WORD  0x00007DF2u
#define AP_TRAMPOLINE_ACK_MAGIC              0x4150u
#define AP_TRAMPOLINE_C_ENTRY_MAGIC          0x4350u

#define AP_TRAMPOLINE_MAILBOX_APIC_ID_OFFSET      4u
#define AP_TRAMPOLINE_MAILBOX_CR3_OFFSET          8u
#define AP_TRAMPOLINE_MAILBOX_STACK_TOP_OFFSET    12u
#define AP_TRAMPOLINE_MAILBOX_C_ENTRY_OFFSET      16u
#define AP_TRAMPOLINE_MAILBOX_LOGICAL_SLOT_OFFSET 20u
#define AP_TRAMPOLINE_MAILBOX_MAIN_LOOP_OFFSET    24u

extern const uint8_t ap_trampoline_blob_start[];
extern const uint8_t ap_trampoline_blob_end[];

static uint8_t ap_trampoline_installed = 0u;
static uint32_t ap_trampoline_install_count = 0u;
static uint32_t ap_trampoline_ack_wait_count = 0u;
static uint32_t ap_trampoline_ack_success_count = 0u;
static uint32_t ap_trampoline_ack_timeout_count = 0u;
static uint32_t ap_trampoline_c_entry_wait_count = 0u;
static uint32_t ap_trampoline_c_entry_success_count = 0u;
static uint32_t ap_trampoline_c_entry_timeout_count = 0u;

static volatile uint16_t *ap_trampoline_ack_word_pointer(void)
{
    uintptr_t ack_virtual = (uintptr_t) (AP_TRAMPOLINE_ACK_PHYSICAL_WORD + KERNEL_VIRTUAL_BASE);

    return (volatile uint16_t *) ack_virtual;
}

static volatile uint16_t *ap_trampoline_c_entry_word_pointer(void)
{
    uintptr_t marker_virtual = (uintptr_t) (AP_TRAMPOLINE_C_ENTRY_PHYSICAL_WORD + KERNEL_VIRTUAL_BASE);

    return (volatile uint16_t *) marker_virtual;
}

static volatile uint32_t *ap_trampoline_mailbox_word_pointer(uint32_t offset)
{
    uintptr_t mailbox_virtual =
        (uintptr_t) (AP_TRAMPOLINE_MAILBOX_PHYSICAL + offset + KERNEL_VIRTUAL_BASE);

    return (volatile uint32_t *) mailbox_virtual;
}

uint8_t application_processor_trampoline_install(void)
{
    volatile uint8_t *destination =
        (volatile uint8_t *) (uintptr_t) (AP_TRAMPOLINE_PHYSICAL_BASE + KERNEL_VIRTUAL_BASE);
    uint32_t trampoline_size = (uint32_t) (ap_trampoline_blob_end - ap_trampoline_blob_start);

    for (uint32_t i = 0u; i < trampoline_size; ++i)
        destination[i] = ap_trampoline_blob_start[i];

    ap_trampoline_installed = 1u;
    ++ap_trampoline_install_count;
    application_processor_trampoline_reset_acknowledgement();
    *ap_trampoline_c_entry_word_pointer() = 0u;
    return 1u;
}

uint8_t application_processor_trampoline_is_installed(void)
{
    return ap_trampoline_installed;
}

uint8_t application_processor_trampoline_get_startup_vector(void)
{
    return AP_TRAMPOLINE_STARTUP_VECTOR;
}

void application_processor_trampoline_reset_acknowledgement(void)
{
    *ap_trampoline_ack_word_pointer() = 0u;
    *ap_trampoline_c_entry_word_pointer() = 0u;
}

uint8_t application_processor_trampoline_wait_for_acknowledgement(uint32_t spin_limit)
{
    volatile uint16_t *ack = ap_trampoline_ack_word_pointer();

    ++ap_trampoline_ack_wait_count;

    for (uint32_t spin = 0u; spin < spin_limit; ++spin)
    {
        if (*ack == AP_TRAMPOLINE_ACK_MAGIC)
        {
            ++ap_trampoline_ack_success_count;
            return 1u;
        }
    }

    ++ap_trampoline_ack_timeout_count;

    return 0u;
}

uint16_t application_processor_trampoline_get_acknowledgement_word(void)
{
    return *ap_trampoline_ack_word_pointer();
}

void application_processor_trampoline_configure_handoff(uint8_t apic_id, uint32_t logical_slot,
                                     uint32_t stack_top_virtual, uint32_t c_entry_virtual,
                                     uint32_t main_loop_virtual, uint32_t cr3_physical)
{
    *ap_trampoline_mailbox_word_pointer(AP_TRAMPOLINE_MAILBOX_APIC_ID_OFFSET) = (uint32_t) apic_id;
    *ap_trampoline_mailbox_word_pointer(AP_TRAMPOLINE_MAILBOX_CR3_OFFSET) = cr3_physical;
    *ap_trampoline_mailbox_word_pointer(AP_TRAMPOLINE_MAILBOX_STACK_TOP_OFFSET) = stack_top_virtual;
    *ap_trampoline_mailbox_word_pointer(AP_TRAMPOLINE_MAILBOX_C_ENTRY_OFFSET) = c_entry_virtual;
    *ap_trampoline_mailbox_word_pointer(AP_TRAMPOLINE_MAILBOX_LOGICAL_SLOT_OFFSET) = logical_slot;
    *ap_trampoline_mailbox_word_pointer(AP_TRAMPOLINE_MAILBOX_MAIN_LOOP_OFFSET) = main_loop_virtual;
}

uint8_t application_processor_trampoline_wait_for_c_entry(uint32_t spin_limit)
{
    volatile uint16_t *marker = ap_trampoline_c_entry_word_pointer();

    ++ap_trampoline_c_entry_wait_count;

    for (uint32_t spin = 0u; spin < spin_limit; ++spin)
    {
        if (*marker == AP_TRAMPOLINE_C_ENTRY_MAGIC)
        {
            ++ap_trampoline_c_entry_success_count;
            return 1u;
        }
    }

    ++ap_trampoline_c_entry_timeout_count;
    return 0u;
}

uint16_t application_processor_trampoline_get_c_entry_word(void)
{
    return *ap_trampoline_c_entry_word_pointer();
}

uint32_t application_processor_trampoline_get_install_count(void)
{
    return ap_trampoline_install_count;
}

uint32_t application_processor_trampoline_get_ack_wait_count(void)
{
    return ap_trampoline_ack_wait_count;
}

uint32_t application_processor_trampoline_get_ack_success_count(void)
{
    return ap_trampoline_ack_success_count;
}

uint32_t application_processor_trampoline_get_ack_timeout_count(void)
{
    return ap_trampoline_ack_timeout_count;
}

uint32_t application_processor_trampoline_get_c_entry_wait_count(void)
{
    return ap_trampoline_c_entry_wait_count;
}

uint32_t application_processor_trampoline_get_c_entry_success_count(void)
{
    return ap_trampoline_c_entry_success_count;
}

uint32_t application_processor_trampoline_get_c_entry_timeout_count(void)
{
    return ap_trampoline_c_entry_timeout_count;
}
