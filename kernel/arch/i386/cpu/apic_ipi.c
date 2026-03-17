/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** APIC Inter-Processor Interrupt (IPI) framework
*/

#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/paging.h>
#include <stddef.h>
#include <stdint.h>

/* LAPIC register offsets (relative to LAPIC MMIO base) */
#define LAPIC_ICR_LOW_OFFSET  0x300u
#define LAPIC_ICR_HIGH_OFFSET 0x310u

/* LAPIC ICR (Interrupt Command Register) bit definitions */
#define LAPIC_ICR_VECTOR_MASK          0x000000FFu
#define LAPIC_ICR_DELIVERY_MODE_MASK   0x00000700u
#define LAPIC_ICR_DELIVERY_MODE_FIXED  (0u << 8)
#define LAPIC_ICR_DELIVERY_MODE_LOWEST (1u << 8)
#define LAPIC_ICR_DELIVERY_MODE_SMI    (2u << 8)
#define LAPIC_ICR_DELIVERY_MODE_NMI    (4u << 8)
#define LAPIC_ICR_DELIVERY_MODE_INIT   (5u << 8)
#define LAPIC_ICR_DELIVERY_MODE_SIPI   (6u << 8)
#define LAPIC_ICR_DEST_MODE_PHYSICAL   (0u << 11)
#define LAPIC_ICR_DEST_MODE_LOGICAL    (1u << 11)
#define LAPIC_ICR_DELIVERY_STATUS      (1u << 12)
#define LAPIC_ICR_LEVEL_MASK           (1u << 14)
#define LAPIC_ICR_LEVEL_ASSERT         (1u << 14)
#define LAPIC_ICR_LEVEL_DEASSERT       0u
#define LAPIC_ICR_TRIGGER_MODE_EDGE    0u
#define LAPIC_ICR_TRIGGER_MODE_LEVEL   (1u << 15)
#define LAPIC_ICR_DEST_SHORT_MASK      (3u << 18)
#define LAPIC_ICR_DEST_SHORT_NONE      0u
#define LAPIC_ICR_DEST_SHORT_SELF      (1u << 18)
#define LAPIC_ICR_DEST_SHORT_ALL_INCL  (2u << 18)
#define LAPIC_ICR_DEST_SHORT_ALL_EXCL  (3u << 18)

/* Cached LAPIC MMIO base from ACPI/MADT */
static uint32_t apic_ipi_lapic_base = 0u;
static uint32_t apic_ipi_init_attempt_count = 0u;
static uint32_t apic_ipi_sipi_attempt_count = 0u;
static uint32_t apic_ipi_startup_sequence_attempt_count = 0u;
static uint32_t apic_ipi_startup_sequence_success_count = 0u;

/* TLB Shootdown synchronization */
static volatile uint32_t apic_ipi_tlb_shootdown_addr = 0u;
static volatile uint32_t apic_ipi_tlb_shootdown_pending = 0u;

/* Private IPI handler for TLB shootdown */
static void apic_ipi_tlb_shootdown_handler(const InterruptFrame_t *frame)
{
    (void) frame;
    if (apic_ipi_tlb_shootdown_addr == 0xFFFFFFFFu)
    {
        /* Complete flush */
        paging_flush_tlb();
    }
    else
    {
        /* Single page invalidate */
        paging_invlpg(apic_ipi_tlb_shootdown_addr);
    }
    __sync_fetch_and_sub(&apic_ipi_tlb_shootdown_pending, 1u);
}

/**
 * @brief Initialize IPI framework with LAPIC MMIO base.
 *
 * Must be called after APIC late-init (which maps LAPIC MMIO).
 *
 * @param lapic_phys_base Physical address of LAPIC MMIO (e.g., 0xFEE00000).
 */
void advanced_pic_ipi_initialize(uint32_t lapic_virtual_base)
{
    if (!lapic_virtual_base)
    {
        apic_ipi_lapic_base = 0u;
        return;
    }

    apic_ipi_lapic_base = lapic_virtual_base;

    /* Register TLB shootdown handler on vector 0x40 (fixed for this kernel) */
    interrupt_service_routine_register_handler(0x40u, apic_ipi_tlb_shootdown_handler);
}

uint8_t advanced_pic_ipi_is_ready(void) { return apic_ipi_lapic_base != 0u; }

