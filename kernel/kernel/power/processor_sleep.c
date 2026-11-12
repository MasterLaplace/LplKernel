#include <kernel/power/processor_sleep.h>

#include <kernel/acpi/host.h>
#include <kernel/acpi/initialization.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/tables.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/cpu_identity.h>

#include <kernel/lib/asmutils.h>
#include <kernel/memory/spsc_ring.h>
#include <kernel/power/wakeup_accounting.h>

#include <string.h>

/** CPUID leaf 1, ECX bit 3: the processor implements MONITOR and MWAIT. */
#define PROCESSOR_SLEEP_CPUID_MONITOR_BIT (1u << 3)

/** CPUID leaf 5, EBX low 16 bits: the largest line the monitor watches. */
#define PROCESSOR_SLEEP_MONITOR_LEAF 5u

/** The bits of leaf 5's EBX that hold that line size. */
#define PROCESSOR_SLEEP_MONITOR_LINE_BITS 0xFFFFu

/**
 * MWAIT extension bit 0: treat interrupts as break events even when MASKED.
 *
 * Gated on CPUID.05H:ECX bit 1, which raises #GP when the bit is set without it. Dropping
 * it costs nothing on these paths: each MWAIT comes right after the STI that unmasks
 * interrupts, so a pending one ends it all the same. Linux passes the bit only to a
 * MWAIT entered masked (mwait_idle_with_hints, arch/x86/include/asm/mwait.h, v6.12).
 */
#define PROCESSOR_SLEEP_BREAK_ON_INTERRUPT 1u

/** CPUID leaf 5, ECX bit 1: MWAIT may be given the break-on-masked-interrupt extension. */
#define PROCESSOR_SLEEP_CPUID_INTERRUPT_BREAK_BIT (1u << 1)

/**
 * @name The families and models Linux's vendor rule for C3 names (v6.12 `acpi_processor_power_init_bm_check`)
 * @{
 */
#define PROCESSOR_SLEEP_FAMILY_6             6u    /**< The P6 family, Intel's and Centaur's. */
#define PROCESSOR_SLEEP_FAMILY_NETBURST      0x0Fu /**< Intel's NetBurst family, past which every family is. */
#define PROCESSOR_SLEEP_MODEL_CORE_2_OR_NANO 0x0Fu /**< Family 6 model 0x0F: Intel's Core 2, Centaur's Nano. */
#define PROCESSOR_SLEEP_STEPPING_NANO        0x0Eu /**< Centaur's first stepping of it the rule names. */
#define PROCESSOR_SLEEP_FAMILY_ZEN           0x17u /**< AMD's Zen family, from which every family is. */
/** @} */

static bool processor_sleep_monitor_available = false;
static uint32_t processor_sleep_monitor_line = 0u;
static uint32_t processor_sleep_sleeps = 0u;
static uint32_t processor_sleep_skipped = 0u;
static uint32_t processor_sleep_halts = 0u;
static uint64_t processor_sleep_asleep = 0u;
static uint64_t processor_sleep_awake = 0u;
static uint64_t processor_sleep_last_wake = 0u;
static ProcessorSleepLadder_t processor_sleep_ladder = {.available = 1u};
static uint32_t processor_sleep_available_depths = 1u;
static uint32_t processor_sleep_validated_depths = 1u;
static bool processor_sleep_probing = false;
static uint32_t processor_sleep_depth = PROCESSOR_SLEEP_DEPTH_C1;
static uint32_t processor_sleep_clamped = 0u;
static uint32_t processor_sleep_refused = 0u;
static uint32_t processor_sleep_port_sleeps = 0u;
static bool processor_sleep_interrupt_break = false;
static volatile uint32_t processor_sleep_published_duty = KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED;
static uint32_t processor_sleep_bus_master_reload = PROCESSOR_SLEEP_BUS_MASTER_RELOAD_UNNEEDED;
static uint32_t processor_sleep_bus_master_demotions = 0u;

/**
 * @brief Charges the time since the last wake-up to the awake total.
 * @param now Current timestamp.
 */
static void processor_sleep_charge_awake(uint64_t now)
{
    if (processor_sleep_last_wake != 0u && now > processor_sleep_last_wake)
        processor_sleep_awake += now - processor_sleep_last_wake;
}

/**
 * @brief Charges the time since @p before to the asleep total and marks the wake-up.
 * @param before Timestamp taken just before the processor went to sleep.
 */
static void processor_sleep_charge_asleep_since(uint64_t before)
{
    const uint64_t after = asmutils_read_timestamp_counter();
    processor_sleep_asleep += after - before;
    processor_sleep_last_wake = after;
}

/**
 * @brief The line size the monitor watches, as CPUID leaves 1 and 5 give it: zero when leaf 1 does not advertise the
 *        pair, or when that line is larger than the idle line, which it would then share with other data.
 */
