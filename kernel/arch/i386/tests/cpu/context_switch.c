#include <kernel/cpu/context.h>
#include <kernel/cpu/context_probe.h>
#include <kernel/cpu/cpu_identity.h>
#include <kernel/cpu/extended_state.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(context_switch, KERNEL_TEST_STAGE_PUBLISHED);

_Static_assert(offsetof(ContextProbeState_t, control_word) == CONTEXT_PROBE_OFFSET_CONTROL_WORD,
               "context_probe.S reads the control word there");
_Static_assert(offsetof(ContextProbeState_t, simd_control_status) == CONTEXT_PROBE_OFFSET_SIMD_CONTROL,
               "context_probe.S reads the MXCSR there");
_Static_assert(offsetof(ContextProbeState_t, vectors) == CONTEXT_PROBE_OFFSET_VECTORS,
               "context_probe.S reads the vectors there");

#define CONTEXT_SWITCH_TEST_SAVES       4u          /**< FXSAVE, XSAVE, XSAVEOPT and XSAVES. */
#define CONTEXT_SWITCH_TEST_AREA_BYTES  3072u       /**< Above the 2,688 bytes of XSAVE's AVX-512 components. */
#define CONTEXT_SWITCH_TEST_STACK_BYTES 4096u       /**< A test context's stack. */
#define CONTEXT_SWITCH_TEST_ROUND_TRIPS 1000u       /**< The switches between two contexts a measure counts. */
#define CONTEXT_SWITCH_TEST_CR4_OSXSAVE (1u << 18u) /**< CR4.OSXSAVE: XGETBV runs. */
#define CONTEXT_SWITCH_TEST_XCR0_SSE    0x2u        /**< XCR0's SSE component. */
#define CONTEXT_SWITCH_TEST_XCR0_AVX    0x4u        /**< XCR0's AVX component: YMM0-YMM7 are held whole. */
#define CONTEXT_SWITCH_TEST_XSTATE_BV   512u /**< Where an XSAVE area's header holds XSTATE_BV (SDM vol. 1, §13.4.2). */
#define CONTEXT_SWITCH_TEST_XMM_REGION  160u /**< Where the legacy region holds XMM0 (SDM vol. 1, Table 13-1). */
#define CONTEXT_SWITCH_TEST_XMM_BYTES   128u /**< XMM0-XMM7. */
#define CONTEXT_SWITCH_TEST_MARKER                                                                                     \
    0xA5u /**< What an area's XMM region holds before a switch that must not write it.                                 \
           */
#define CONTEXT_SWITCH_TEST_FCW_INITIAL   0x037Fu /**< The x87 control word after FNINIT (SDM vol. 1, Table 8-4). */
#define CONTEXT_SWITCH_TEST_MXCSR_INITIAL 0x1F80u /**< The MXCSR after a reset (SDM vol. 1, §10.2.3). */
#define CONTEXT_SWITCH_TEST_TCG           "TCGTCGTCGTCG" /**< QEMU's TCG, which does not track the components in use. */

/**
 * @brief What a test context runs between two others: it switches to @p peer @p remaining times in all, loading
 *        @p state first when there is one, then back to @p home.
 */
typedef struct {
    CpuContext_t *self;               /**< The context that runs it. */
    CpuContext_t *peer;               /**< The context it switches to while switches remain. */
    CpuContext_t *home;               /**< The context it switches back to once none remains. */
    volatile uint32_t *remaining;     /**< The switches left to the pair, shared with the peer. */
    const ContextProbeState_t *state; /**< What it loads before each switch, or NULL for integer code only. */
    uint32_t wide;                    /**< Whether @p state holds YMM0-YMM7 rather than XMM0-XMM7. */
    ContextProbeState_t *report;      /**< Where it stores its registers before its first switch, or NULL. */
    void *volatile received;          /**< The argument its entry received. */
    volatile uint32_t entry_frame;    /**< Its entry's frame pointer: 8 past a 16-byte boundary when called aligned. */
} ContextSwitchTestRole_t;

#define CONTEXT_SWITCH_TEST_AREA_HOME                                                                                  \
    0u /**< The area of the context the test itself runs in, adopted again by each.                                    \
        */