void advanced_pic_ipi_enable_local_apic(void)
{
    /* Already handled by apic_initialize_on_cpu inside timer backend late-init */
    /* But for APs, we call it in ap_startup.c */
    uint32_t svr = apic_read(LAPIC_REG_SPURIOUS);
    apic_write(LAPIC_REG_SPURIOUS, svr | (1u << 8u));
}

static uint8_t advanced_pic_ipi_wait_delivery(void)
{
    if (apic_is_x2apic_active())
        return 1u; /* x2APIC doesn't need to wait for delivery status in most cases */

    /* Poll delivery status (bit 12) for xAPIC */
    for (uint32_t i = 0u; i < 100000u; ++i)
    {
        if ((apic_read(LAPIC_REG_ICR_LOW) & (1u << 12u)) == 0u)
            return 1u;
    }
    return 0u;
}

uint8_t advanced_pic_ipi_send_init(uint8_t apic_id)
{
    if (!advanced_pic_ipi_wait_delivery())
        return 0u;

    uint32_t low = 0x00000500u | (1u << 14u) | (1u << 15u); /* INIT + Assert + Level */
    uint32_t high = (uint32_t) apic_id << 24u;

    apic_write_icr(high, low);
    return advanced_pic_ipi_wait_delivery();
}

uint8_t advanced_pic_ipi_send_sipi(uint8_t apic_id, uint8_t startup_vector)
{
    if (!advanced_pic_ipi_wait_delivery())
        return 0u;

    uint32_t low = (uint32_t) startup_vector | 0x00000600u; /* SIPI */
    uint32_t high = (uint32_t) apic_id << 24u;

    apic_write_icr(high, low);
    return advanced_pic_ipi_wait_delivery();
}

uint8_t advanced_pic_ipi_send_startup_sequence(uint8_t apic_id, uint8_t startup_vector)
{
    if (!advanced_pic_ipi_send_init(apic_id))
        return 0u;

    for (volatile uint32_t delay = 0u; delay < 10000u; ++delay)
        ;

    if (!advanced_pic_ipi_send_sipi(apic_id, startup_vector))
        return 0u;

    for (volatile uint32_t delay = 0u; delay < 10000u; ++delay)
        ;

    if (!advanced_pic_ipi_send_sipi(apic_id, startup_vector))
        return 0u;

    return 1u;
}

uint32_t advanced_pic_ipi_get_init_attempt_count(void) { return apic_ipi_init_attempt_count; }

uint32_t advanced_pic_ipi_get_sipi_attempt_count(void) { return apic_ipi_sipi_attempt_count; }

uint32_t advanced_pic_ipi_get_startup_sequence_attempt_count(void) { return apic_ipi_startup_sequence_attempt_count; }

uint32_t advanced_pic_ipi_get_startup_sequence_success_count(void) { return apic_ipi_startup_sequence_success_count; }

uint8_t advanced_pic_ipi_send_fixed(uint8_t apic_id, uint8_t vector, uint8_t shorthand)
{
    if (!advanced_pic_ipi_wait_delivery())
        return 0u;

    uint32_t low = (uint32_t) vector | ((uint32_t) shorthand << 18u);
    uint32_t high = (shorthand == 0) ? ((uint32_t) apic_id << 24u) : 0u;

    apic_write_icr(high, low);
    return advanced_pic_ipi_wait_delivery();
}

void advanced_pic_ipi_broadcast_tlb_shootdown(uint32_t virt_addr)
{
    uint32_t cpu_count = cpu_topology_get_online_cpu_count();

    if (cpu_count <= 1u)
    {
        paging_invlpg(virt_addr);
        return;
    }

    apic_ipi_tlb_shootdown_addr = virt_addr;
    apic_ipi_tlb_shootdown_pending = cpu_count - 1u;

    advanced_pic_ipi_send_fixed(0u, 0x40u, 3u); /* 3 = ALL EXCLUDING SELF */

    /* Wait for acknowledgement from other APs */
    while (apic_ipi_tlb_shootdown_pending > 0u)
    {
        __asm__ volatile("pause");
    }

    paging_invlpg(virt_addr);
}

void advanced_pic_ipi_broadcast_tlb_flush(void) { advanced_pic_ipi_broadcast_tlb_shootdown(0xFFFFFFFFu); }