static uint32_t processor_sleep_monitor_line_of(uint32_t leaf1_ecx, uint32_t leaf5_ebx)
{
    const uint32_t line = leaf5_ebx & PROCESSOR_SLEEP_MONITOR_LINE_BITS;

    if ((leaf1_ecx & PROCESSOR_SLEEP_CPUID_MONITOR_BIT) == 0u || !kernel_processor_sleep_monitor_line_fits(line))
        return 0u;
    return line;
}

/**
 * @brief Reads CPUID for the monitor/wait pair, and arms nothing.
 *
 * @param out_line Receives the line size CPUID leaf 5 gives.
 * @param out_ecx  Receives CPUID leaf 5's ECX.
 * @param out_edx  Receives CPUID leaf 5's EDX.
 * @return true when leaf 1 advertises the pair and leaf 5 gives a non-zero line size.
 */
static bool processor_sleep_read_monitor(uint32_t *out_line, uint32_t *out_ecx, uint32_t *out_edx)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t leaf1_ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(1u, 0u, &eax, &ebx, &leaf1_ecx, &edx);
    if ((leaf1_ecx & PROCESSOR_SLEEP_CPUID_MONITOR_BIT) == 0u)
        return false;
    asmutils_cpuid(PROCESSOR_SLEEP_MONITOR_LEAF, 0u, &eax, &ebx, out_ecx, out_edx);
    *out_line = processor_sleep_monitor_line_of(leaf1_ecx, ebx);
    return *out_line != 0u;
}

/**
 * @brief A line nothing in the kernel writes, alone in its interference span. Armed as the monitor target, it turns
 *        MWAIT into the same wait as HLT, only an interrupt ending it, except at the requested depth.
 */
typedef struct __attribute__((aligned(KERNEL_SPSC_RING_INTERFERENCE_SIZE))) ProcessorSleepIdleLine {
    uint32_t word; /**< What MONITOR watches. */
} ProcessorSleepIdleLine_t;

_Static_assert(sizeof(ProcessorSleepIdleLine_t) == KERNEL_SPSC_RING_INTERFERENCE_SIZE,
               "the idle line spans its interference size and no more");

static volatile ProcessorSleepIdleLine_t processor_sleep_idle_line;

/** What the probes since the last adoption found of this ladder's ports: a word per depth past C1, 0 where none. */
static volatile uint32_t processor_sleep_port_verdict_words[PROCESSOR_SLEEP_PORT_VERDICTS];

_Static_assert(PROCESSOR_SLEEP_DEPTHS == PROCESSOR_SLEEP_HINT_MAX + 1u, "a ladder holds a depth per MWAIT hint");
_Static_assert(PROCESSOR_SLEEP_DEPTHS == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_PORTS + 1u,
               "a ladder holds C1 and a depth per port state a slot gives");
_Static_assert(sizeof(ProcessorSleepDepth_t) == 4u * sizeof(uint32_t),
               "a depth's two flags ride the padding after its port, so that a ladder's depths stay 16 bytes each");

/**
 * @brief The deepest validated depth no deeper than @p ceiling, or C1 when there is none.
 */
static uint32_t processor_sleep_deepest_validated_at_or_below(uint32_t ceiling)
{
    for (uint32_t candidate = ceiling; candidate > PROCESSOR_SLEEP_DEPTH_C1; --candidate)
        if (((processor_sleep_validated_depths >> candidate) & 1u) != 0u)
            return candidate;
    return PROCESSOR_SLEEP_DEPTH_C1;
}

/**
 * @brief The depth a sleep until an interrupt enters: the active one while a probe is open or once validated, the
 *        deepest validated below it otherwise.
 */
static uint32_t processor_sleep_entered_depth(void)
{
    if (processor_sleep_probing || ((processor_sleep_validated_depths >> processor_sleep_depth) & 1u) != 0u)
        return processor_sleep_depth;
    return processor_sleep_deepest_validated_at_or_below(processor_sleep_depth);
}

/**
 * @brief The depths of @p ladder a sleep may enter without a probe: every one but its port reads.
 */
static uint32_t processor_sleep_depths_without_ports(const ProcessorSleepLadder_t *ladder)
{
    uint32_t validated = 1u;

    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1 + 1u; depth < PROCESSOR_SLEEP_DEPTHS; ++depth)
        if (((ladder->available >> depth) & 1u) != 0u && ladder->depths[depth].entry != PROCESSOR_SLEEP_ENTRY_PORT)
            validated |= 1u << depth;
    return validated;
}

/**
 * @brief The deepest available depth no deeper than @p ceiling, or C1 when there is none.
 *
 * @details Walked down rather than taken from the highest set bit, because a ladder of MWAIT hints has gaps and the
 *          caller asked for at most this much.
 *
 * @param ceiling Depth the caller asked for, which is the most it is willing to take.
 * @return The depth.
 */