#define CONTEXT_SWITCH_TEST_AREA_OTHER  1u /**< The area of the context a test switches to alone. */
#define CONTEXT_SWITCH_TEST_AREA_FIRST  2u /**< The area of the first context of a pair. */
#define CONTEXT_SWITCH_TEST_AREA_SECOND 3u /**< The area of the second. */

static uint8_t context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_SECOND + 1u][CONTEXT_SWITCH_TEST_AREA_BYTES]
    __attribute__((aligned(EXTENDED_STATE_AREA_ALIGNMENT))); /**< The extended state areas. */
static uint8_t context_switch_test_stacks[3][CONTEXT_SWITCH_TEST_STACK_BYTES]
    __attribute__((aligned(16))); /**< The stacks of the prepared contexts. */

/**
 * @brief Makes the running code @p here with the save @p save, on the one home area every test adopts again.
 */
static void context_switch_test_adopt(CpuContext_t *here, ExtendedStateSave_t save)
{
    cpu_context_adopt(here, context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_HOME], save);
}

/**
 * @brief Runs a test context's role: reports its registers when asked, then switches to its peer while switches
 *        remain, and back home.
 */
static void context_switch_test_play(void *argument)
{
    ContextSwitchTestRole_t *const role = (ContextSwitchTestRole_t *) argument;

    role->received = argument;
    role->entry_frame = (uint32_t) (uintptr_t) __builtin_frame_address(0);
    if (role->report)
        context_probe_read(role->report, role->wide);
    for (;;)
    {
        if (*role->remaining == 0u)
            cpu_context_switch(role->self, role->home);
        else
        {
            --*role->remaining;
            if (role->state)
                context_probe_load_and_switch(role->state, role->self, role->peer, role->wide);
            else
                cpu_context_switch(role->self, role->peer);
        }
    }
}

/**
 * @brief XCR0, which XGETBV reads once CR4.OSXSAVE is set.
 */
__attribute__((target("xsave"))) static uint32_t context_switch_test_read_xcr0(void)
{
    return (uint32_t) __builtin_ia32_xgetbv(0u);
}

/**
 * @brief Whether XCR0 enables AVX, so the probes hold YMM0-YMM7 whole.
 */
static uint32_t context_switch_test_wide(void)
{
    if ((asmutils_read_control_register_4() & CONTEXT_SWITCH_TEST_CR4_OSXSAVE) == 0u)
        return 0u;
    return (context_switch_test_read_xcr0() & CONTEXT_SWITCH_TEST_XCR0_AVX) != 0u ? 1u : 0u;
}

/**
 * @brief Whether the processor runs under QEMU's TCG, whose XSAVE treats every component as in use.
 */
static bool context_switch_test_under_tcg(void)
{
    CpuIdentity_t identity;

    cpu_identity_read(&identity);
    return identity.hypervisor &&
           memcmp(identity.hypervisor_signature, CONTEXT_SWITCH_TEST_TCG, sizeof(CONTEXT_SWITCH_TEST_TCG)) == 0;
}

/**
 * @brief Fills @p state with control words and vector bytes made from @p seed.
 */
static void context_switch_test_fill(ContextProbeState_t *state, uint16_t control_word, uint32_t simd_control_status,
                                     uint8_t seed)
{
    state->control_word = control_word;
    state->reserved = 0u;
    state->simd_control_status = simd_control_status;
    for (uint32_t byte = 0u; byte < CONTEXT_PROBE_VECTOR_BYTES; ++byte)
        state->vectors[byte] = (uint8_t) (seed + byte);
}

/**
 * @brief Whether @p found holds what @p expected does: the two control words and the vector bytes the probes hold,
 *        all 32 of each register when @p wide, its first 16 otherwise.
 */
static bool context_switch_test_same(const ContextProbeState_t *expected, const ContextProbeState_t *found,
                                     uint32_t wide)
{
    const uint32_t register_bytes = wide ? 32u : 16u;

    if (expected->control_word != found->control_word || expected->simd_control_status != found->simd_control_status)
        return false;
    for (uint32_t vector = 0u; vector < 8u; ++vector)
    {
        if (memcmp(&expected->vectors[32u * vector], &found->vectors[32u * vector], register_bytes) != 0)
            return false;
    }
    return true;
}

