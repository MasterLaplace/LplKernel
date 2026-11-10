#include <kernel/power/wake_planner.h>

#include <stddef.h>

_Static_assert(KERNEL_WAKE_PLANNER_DUTIES < 32u, "one bit per duty in a 32-bit mask, and a full mask below it");

/**
 * @brief The bit of a duty number in the plan's masks, or 0 for a number that names no slot.
 * @param duty A duty number, from 1.
 * @return Its bit.
 */
static uint32_t wake_planner_bit(uint32_t duty)
{
    if (duty == KERNEL_WAKE_PLANNER_NO_DUTY || duty > KERNEL_WAKE_PLANNER_DUTIES)
        return 0u;
    return 1u << (duty - 1u);
}

/**
 * @brief The duties of @p candidates whose window has opened by @p now.
 * @param plan       The plan.
 * @param candidates The armed duties, one bit each.
 * @param now        The wake-up's cycle.
 * @return Their bits.
 */
static uint32_t wake_planner_open_windows(const KernelWakePlan_t *plan, uint32_t candidates, uint64_t now)
{
    uint32_t open = 0u;

    while (candidates != 0u)
    {
        const uint32_t slot = (uint32_t) __builtin_ctz(candidates);
        candidates &= candidates - 1u;
        if (plan->earliest[slot] <= now)
            open |= 1u << slot;
    }
    return open;
}

/**
 * @brief The slot of @p candidates with the earliest latest, the lowest slot among equals.
 * @param plan       The plan.
 * @param candidates A non-empty set of slots, one bit each.
 * @return That slot.
 */
static uint32_t wake_planner_most_urgent(const KernelWakePlan_t *plan, uint32_t candidates)
{
    uint32_t chosen = (uint32_t) __builtin_ctz(candidates);

    for (candidates &= candidates - 1u; candidates != 0u; candidates &= candidates - 1u)
    {
        const uint32_t slot = (uint32_t) __builtin_ctz(candidates);
        if (plan->latest[slot] < plan->latest[chosen])
            chosen = slot;
    }
    return chosen;
}

/**
 * @brief When to arm a wake-up that comes @p lead cycles late so that it serves a slot by its latest: that latest
 *        less the lead, never before the slot's earliest.
 * @param plan The plan.
 * @param slot An armed slot whose latest is a cycle.
 * @param lead The lead.
 * @return That cycle.
 */
static uint64_t wake_planner_arming(const KernelWakePlan_t *plan, uint32_t slot, uint64_t lead)
{
    const uint64_t latest = plan->latest[slot];
    const uint64_t earliest = plan->earliest[slot];

    return latest - earliest <= lead ? earliest : latest - lead;
}

/**
 * @brief Counts a duty run @p overshoot cycles past its latest, and keeps the worst such overshoot.
 * @param plan      The plan.
 * @param overshoot Cycles past the latest; never zero here.
 */
static void wake_planner_count_late(KernelWakePlan_t *plan, uint64_t overshoot)
{
    ++plan->late_services;
    if (overshoot > plan->latest_overshoot)
        plan->latest_overshoot = overshoot;
}

void kernel_wake_planner_initialize(KernelWakePlan_t *plan)
{
    for (uint32_t slot = 0u; slot < KERNEL_WAKE_PLANNER_DUTIES; ++slot)
    {
        plan->earliest[slot] = 0u;
        plan->latest[slot] = KERNEL_WAKE_PLANNER_NEVER;
        plan->duties[slot] = NULL;
        plan->contexts[slot] = NULL;
    }
    plan->declared = 0u;
    plan->armed = 0u;
    plan->waking = 0u;
    plan->wakes = 0u;
    plan->wasted_wakes = 0u;
    plan->batched_wakes = 0u;
    plan->services = 0u;
    plan->late_services = 0u;
    plan->latest_overshoot = 0u;
}

uint32_t kernel_wake_planner_declare(KernelWakePlan_t *plan, KernelWakePlannerDuty_t duty, void *context)
{
    const uint32_t free_slots = ~plan->declared & ((1u << KERNEL_WAKE_PLANNER_DUTIES) - 1u);

    if (duty == NULL || free_slots == 0u)
        return KERNEL_WAKE_PLANNER_NO_DUTY;

    const uint32_t slot = (uint32_t) __builtin_ctz(free_slots);
    plan->duties[slot] = duty;
    plan->contexts[slot] = context;
    plan->declared |= 1u << slot;
    return slot + 1u;
}

void kernel_wake_planner_withdraw(KernelWakePlan_t *plan, uint32_t duty)
{
    const uint32_t bit = wake_planner_bit(duty);

    plan->declared &= ~bit;
    plan->armed &= ~bit;
}

bool kernel_wake_planner_arm(KernelWakePlan_t *plan, uint32_t duty, uint64_t earliest, uint64_t latest)
{
    const uint32_t bit = wake_planner_bit(duty);

    if ((plan->declared & bit) == 0u || latest < earliest)
        return false;

    plan->earliest[duty - 1u] = earliest;
    plan->latest[duty - 1u] = latest;
    plan->armed |= bit;
    if (latest == KERNEL_WAKE_PLANNER_NEVER)
        plan->waking &= ~bit;
    else
        plan->waking |= bit;
    return true;
}

uint64_t kernel_wake_planner_next_wake(const KernelWakePlan_t *plan, uint64_t lead)
{
    uint64_t next = KERNEL_WAKE_PLANNER_NEVER;

    for (uint32_t waking = plan->armed & plan->waking; waking != 0u; waking &= waking - 1u)
    {
        const uint32_t slot = (uint32_t) __builtin_ctz(waking);
        const uint64_t arming = wake_planner_arming(plan, slot, lead);
        if (arming < next)
            next = arming;
    }
    return next;
}

uint32_t kernel_wake_planner_serve(KernelWakePlan_t *plan, uint64_t now)
{
    uint32_t open = wake_planner_open_windows(plan, plan->armed, now);
    uint32_t served = 0u;

    plan->armed &= ~open;
    ++plan->wakes;

    for (open &= plan->declared; open != 0u; open &= plan->declared)
    {
        const uint32_t slot = wake_planner_most_urgent(plan, open);
        open &= ~(1u << slot);
        if (now > plan->latest[slot])
            wake_planner_count_late(plan, now - plan->latest[slot]);
        ++served;
        plan->duties[slot](plan->contexts[slot], now);
    }

    plan->services += served;
    if (served == 0u)
        ++plan->wasted_wakes;
    else if (served > 1u)
        ++plan->batched_wakes;
    return served;
}