static uint32_t processor_sleep_deepest_available_at_or_below(uint32_t ceiling)
{
    for (uint32_t candidate = ceiling; candidate > PROCESSOR_SLEEP_DEPTH_C1; --candidate)
    {
        if (((processor_sleep_available_depths >> candidate) & 1u) != 0u)
            return candidate;
    }

    return PROCESSOR_SLEEP_DEPTH_C1;
}

/**
 * @brief Whether the processor's vendor string is @p vendor, of CPU_IDENTITY_SIGNATURE_SIZE bytes with its terminator.
 */
static bool processor_sleep_vendor_is(const CpuIdentity_t *identity, const char *vendor)
{
    return memcmp(identity->vendor, vendor, CPU_IDENTITY_SIGNATURE_SIZE) == 0;
}

/**
 * @brief Whether the processor's vendor string is Intel's.
 */
static bool processor_sleep_is_intel(const CpuIdentity_t *identity)
{
    return processor_sleep_vendor_is(identity, "GenuineIntel");
}

/**
 * @brief Depth 0 of a ladder: MWAIT 0 where the monitor pair is usable, HLT otherwise, with C1's published latency.
 */
static void processor_sleep_ladder_c1(const ProcessorSleepLadderFacts_t *facts, ProcessorSleepDepth_t *out_depth)
{
    out_depth->entry = facts->monitor ? PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT : PROCESSOR_SLEEP_ENTRY_HALT;
    out_depth->latency_published =
        facts->idle != NULL && advanced_configuration_and_power_interface_processor_idle_latency(
                                   facts->idle, PROCESSOR_SLEEP_HINT_C1, &out_depth->latency);
}

/**
 * @brief The depths past C1 of an Intel processor whose MWAIT reaches past it: each usable hint, at its own depth.
 */
static void processor_sleep_ladder_hints(const ProcessorSleepLadderFacts_t *facts, ProcessorSleepLadder_t *out_ladder)
{
    for (uint32_t hint = PROCESSOR_SLEEP_HINT_C1 + 1u; hint <= PROCESSOR_SLEEP_HINT_MAX; ++hint)
    {
        ProcessorSleepDepth_t *depth = &out_ladder->depths[hint];

        if (((facts->usable_hints >> hint) & 1u) == 0u)
            continue;
        out_ladder->available |= 1u << hint;
        depth->entry = PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT;
        depth->hint = hint;
        depth->latency_published =
            facts->idle != NULL &&
            advanced_configuration_and_power_interface_processor_idle_latency(facts->idle, hint, &depth->latency);
    }
}

/**
 * @brief Whether the slot's port states may be the depths past C1, and why not.
 * @return A ProcessorSleepPorts_t.
 */
static uint32_t processor_sleep_ladder_ports_gate(const ProcessorSleepLadderFacts_t *facts)
{
    if (facts->idle == NULL)
        return PROCESSOR_SLEEP_PORTS_NO_SLOT;
    if (!facts->timer_always_runs)
        return PROCESSOR_SLEEP_PORTS_TIMER_MAY_STOP;
    if (!facts->counter_invariant)
        return PROCESSOR_SLEEP_PORTS_COUNTER_MAY_STOP;
    if (facts->control_skipped)
        return PROCESSOR_SLEEP_PORTS_NOT_HANDED_OVER;
    return PROCESSOR_SLEEP_PORTS_TAKEN;
}

/**
 * @brief The depths past C1 the slot's port states give, depth 1 on, in their order.
 */
static void processor_sleep_ladder_ports(const ProcessorSleepLadderFacts_t *facts, ProcessorSleepLadder_t *out_ladder)
{
    AdvancedConfigurationAndPowerInterfaceIdlePorts_t ports;

    bool checks_bus_masters = false;

    advanced_configuration_and_power_interface_processor_idle_ports(facts->idle, facts->fadt, facts->bus_master_check,
                                                                    &ports);
    out_ladder->ports_refused = ports.refused;
    for (uint32_t index = 0u; index < ports.count; ++index)
    {
        ProcessorSleepDepth_t *depth = &out_ladder->depths[index + 1u];

        out_ladder->available |= 1u << (index + 1u);
        depth->entry = PROCESSOR_SLEEP_ENTRY_PORT;
        depth->port = ports.ports[index].port;
        depth->latency_published = true;
        depth->latency = ports.ports[index].latency;
        depth->bus_master_check =
            ports.ports[index].type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_TYPE_3;
        checks_bus_masters = checks_bus_masters || depth->bus_master_check;
    }
    if (checks_bus_masters)
        (void) advanced_configuration_and_power_interface_power_management_status_ports(
            facts->fadt, out_ladder->bus_master_status_ports);
}

/**
 * @brief Whether a depth @p ladder holds has the bus-master check.
 */