/**
 * @brief XSTATE_BV, the components an XSAVE area says it holds.
 */
static uint32_t context_switch_test_components_saved(const uint8_t *area)
{
    return (uint32_t) area[CONTEXT_SWITCH_TEST_XSTATE_BV] | ((uint32_t) area[CONTEXT_SWITCH_TEST_XSTATE_BV + 1u] << 8u);
}

/**
 * @brief Whether an area's XMM region still holds CONTEXT_SWITCH_TEST_MARKER in each byte.
 */
static bool context_switch_test_marker_kept(const uint8_t *area)
{
    for (uint32_t byte = 0u; byte < CONTEXT_SWITCH_TEST_XMM_BYTES; ++byte)
    {
        if (area[CONTEXT_SWITCH_TEST_XMM_REGION + byte] != CONTEXT_SWITCH_TEST_MARKER)
            return false;
    }
    return true;
}

/**
 * @brief Runs the pair @p first and @p second from @p home, interrupts disabled, until @p switches switches have run
 *        between them, and gives the timestamp counter's ticks the whole run took.
 */
static uint64_t context_switch_test_run_pair(CpuContext_t *home, CpuContext_t *first, volatile uint32_t *remaining,
                                             uint32_t switches)
{
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();
    const uint64_t start = asmutils_read_timestamp_counter();

    *remaining = switches;
    cpu_context_switch(home, first);

    const uint64_t ticks = asmutils_read_timestamp_counter() - start;

    asmutils_restore_flags(flags);
    return ticks;
}

/**
 * @brief Prepares the pair of roles @p first and @p second, which play between each other from @p home with the save
 *        @p save, on the pair's two areas, loading @p state before each switch when it is not NULL.
 */
static void context_switch_test_prepare_pair(ContextSwitchTestRole_t *first, ContextSwitchTestRole_t *second,
                                             CpuContext_t *first_context, CpuContext_t *second_context,
                                             CpuContext_t *home, volatile uint32_t *remaining, ExtendedStateSave_t save,
                                             const ContextProbeState_t *state, uint32_t wide)
{
    *first = (ContextSwitchTestRole_t){first_context, second_context, home, remaining, state, wide, NULL, NULL, 0u};
    *second = (ContextSwitchTestRole_t){second_context, first_context, home, remaining, state, wide, NULL, NULL, 0u};
    cpu_context_prepare(first_context, context_switch_test_stacks[1] + CONTEXT_SWITCH_TEST_STACK_BYTES,
                        context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_FIRST], save, context_switch_test_play,
                        first);
    cpu_context_prepare(second_context, context_switch_test_stacks[2] + CONTEXT_SWITCH_TEST_STACK_BYTES,
                        context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_SECOND], save, context_switch_test_play,
                        second);
}

/**
 * @brief A context finds its x87 control word, its MXCSR and its vector registers as it left them after another
 *        context ran and overwrote them, with every save the processor offers.
 *
 * @details The vector registers are YMM0-YMM7 when XCR0 enables AVX, XMM0-XMM7 otherwise. A switch that saved only
 *          the 512 bytes of FXSAVE would lose the upper halves of the YMM registers.
 */