static bool processor_sleep_ladder_checks_bus_masters(const ProcessorSleepLadder_t *ladder)
{
    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1 + 1u; depth < PROCESSOR_SLEEP_DEPTHS; ++depth)
        if (((ladder->available >> depth) & 1u) != 0u && ladder->depths[depth].bus_master_check)
            return true;
    return false;
}

/**
 * @brief Sets BM_RLD in @p fadt's PM1 control registers where @p ladder holds a depth with the bus-master check.
 * @return A ProcessorSleepBusMasterReload_t.
 */
static uint32_t
processor_sleep_set_bus_master_reload(const ProcessorSleepLadder_t *ladder,
                                      const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt)
{
    uint16_t control[2];

    if (!processor_sleep_ladder_checks_bus_masters(ladder))
        return PROCESSOR_SLEEP_BUS_MASTER_RELOAD_UNNEEDED;
    advanced_configuration_and_power_interface_power_management_control_ports(fadt, control);
    return advanced_configuration_and_power_interface_set_power_management_control_bits(
               &advanced_configuration_and_power_interface_machine_host, control,
               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_RELOAD) ?
               PROCESSOR_SLEEP_BUS_MASTER_RELOAD_SET :
               PROCESSOR_SLEEP_BUS_MASTER_RELOAD_UNWRITTEN;
}

/**
 * @brief Publishes what a probe found of port depth @p depth's port, for the parked processors.
 */
static void processor_sleep_publish_verdict(uint32_t depth, uint32_t verdict)
{
    processor_sleep_port_verdict_words[depth - 1u] = (uint32_t) processor_sleep_ladder.depths[depth].port | verdict;
}

/**
 * @brief Adopts @p ladder: every depth available, C1 active, no refusal counted, no verdict published.
 */
static void processor_sleep_adopt(const ProcessorSleepLadder_t *ladder)
{
    processor_sleep_ladder = *ladder;
    processor_sleep_available_depths = ladder->available | 1u;
    processor_sleep_validated_depths = processor_sleep_depths_without_ports(ladder);
    processor_sleep_probing = false;
    processor_sleep_depth = PROCESSOR_SLEEP_DEPTH_C1;
    processor_sleep_refused = 0u;
    for (uint32_t index = 0u; index < PROCESSOR_SLEEP_PORT_VERDICTS; ++index)
        processor_sleep_port_verdict_words[index] = 0u;
}

/**
 * @brief The depth a watched sleep enters: the active one where it is an MWAIT depth, C1 otherwise, since a port
 *        read watches no line.
 */
static uint32_t processor_sleep_watched_depth(void)
{
    return processor_sleep_ladder.depths[processor_sleep_depth].entry == PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT ?
               processor_sleep_depth :
               PROCESSOR_SLEEP_DEPTH_C1;
}

/**
 * @brief The depth a sleep at @p depth enters once the bus-master check ran, where @p depth has it and no probe is
 *        open: a probe enters the depth it judges, as Linux's s2idle skips the check (v6.12 `acpi_idle_enter_s2idle`),
 *        so that each of its sleeps reads the port it judges.
 */
__attribute__((always_inline)) static inline uint32_t processor_sleep_checked_depth(uint32_t depth)
{
    if (!processor_sleep_ladder.depths[depth].bus_master_check || processor_sleep_probing)
        return depth;

    const uint32_t entered = kernel_processor_sleep_depth_after_the_bus_master_check(
        &processor_sleep_ladder, processor_sleep_validated_depths, depth,
        kernel_processor_sleep_check_bus_masters(&processor_sleep_ladder));
    processor_sleep_bus_master_demotions += entered != depth ? 1u : 0u;
    return entered;
}

/**
 * @struct ProcessorSleepWatch_t
 * @brief The word a watched sleep waits on, and the value that means nothing new.
 */
typedef struct {
    const volatile uint32_t *watched; /**< The word. */
    uint32_t expected;                /**< Its value until something new arrives. */
} ProcessorSleepWatch_t;

/**
 * @brief Whether the watched word has moved, for processor_sleep_masked.
 * @param context The ProcessorSleepWatch_t.
 * @return true once it differs from the value expected.
 */
__attribute__((always_inline)) static inline bool processor_sleep_watched_moved(void *context)
{
    const ProcessorSleepWatch_t *const watch = (const ProcessorSleepWatch_t *) context;

    return *watch->watched != watch->expected;
}

/**
 * @brief Enters depth @p entered by its entry, the accounting armed, and says whether an interrupt was taken before the
 *        return.
 *
 * @details Called with interrupts masked, the monitor armed for an MWAIT depth. HLT and MWAIT come in the step that
 *          enables interrupts; a port is read masked, and interrupts enabled after the read.
 *
 * @param entered The depth.
 * @param line    The line MONITOR watches, for an MWAIT depth.
 * @return How the processor was put to sleep, whether an interrupt was taken, and @p entered.
 */