KERNEL_TEST(a_context_finds_its_registers_after_another_ran)
{
    static ContextProbeState_t held;
    static ContextProbeState_t overwriting;
    static ContextProbeState_t after;
    static ContextProbeState_t found;
    static CpuContext_t here;
    static CpuContext_t other;
    static ContextSwitchTestRole_t role;
    static volatile uint32_t remaining;
    const uint32_t wide = context_switch_test_wide();
    uint32_t offered = 0u;
    uint32_t kept = 0u;

    context_probe_read(&found, wide);
    context_switch_test_fill(&held, 0x027Fu, 0x7F80u, 0x10u);
    context_switch_test_fill(&overwriting, 0x007Fu, 0x3F80u, 0x80u);
    for (ExtendedStateSave_t save = EXTENDED_STATE_SAVE_FXSAVE; save <= EXTENDED_STATE_SAVE_XSAVES; ++save)
    {
        if (!extended_state_offers_save(save) || extended_state_area_size(save) > CONTEXT_SWITCH_TEST_AREA_BYTES)
            continue;
        ++offered;
        memset(&after, 0, sizeof(after));
        remaining = 1u;
        role = (ContextSwitchTestRole_t){&other, &here, &here, &remaining, &overwriting, wide, NULL, NULL, 0u};
        context_switch_test_adopt(&here, save);
        cpu_context_prepare(&other, context_switch_test_stacks[0] + CONTEXT_SWITCH_TEST_STACK_BYTES,
                            context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_OTHER], save, context_switch_test_play,
                            &role);

        const uint32_t flags = asmutils_save_flags_and_disable_interrupts();

        context_probe_hold_across_switch(&held, &after, &here, &other, wide);
        context_probe_load(&found, wide);
        asmutils_restore_flags(flags);
        kept += context_switch_test_same(&held, &after, wide) ? 1u : 0u;
    }
    kernel_test_check(test, offered >= 1u, "the processor offers a save that fits the test's areas");
    kernel_test_check(test, kept == offered, "the registers come back as they were, with every save offered");
    kernel_test_measure(test, "offered", offered);
    kernel_test_measure(test, "kept", kept);
    kernel_test_measure(test, "wide", wide);
    kernel_test_measure(test, "preferred", extended_state_preferred_save());
}

/**
 * @brief A prepared context starts with its argument, from the initial x87 and SIMD state, whatever the context that
 *        switched to it held, and that context gets its own registers back, with every save the processor offers.
 *
 * @details The standard XRSTOR loads the MXCSR from the area whatever its header says, while the compacted XRSTORS
 *          initialises it (Intel SDM vol. 1, 253665-093US, §13.8.1 and §13.8.2): each save starts from its own path.
 */
KERNEL_TEST(a_prepared_context_starts_from_the_initial_state_with_its_argument)
{
    static ContextProbeState_t held;
    static ContextProbeState_t initial;
    static ContextProbeState_t reported;
    static ContextProbeState_t after;
    static ContextProbeState_t found;
    static CpuContext_t here;
    static CpuContext_t fresh;
    static ContextSwitchTestRole_t role;
    static volatile uint32_t remaining;
    const uint32_t wide = context_switch_test_wide();
    uint32_t offered = 0u;
    uint32_t received = 0u;
    uint32_t aligned = 0u;
    uint32_t initialised = 0u;
    uint32_t kept = 0u;

    context_probe_read(&found, wide);
    context_switch_test_fill(&held, 0x027Fu, 0x7F80u, 0x10u);
    memset(&initial, 0, sizeof(initial));
    initial.control_word = CONTEXT_SWITCH_TEST_FCW_INITIAL;
    initial.simd_control_status = CONTEXT_SWITCH_TEST_MXCSR_INITIAL;
    for (ExtendedStateSave_t save = EXTENDED_STATE_SAVE_FXSAVE; save <= EXTENDED_STATE_SAVE_XSAVES; ++save)
    {
        if (!extended_state_offers_save(save) || extended_state_area_size(save) > CONTEXT_SWITCH_TEST_AREA_BYTES)
            continue;
        ++offered;
        memset(&reported, 0xFF, sizeof(reported));
        remaining = 0u;
        role = (ContextSwitchTestRole_t){&fresh, &here, &here, &remaining, NULL, wide, &reported, NULL, 0u};
        context_switch_test_adopt(&here, save);
        cpu_context_prepare(&fresh, context_switch_test_stacks[0] + CONTEXT_SWITCH_TEST_STACK_BYTES,
                            context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_OTHER], save, context_switch_test_play,
                            &role);

        const uint32_t flags = asmutils_save_flags_and_disable_interrupts();

        context_probe_hold_across_switch(&held, &after, &here, &fresh, wide);
        context_probe_load(&found, wide);
        asmutils_restore_flags(flags);
        received += role.received == &role ? 1u : 0u;
        aligned += (role.entry_frame & 15u) == 8u ? 1u : 0u;
        initialised += context_switch_test_same(&initial, &reported, wide) ? 1u : 0u;
        kept += context_switch_test_same(&held, &after, wide) ? 1u : 0u;
    }
    kernel_test_check(test, offered >= 1u, "the processor offers a save that fits the test's areas");
    kernel_test_check(test, received == offered, "the entry receives its argument");
    kernel_test_check(test, aligned == offered,
                      "on a stack 16-byte aligned at its call: its frame pointer sits 8 bytes past a boundary");
    kernel_test_check(test, initialised == offered,
                      "it starts with the control words of FNINIT and a reset, and vector registers at zero");
    kernel_test_check(test, kept == offered, "the context that switched to it gets its registers back");
    kernel_test_measure(test, "offered", offered);
    kernel_test_measure(test, "initialised", initialised);
}