__attribute__((always_inline)) static inline ProcessorSleepOutcome_t processor_sleep_enter(uint32_t entered,
                                                                                           const volatile void *line)
{
    const ProcessorSleepDepth_t *depth = &processor_sleep_ladder.depths[entered];
    ProcessorSleepOutcome_t outcome = {PROCESSOR_SLEEP_HALT, false, entered};

    ++processor_sleep_sleeps;
    kernel_wakeup_accounting_arm();
    if (depth->entry == PROCESSOR_SLEEP_ENTRY_PORT)
    {
        ++processor_sleep_port_sleeps;
        asmutils_input_byte_then_enable_interrupts(depth->port);
        outcome.mode = PROCESSOR_SLEEP_PORT;
    }
    else if (depth->entry == PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT)
    {
        asmutils_enable_interrupts_and_monitor_wait(
            kernel_processor_sleep_mwait_hint(depth->hint),
            processor_sleep_interrupt_break ? PROCESSOR_SLEEP_BREAK_ON_INTERRUPT : 0u);
        outcome.mode = PROCESSOR_SLEEP_MONITOR;
    }
    else
    {
        ++processor_sleep_halts;
        asmutils_enable_interrupts_and_halt();
    }
    outcome.interrupted = !kernel_wakeup_accounting_is_armed();
    if (outcome.mode == PROCESSOR_SLEEP_MONITOR && line != &processor_sleep_idle_line)
        kernel_wakeup_accounting_close_monitor_write();
    else
        kernel_wakeup_accounting_close_unattributed();
    return outcome;
}

/**
 * @brief Sleeps unless @p done says the wait is over: masks interrupts, arms the monitor on @p line for an MWAIT depth,
 *        asks @p done, then enters @p depth, and gives the interrupt flag back as found.
 *
 * @details The one place every sleep of this module is entered, so that the masking around the last check is written,
 *          and tested, once; inlined, so that each caller pays only for the parts it uses. A wait on the idle line that
 *          returns while still armed is counted unattributed, since nothing writes that line; one on another line, a
 *          write.
 *
 * @param line    The line MONITOR watches: the idle line when only an interrupt should end the wait.
 * @param done    Asked once, after the monitor is armed; NULL never says the wait is over.
 * @param context What @p done receives.
 * @param depth   The depth asked, which the bus-master check may lower.
 * @return How the processor was put to sleep, PROCESSOR_SLEEP_NONE when @p done said the wait was over, whether an
 *         interrupt was taken, and the depth entered.
 */
__attribute__((always_inline)) static inline ProcessorSleepOutcome_t
processor_sleep_masked(const volatile void *line, ProcessorSleepCondition_t done, void *context, uint32_t depth)
{
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();
    const uint64_t before = asmutils_read_timestamp_counter();
    ProcessorSleepOutcome_t outcome = {PROCESSOR_SLEEP_NONE, false, depth};

    processor_sleep_charge_awake(before);
    if (processor_sleep_ladder.depths[depth].entry == PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT)
        asmutils_monitor((const void *) line, 0u, 0u);

    if (done != NULL && done(context))
    {
        ++processor_sleep_skipped;
        processor_sleep_last_wake = before;
    }
    else
    {
        outcome = processor_sleep_enter(processor_sleep_checked_depth(depth), line);
        processor_sleep_charge_asleep_since(before);
    }
    asmutils_restore_flags(flags);
    return outcome;
}

void kernel_processor_sleep_initialize(void)
{
    uint32_t line = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    ProcessorSleepLadderFacts_t facts;
    ProcessorSleepLadder_t ladder;

    processor_sleep_clamped = 0u;
    processor_sleep_published_duty = KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED;
    kernel_processor_sleep_begin_accounting();
    processor_sleep_monitor_available = processor_sleep_read_monitor(&line, &ecx, &edx);
    processor_sleep_monitor_line = processor_sleep_monitor_available ? line : 0u;
    processor_sleep_interrupt_break =
        processor_sleep_monitor_available && (ecx & PROCESSOR_SLEEP_CPUID_INTERRUPT_BREAK_BIT) != 0u;
    kernel_processor_sleep_read_ladder_facts(&facts);
    kernel_processor_sleep_build_ladder(&facts, &ladder);
    processor_sleep_adopt(&ladder);
    processor_sleep_bus_master_reload = processor_sleep_set_bus_master_reload(&ladder, facts.fadt);
}

bool kernel_processor_sleep_enters_type_3_behind_the_bus_master_check(const CpuIdentity_t *identity)
{
    const bool family_6 = identity->family == PROCESSOR_SLEEP_FAMILY_6;

    if (processor_sleep_is_intel(identity))
        return identity->family > PROCESSOR_SLEEP_FAMILY_NETBURST ||
               (family_6 && identity->model >= PROCESSOR_SLEEP_MODEL_CORE_2_OR_NANO);
    if (processor_sleep_vendor_is(identity, "CentaurHauls"))
        return identity->family > PROCESSOR_SLEEP_FAMILY_6 ||
               (family_6 && identity->model == PROCESSOR_SLEEP_MODEL_CORE_2_OR_NANO &&
                identity->stepping >= PROCESSOR_SLEEP_STEPPING_NANO);
    if (processor_sleep_vendor_is(identity, "AuthenticAMD"))
        return identity->family >= PROCESSOR_SLEEP_FAMILY_ZEN;
    return processor_sleep_vendor_is(identity, "  Shanghai  ");
}

uint32_t kernel_processor_sleep_bus_master_reload(void) { return processor_sleep_bus_master_reload; }

const char *kernel_processor_sleep_bus_master_reload_name(uint32_t reload)
{
    static const char *const names[] = {"unneeded", "set", "unwritten"};

    return reload < sizeof(names) / sizeof(names[0]) ? names[reload] : "unknown";
}

void kernel_processor_sleep_build_ladder(const ProcessorSleepLadderFacts_t *facts, ProcessorSleepLadder_t *out_ladder)
{
    memset(out_ladder, 0, sizeof(*out_ladder));
    out_ladder->available = 1u;
    processor_sleep_ladder_c1(facts, &out_ladder->depths[PROCESSOR_SLEEP_DEPTH_C1]);
    const bool deep_hints = facts->monitor && (facts->usable_hints >> (PROCESSOR_SLEEP_HINT_C1 + 1u)) != 0u;
    if (deep_hints && facts->intel)
    {
        out_ladder->ports = PROCESSOR_SLEEP_PORTS_MONITOR_WAIT;
        processor_sleep_ladder_hints(facts, out_ladder);
        return;
    }
    out_ladder->ports = processor_sleep_ladder_ports_gate(facts);
    if (out_ladder->ports == PROCESSOR_SLEEP_PORTS_TAKEN)
        processor_sleep_ladder_ports(facts, out_ladder);
    if (deep_hints && out_ladder->available == 1u)
    {
        out_ladder->ports = PROCESSOR_SLEEP_PORTS_MONITOR_WAIT;
        processor_sleep_ladder_hints(facts, out_ladder);
    }
}

void kernel_processor_sleep_read_ladder_facts(ProcessorSleepLadderFacts_t *out_facts)
{
    uint32_t line = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;
    CpuIdentity_t identity;

    memset(out_facts, 0, sizeof(*out_facts));
    cpu_identity_read(&identity);
    out_facts->monitor = processor_sleep_read_monitor(&line, &ecx, &edx);
    out_facts->intel = processor_sleep_is_intel(&identity);
    out_facts->bus_master_check = kernel_processor_sleep_enters_type_3_behind_the_bus_master_check(&identity);
    out_facts->timer_always_runs = advanced_pic_timer_backend_always_runs() != 0u;
    out_facts->counter_invariant = advanced_pic_timer_backend_has_invariant_timestamp_counter() != 0u;
    out_facts->usable_hints = out_facts->monitor ?
                                  kernel_processor_sleep_usable_hints(kernel_processor_sleep_enumerated_hints(edx),
                                                                      out_facts->timer_always_runs) :
                                  0u;
    out_facts->control_skipped = advanced_configuration_and_power_interface_initialization()->idle_control ==
                                 ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_SKIPPED;
    out_facts->idle = apic_is_running() ? advanced_configuration_and_power_interface_processor_idle_of_processor(
                                              advanced_configuration_and_power_interface_processor_idle(),
                                              advanced_configuration_and_power_interface_madt(), apic_get_id()) :
                                          NULL;
    out_facts->fadt = &advanced_configuration_and_power_interface_directory()->fixed_description;
}

void kernel_processor_sleep_begin_accounting(void)
{
    processor_sleep_sleeps = 0u;
    processor_sleep_skipped = 0u;
    processor_sleep_halts = 0u;
    processor_sleep_port_sleeps = 0u;
    processor_sleep_bus_master_demotions = 0u;
    processor_sleep_asleep = 0u;
    processor_sleep_awake = 0u;
    processor_sleep_last_wake = 0u;
    kernel_wakeup_accounting_reset();
}

bool kernel_processor_sleep_has_monitor(void) { return processor_sleep_monitor_available; }

bool kernel_processor_sleep_monitor_implemented(void)
{
    uint32_t line = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    return processor_sleep_read_monitor(&line, &ecx, &edx);
}

uint32_t kernel_processor_sleep_hints_of_cpuid_leaves(uint32_t leaf1_ecx, uint32_t leaf5_ebx, uint32_t leaf5_edx)
{
    if (processor_sleep_monitor_line_of(leaf1_ecx, leaf5_ebx) == 0u)
        return 0u;
    return kernel_processor_sleep_enumerated_hints(leaf5_edx);
}