/**
 * @brief Two contexts that run integer code only write no vector byte when they switch, with XSAVEOPT and XSAVES,
 *        while a context that holds vectors is saved with them.
 *
 * @details Restoring a prepared area puts every component in its initial state. For a component in that state,
 *          XSAVEOPT and XSAVES write its XSTATE_BV bit as 0 and leave its region alone (Intel SDM vol. 1, 253665-093US,
 *          §13.9 and §13.11). Only the SSE and AVX bits are checked: the processor may still track x87 as in use after
 *          the restore (§13.8.3). QEMU's TCG treats every component as in use (target/i386/tcg/fpu_helper.c,
 *          get_xinuse), so there only the control is checked.
 */
KERNEL_TEST(switching_between_integer_contexts_writes_no_vector_state)
{
    static ContextProbeState_t vectors;
    static CpuContext_t here;
    static CpuContext_t first;
    static CpuContext_t second;
    static ContextSwitchTestRole_t first_role;
    static ContextSwitchTestRole_t second_role;
    static volatile uint32_t remaining;
    const uint32_t wide = context_switch_test_wide();
    const bool tracked = !context_switch_test_under_tcg();
    const uint32_t vector_components = CONTEXT_SWITCH_TEST_XCR0_SSE | (wide ? CONTEXT_SWITCH_TEST_XCR0_AVX : 0u);
    uint32_t checked = 0u;
    uint32_t untouched = 0u;
    uint32_t controls = 0u;

    context_switch_test_fill(&vectors, 0x037Fu, 0x1F80u, 0x40u);
    for (ExtendedStateSave_t save = EXTENDED_STATE_SAVE_XSAVEOPT; save <= EXTENDED_STATE_SAVE_XSAVES; ++save)
    {
        if (!extended_state_offers_save(save) || extended_state_area_size(save) > CONTEXT_SWITCH_TEST_AREA_BYTES)
            continue;
        context_switch_test_adopt(&here, save);
        ++checked;
        context_switch_test_prepare_pair(&first_role, &second_role, &first, &second, &here, &remaining, save, NULL,
                                         wide);
        memset(context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_FIRST] + CONTEXT_SWITCH_TEST_XMM_REGION,
               CONTEXT_SWITCH_TEST_MARKER, CONTEXT_SWITCH_TEST_XMM_BYTES);
        memset(context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_SECOND] + CONTEXT_SWITCH_TEST_XMM_REGION,
               CONTEXT_SWITCH_TEST_MARKER, CONTEXT_SWITCH_TEST_XMM_BYTES);
        (void) context_switch_test_run_pair(&here, &first, &remaining, 4u);
        untouched +=
            (context_switch_test_components_saved(context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_FIRST]) &
             vector_components) == 0u &&
                    (context_switch_test_components_saved(context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_SECOND]) &
                     vector_components) == 0u &&
                    context_switch_test_marker_kept(context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_FIRST]) &&
                    context_switch_test_marker_kept(context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_SECOND]) ?
                1u :
                0u;
        context_switch_test_prepare_pair(&first_role, &second_role, &first, &second, &here, &remaining, save, &vectors,
                                         wide);
        (void) context_switch_test_run_pair(&here, &first, &remaining, 2u);
        controls += (context_switch_test_components_saved(context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_FIRST]) &
                     vector_components) == vector_components ?
                        1u :
                        0u;
    }
    if (checked == 0u)
    {
        kernel_test_skip(test, "the processor offers neither XSAVEOPT nor XSAVES");
        return;
    }
    kernel_test_check(test, controls == checked, "a context that holds vectors is saved with them");
    if (tracked)
        kernel_test_check(test, untouched == checked, "integer contexts write no vector byte, with each save");
    kernel_test_measure(test, "checked", checked);
    kernel_test_measure(test, "untouched", untouched);
    kernel_test_measure(test, "tracked", tracked ? 1u : 0u);
}

/**
 * @brief A thousand switches between two contexts that run integer code only, with the preferred save: the window
 *        tools/budgets/context-switch.debug.budget counts, and the timestamp counter's ticks per switch.
 */
KERNEL_TEST(a_thousand_switches_between_two_integer_contexts)
{
    static CpuContext_t here;
    static CpuContext_t first;
    static CpuContext_t second;
    static ContextSwitchTestRole_t first_role;
    static ContextSwitchTestRole_t second_role;
    static volatile uint32_t remaining;
    const ExtendedStateSave_t save = extended_state_preferred_save();

    context_switch_test_adopt(&here, save);
    context_switch_test_prepare_pair(&first_role, &second_role, &first, &second, &here, &remaining, save, NULL, 0u);

    const uint64_t ticks = context_switch_test_run_pair(&here, &first, &remaining, CONTEXT_SWITCH_TEST_ROUND_TRIPS);

    kernel_test_check(test, remaining == 0u, "every switch ran, and the pair came back");
    kernel_test_measure(test, "save", save);
    kernel_test_measure(test, "ticks_per_switch", (uint32_t) (ticks / (CONTEXT_SWITCH_TEST_ROUND_TRIPS + 2u)));
}

/**
 * @brief The timestamp counter's ticks per switch with each save the processor offers, between two integer contexts
 *        and between two that load their vectors before each switch.
 *
 * @details Printed beside the order extended_state_preferred_save takes from the SDM. A clock varies from one run to
 *          the next by more than XSAVES and XSAVEOPT differ, so it decides nothing.
 */
KERNEL_TEST(each_save_takes_its_ticks_per_switch)
{
    static const char *const integer_keys[CONTEXT_SWITCH_TEST_SAVES] = {"fxsave_integer", "xsave_integer",
                                                                        "xsaveopt_integer", "xsaves_integer"};
    static const char *const vector_keys[CONTEXT_SWITCH_TEST_SAVES] = {"fxsave_vectors", "xsave_vectors",
                                                                       "xsaveopt_vectors", "xsaves_vectors"};
    static ContextProbeState_t vectors;
    static CpuContext_t here;
    static CpuContext_t first;
    static CpuContext_t second;
    static ContextSwitchTestRole_t first_role;
    static ContextSwitchTestRole_t second_role;
    static volatile uint32_t remaining;
    const uint32_t wide = context_switch_test_wide();
    uint32_t measured = 0u;

    context_switch_test_fill(&vectors, 0x037Fu, 0x1F80u, 0x40u);
    for (ExtendedStateSave_t save = EXTENDED_STATE_SAVE_FXSAVE; save <= EXTENDED_STATE_SAVE_XSAVES; ++save)
    {
        if (!extended_state_offers_save(save) || extended_state_area_size(save) > CONTEXT_SWITCH_TEST_AREA_BYTES)
            continue;
        context_switch_test_adopt(&here, save);
        context_switch_test_prepare_pair(&first_role, &second_role, &first, &second, &here, &remaining, save, NULL,
                                         wide);

        const uint64_t integer_ticks =
            context_switch_test_run_pair(&here, &first, &remaining, CONTEXT_SWITCH_TEST_ROUND_TRIPS);

        context_switch_test_prepare_pair(&first_role, &second_role, &first, &second, &here, &remaining, save, &vectors,
                                         wide);

        const uint64_t vector_ticks =
            context_switch_test_run_pair(&here, &first, &remaining, CONTEXT_SWITCH_TEST_ROUND_TRIPS);

        kernel_test_measure(test, integer_keys[save],
                            (uint32_t) (integer_ticks / (CONTEXT_SWITCH_TEST_ROUND_TRIPS + 2u)));
        kernel_test_measure(test, vector_keys[save],
                            (uint32_t) (vector_ticks / (CONTEXT_SWITCH_TEST_ROUND_TRIPS + 2u)));
        ++measured;
    }
    kernel_test_check(test, measured >= 1u, "at least one save was measured");
    kernel_test_measure(test, "measured", measured);
}