uint32_t kernel_processor_sleep_hints_enumerated_by_this_processor(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t leaf1_ecx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(1u, 0u, &eax, &ebx, &leaf1_ecx, &edx);
    asmutils_cpuid(PROCESSOR_SLEEP_MONITOR_LEAF, 0u, &eax, &ebx, &ecx, &edx);
    return kernel_processor_sleep_hints_of_cpuid_leaves(leaf1_ecx, ebx, edx);
}

uint32_t kernel_processor_sleep_monitor_line_bytes(void) { return processor_sleep_monitor_line; }

ProcessorSleepMode_t processor_sleep_until_write(const volatile uint32_t *watched, uint32_t expected)
{
    if (watched == NULL)
        return processor_sleep_until_interrupt().mode;

    ProcessorSleepWatch_t watch = {watched, expected};
    return processor_sleep_masked(watched, processor_sleep_watched_moved, &watch, processor_sleep_watched_depth()).mode;
}

ProcessorSleepOutcome_t processor_sleep_until_interrupt(void)
{
    return processor_sleep_masked(&processor_sleep_idle_line, NULL, NULL, processor_sleep_entered_depth());
}

bool processor_sleep_unless(ProcessorSleepCondition_t done, void *context)
{
    return processor_sleep_masked(&processor_sleep_idle_line, done, context, PROCESSOR_SLEEP_DEPTH_C1).mode ==
           PROCESSOR_SLEEP_NONE;
}

uint32_t kernel_processor_sleep_count(void) { return processor_sleep_sleeps; }

uint32_t kernel_processor_sleep_skipped_count(void) { return processor_sleep_skipped; }

uint32_t kernel_processor_sleep_halt_count(void) { return processor_sleep_halts; }

uint64_t kernel_processor_sleep_asleep_cycles(void) { return processor_sleep_asleep; }

uint64_t kernel_processor_sleep_awake_cycles(void) { return processor_sleep_awake; }

uint32_t kernel_processor_sleep_duty_cycle_permille(void)
{
    const uint64_t accounted = processor_sleep_awake + processor_sleep_asleep;
    if (accounted == 0u)
        return 1000u;
    return (uint32_t) ((processor_sleep_awake * 1000u) / accounted);
}

void kernel_processor_sleep_close_session(void)
{
    const uint64_t accounted = processor_sleep_awake + processor_sleep_asleep;
    processor_sleep_published_duty =
        (accounted == 0u) ? KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED : kernel_processor_sleep_duty_cycle_permille();
}

uint32_t kernel_processor_sleep_published_duty_cycle_permille(void) { return processor_sleep_published_duty; }

uint32_t kernel_processor_sleep_enumerated_hints(uint32_t leaf5_edx)
{
    uint32_t mask = 0u;

    for (uint32_t hint = 0u; hint <= PROCESSOR_SLEEP_HINT_MAX; ++hint)
    {
        const uint32_t nibble = (leaf5_edx >> (4u * (hint + 1u))) & 0xFu;
        if (nibble != 0u)
            mask |= (1u << hint);
    }

    return mask;
}

uint32_t kernel_processor_sleep_usable_hints(uint32_t enumerated, bool timer_always_runs)
{
    return timer_always_runs ? enumerated : enumerated & (1u << PROCESSOR_SLEEP_HINT_C1);
}

bool kernel_processor_sleep_has_interrupt_break(void) { return processor_sleep_interrupt_break; }

uint32_t kernel_processor_sleep_available_depths(void) { return processor_sleep_available_depths; }

bool kernel_processor_sleep_depth_of(uint32_t depth, ProcessorSleepDepth_t *out_depth)
{
    if (depth >= PROCESSOR_SLEEP_DEPTHS || ((processor_sleep_available_depths >> depth) & 1u) == 0u)
        return false;
    *out_depth = processor_sleep_ladder.depths[depth];
    return true;
}

const ProcessorSleepLadder_t *kernel_processor_sleep_ladder(void) { return &processor_sleep_ladder; }

bool kernel_processor_sleep_request_depth(uint32_t depth)
{
    if (depth > PROCESSOR_SLEEP_DEPTH_DEEPEST)
        depth = PROCESSOR_SLEEP_DEPTH_DEEPEST;

    if (((processor_sleep_available_depths >> depth) & 1u) != 0u)
    {
        processor_sleep_depth = depth;
        return true;
    }

    processor_sleep_depth = processor_sleep_deepest_available_at_or_below(depth);
    ++processor_sleep_clamped;
    return false;
}

bool kernel_processor_sleep_refuse_depth(uint32_t depth)
{
    if (depth == PROCESSOR_SLEEP_DEPTH_C1 || depth >= PROCESSOR_SLEEP_DEPTHS ||
        ((processor_sleep_available_depths >> depth) & 1u) == 0u ||
        processor_sleep_ladder.depths[depth].entry != PROCESSOR_SLEEP_ENTRY_PORT)
        return false;

    processor_sleep_available_depths &= ~(1u << depth);
    processor_sleep_validated_depths &= ~(1u << depth);
    processor_sleep_publish_verdict(depth, PROCESSOR_SLEEP_PORT_VERDICT_REFUSED);
    ++processor_sleep_refused;
    if (processor_sleep_depth >= depth)
        processor_sleep_depth = processor_sleep_deepest_available_at_or_below(depth);
    return true;
}