/**
 * @brief The running code adopts the area the processor last restored from, and its next switch away saves its
 *        registers there, with every save the processor offers.
 *
 * @details After a switch back to a context, the processor tracks its area as the source of the last XRSTOR, and
 *          XSAVEOPT and XSAVES skip a component unmodified since (Intel SDM vol. 1, 253665-093US, §13.6 and §13.8.3):
 *          adopted again, the area kept the initial state its preparation wrote, and the registers came back as it.
 */
KERNEL_TEST(adopting_the_area_restored_last_keeps_the_registers)
{
    static ContextProbeState_t found;
    static ContextProbeState_t held;
    static ContextProbeState_t after;
    static ContextProbeState_t again;
    static CpuContext_t here;
    static CpuContext_t other;
    static ContextSwitchTestRole_t role;
    static volatile uint32_t remaining;
    const uint32_t wide = context_switch_test_wide();
    uint32_t offered = 0u;
    uint32_t kept = 0u;

    context_probe_read(&found, wide);
    context_switch_test_fill(&held, 0x027Fu, 0x7F80u, 0x10u);
    for (ExtendedStateSave_t save = EXTENDED_STATE_SAVE_FXSAVE; save <= EXTENDED_STATE_SAVE_XSAVES; ++save)
    {
        if (!extended_state_offers_save(save) || extended_state_area_size(save) > CONTEXT_SWITCH_TEST_AREA_BYTES)
            continue;
        ++offered;
        remaining = 0u;
        role = (ContextSwitchTestRole_t){&other, &here, &here, &remaining, NULL, wide, NULL, NULL, 0u};

        const uint32_t flags = asmutils_save_flags_and_disable_interrupts();

        cpu_context_adopt(&here, context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_HOME], save);
        cpu_context_prepare(&other, context_switch_test_stacks[0] + CONTEXT_SWITCH_TEST_STACK_BYTES,
                            context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_OTHER], save, context_switch_test_play,
                            &role);
        context_probe_hold_across_switch(&held, &after, &here, &other, wide);
        cpu_context_adopt(&here, context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_HOME], save);
        cpu_context_prepare(&other, context_switch_test_stacks[0] + CONTEXT_SWITCH_TEST_STACK_BYTES,
                            context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_OTHER], save, context_switch_test_play,
                            &role);
        cpu_context_switch(&here, &other);
        context_probe_read(&again, wide);
        context_probe_load(&found, wide);
        asmutils_restore_flags(flags);
        kept +=
            context_switch_test_same(&held, &after, wide) && context_switch_test_same(&held, &again, wide) ? 1u : 0u;
    }
    kernel_test_check(test, offered >= 1u, "the processor offers a save that fits the test's areas");
    kernel_test_check(test, kept == offered, "the registers come back from the area adopted again, with every save");
    kernel_test_measure(test, "offered", offered);
    kernel_test_measure(test, "kept", kept);
}

/**
 * @brief An entry that returns, which a prepared context's should never do.
 */
static void context_switch_test_return_at_once(void *argument) { (void) argument; }

/**
 * @brief A context whose entry returns says so on COM1, then halts its processor with interrupts disabled.
 */
KERNEL_TEST_MANUAL(a_context_whose_entry_returns_halts_its_processor,
                   "halts the processor once the entry has returned and the message is written")
{
    static CpuContext_t here;
    static CpuContext_t returning;
    const ExtendedStateSave_t save = extended_state_preferred_save();

    (void) test;
    context_switch_test_adopt(&here, save);
    cpu_context_prepare(&returning, context_switch_test_stacks[0] + CONTEXT_SWITCH_TEST_STACK_BYTES,
                        context_switch_test_areas[CONTEXT_SWITCH_TEST_AREA_OTHER], save,
                        context_switch_test_return_at_once, NULL);
    (void) asmutils_save_flags_and_disable_interrupts();
    cpu_context_switch(&here, &returning);
}