uint32_t kernel_processor_sleep_refused_depth_count(void) { return processor_sleep_refused; }

uint32_t kernel_processor_sleep_depth_awaiting_probe(void)
{
    return ((processor_sleep_validated_depths >> processor_sleep_depth) & 1u) != 0u ? PROCESSOR_SLEEP_NO_DEPTH :
                                                                                      processor_sleep_depth;
}

void kernel_processor_sleep_begin_probe(void) { processor_sleep_probing = true; }

void kernel_processor_sleep_end_probe(bool stops)
{
    processor_sleep_probing = false;
    if (kernel_processor_sleep_depth_awaiting_probe() == PROCESSOR_SLEEP_NO_DEPTH)
        return;
    if (stops)
    {
        processor_sleep_validated_depths |= 1u << processor_sleep_depth;
        processor_sleep_publish_verdict(processor_sleep_depth, PROCESSOR_SLEEP_PORT_VERDICT_VALIDATED);
    }
    else
        (void) kernel_processor_sleep_refuse_depth(processor_sleep_depth);
}

uint32_t kernel_processor_sleep_port_sleep_count(void) { return processor_sleep_port_sleeps; }

uint32_t kernel_processor_sleep_depth_after_the_bus_master_check(const ProcessorSleepLadder_t *ladder, uint32_t allowed,
                                                                 uint32_t depth, uint32_t status)
{
    if (depth >= PROCESSOR_SLEEP_DEPTHS || !ladder->depths[depth].bus_master_check ||
        (status & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_STATUS) == 0u)
        return depth;
    for (uint32_t below = depth - 1u; below > PROCESSOR_SLEEP_DEPTH_C1; --below)
        if ((((ladder->available & allowed) >> below) & 1u) != 0u && !ladder->depths[below].bus_master_check)
            return below;
    return PROCESSOR_SLEEP_DEPTH_C1;
}

uint32_t kernel_processor_sleep_read_bus_master_status(const ProcessorSleepLadder_t *ladder)
{
    uint32_t status = 0u;

    for (uint32_t index = 0u; index < 2u; ++index)
        if (ladder->bus_master_status_ports[index] != 0u)
            status |= asmutils_input_word(ladder->bus_master_status_ports[index]);
    return status;
}

uint32_t kernel_processor_sleep_check_bus_masters(const ProcessorSleepLadder_t *ladder)
{
    const uint32_t status = kernel_processor_sleep_read_bus_master_status(ladder);

    if ((status & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_STATUS) == 0u)
        return status;
    for (uint32_t index = 0u; index < 2u; ++index)
        if (ladder->bus_master_status_ports[index] != 0u)
            asmutils_output_word(ladder->bus_master_status_ports[index],
                                 ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_STATUS);
    return status;
}

uint32_t kernel_processor_sleep_bus_master_demotion_count(void) { return processor_sleep_bus_master_demotions; }

uint32_t kernel_processor_sleep_depth(void) { return processor_sleep_entered_depth(); }

uint32_t kernel_processor_sleep_clamped_count(void) { return processor_sleep_clamped; }

const char *kernel_processor_sleep_ports_name(uint32_t ports)
{
    static const char *const names[] = {"taken",          "monitor_wait",     "no_slot",
                                        "timer_may_stop", "counter_may_stop", "not_handed_over"};

    return ports < sizeof(names) / sizeof(names[0]) ? names[ports] : "unknown";
}

const char *kernel_processor_sleep_entry_name(uint32_t entry)
{
    static const char *const names[] = {"halt", "mwait", "port"};

    return entry < sizeof(names) / sizeof(names[0]) ? names[entry] : "unknown";
}

const volatile void *kernel_processor_sleep_idle_line(void) { return &processor_sleep_idle_line; }

uint32_t kernel_processor_sleep_idle_line_bytes(void) { return (uint32_t) sizeof(processor_sleep_idle_line); }

bool kernel_processor_sleep_monitor_line_fits(uint32_t largest_line_bytes)
{
    return largest_line_bytes <= sizeof(processor_sleep_idle_line);
}

void kernel_processor_sleep_port_verdicts(uint32_t out_verdicts[PROCESSOR_SLEEP_PORT_VERDICTS])
{
    for (uint32_t index = 0u; index < PROCESSOR_SLEEP_PORT_VERDICTS; ++index)
        out_verdicts[index] = processor_sleep_port_verdict_words[index];
}

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)
void kernel_processor_sleep_debug_adopt_ladder(const ProcessorSleepLadder_t *ladder) { processor_sleep_adopt(ladder); }
#endif
