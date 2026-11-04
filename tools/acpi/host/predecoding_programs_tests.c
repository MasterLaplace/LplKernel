#include "made_up_aml.h"
#include "predecoding_runs.h"

#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(host_predecoding_programs, KERNEL_TEST_STAGE_INITIALIZATION);

#define PROGRAMS_COUNT          600u        /**< Methods written and run, records off then on. */
#define PROGRAMS_SEED           0x2545F491u /**< The xorshift state of the first method. */
#define PROGRAMS_MAX_DEPTH      3u          /**< Constructs nested inside the method's outer While. */
#define PROGRAMS_MAX_STATEMENTS 4u          /**< Statements one TermList holds at most. */
#define PROGRAMS_DATA_LOCALS                                                                                           \
    4u                           /**< Local0 to Local3 hold data; Local4 to Local7 count the turns of                  \
                                      the While at each depth, which nothing else writes. */
#define PROGRAMS_LOCAL0    0x60u /**< The byte of Local0. */
#define PROGRAMS_ARGUMENT0 0x68u /**< The byte of Arg0. */
#define PROGRAMS_NULL_NAME 0x00u /**< The byte of NullName. */

/**
 * @brief What writes a method: the xorshift state, the depth of the construct being written and whether
 *        a While encloses it.
 */
typedef struct ProgramsWriter {
    uint32_t state; /**< The xorshift32 state. */
    uint32_t depth; /**< Constructs open around the TermList being written. */
    bool in_loop;   /**< Whether a While encloses it, where Break and Continue may stand. */
} ProgramsWriter_t;

static MadeUpAml_t programs_bodies[PROGRAMS_MAX_DEPTH + 2u];
static MadeUpAml_t programs_helper;
static MadeUpAml_t programs_method;

/**
 * @brief The next number of the writer's xorshift32 sequence, below @p bound.
 */
static uint32_t programs_pick(ProgramsWriter_t *writer, uint32_t bound)
{
    writer->state ^= writer->state << 13u;
    writer->state ^= writer->state >> 17u;
    writer->state ^= writer->state << 5u;
    return writer->state % bound;
}

/**
 * @brief Appends one byte.
 */
static void programs_byte(MadeUpAml_t *aml, uint8_t byte) { made_up_aml_raw(aml, &byte, 1u); }

/**
 * @brief Appends a leaf: a data local, Arg0, or a constant of every width, 0 and Ones included.
 */
static void programs_leaf(ProgramsWriter_t *writer, MadeUpAml_t *aml)
{
    static const uint64_t constants[] = {0u, 1u, 2u, 3u, 5u, 7u, 0x10u, 0xFFu, 0xFFFFFFFFu, 0x100000000ull, ~0ull};
    const uint32_t choice = programs_pick(writer, 6u);

    if (choice < PROGRAMS_DATA_LOCALS)
        programs_byte(aml, (uint8_t) (PROGRAMS_LOCAL0 + choice));
    else if (choice == PROGRAMS_DATA_LOCALS)
        programs_byte(aml, PROGRAMS_ARGUMENT0);
    else
        made_up_aml_integer(aml, constants[programs_pick(writer, sizeof(constants) / sizeof(constants[0]))]);
}

/**
 * @brief Appends an expression: a leaf, a comparison an LNot may wrap, LNot over an operator, or an
 *        operator on two integers with NullName as its target, a modulo by a local that may hold 0
 *        among them, up to @p levels deep.
 */
static void programs_expression(ProgramsWriter_t *writer, MadeUpAml_t *aml, uint32_t levels)
{
    static const uint8_t comparisons[] = {0x93u, 0x94u, 0x95u};
    static const uint8_t operators[] = {0x72u, 0x74u, 0x77u, 0x79u, 0x7Au, 0x7Bu, 0x7Du, 0x7Fu, 0x85u};
    const uint32_t choice = (levels == 0u) ? 0u : programs_pick(writer, 4u);

    if (choice == 0u)
    {
        programs_leaf(writer, aml);
        return;
    }
    if (choice == 1u)
    {
        if (programs_pick(writer, 2u) == 0u)
            programs_byte(aml, 0x92u);
        programs_byte(aml, comparisons[programs_pick(writer, sizeof(comparisons))]);
        programs_expression(writer, aml, levels - 1u);
        programs_expression(writer, aml, levels - 1u);
        return;
    }
    if (choice == 2u)
    {
        programs_byte(aml, 0x92u);
        programs_expression(writer, aml, levels - 1u);
        return;
    }
    programs_byte(aml, operators[programs_pick(writer, sizeof(operators))]);
    programs_expression(writer, aml, levels - 1u);
    programs_expression(writer, aml, levels - 1u);
    programs_byte(aml, PROGRAMS_NULL_NAME);
}

static void programs_statements(ProgramsWriter_t *writer, MadeUpAml_t *aml);

/**
 * @brief Appends a call, which no record runs and which goes on to the next statement: BUMP (), which increments
 *        GLBL, a data local set to INCR (a leaf), or LOOP (a constant), whose While runs in the callee's frame.
 */
static void programs_call(ProgramsWriter_t *writer, MadeUpAml_t *aml)
{
    const uint8_t local = (uint8_t) (PROGRAMS_LOCAL0 + programs_pick(writer, PROGRAMS_DATA_LOCALS));

    switch (programs_pick(writer, 3u))
    {
    case 0u: made_up_aml_name(aml, "BUMP"); return;
    case 1u:
        programs_byte(aml, 0x70u);
        made_up_aml_name(aml, "INCR");
        programs_leaf(writer, aml);
        programs_byte(aml, local);
        return;
    default:
        made_up_aml_name(aml, "LOOP");
        made_up_aml_integer(aml, 1u + programs_pick(writer, 4u));
        return;
    }
}

/**
 * @brief Appends a While no record runs, whose turns all go to the loop: the counter of its depth set, then `While
 *        (LAnd (Increment (counter) < bound, One))`, the counter stepped by the predicate itself, its body one to
 *        three statements that go on to the next: a String stored to a data local, a Noop, or BUMP ().
 */
static void programs_refused_while(ProgramsWriter_t *writer, MadeUpAml_t *aml)
{
    static const uint8_t while_lead[] = {0xA2u};
    MadeUpAml_t *body = &programs_bodies[writer->depth + 1u];
    const uint8_t counter = (uint8_t) (PROGRAMS_LOCAL0 + PROGRAMS_DATA_LOCALS + writer->depth);
    const uint8_t reset[] = {0x70u, 0x00u, counter};
    const uint8_t predicate[] = {0x90u, 0x95u, 0x75u, counter, 0x0Au, (uint8_t) (5u + programs_pick(writer, 4u)),
                                 0x01u};
    const uint32_t count = 1u + programs_pick(writer, 3u);

    made_up_aml_raw(aml, reset, sizeof(reset));
    made_up_aml_begin(body);
    made_up_aml_raw(body, predicate, sizeof(predicate));
    for (uint32_t index = 0u; index < count; ++index)
    {
        const uint8_t string[] = {0x70u, 0x0Du, '7', 0x00u,
                                  (uint8_t) (PROGRAMS_LOCAL0 + programs_pick(writer, PROGRAMS_DATA_LOCALS))};
        const uint32_t choice = programs_pick(writer, 3u);

        if (choice == 0u)
            made_up_aml_raw(body, string, sizeof(string));
        else if (choice == 1u)
            programs_byte(body, 0xA3u);
        else
            made_up_aml_name(body, "BUMP");
    }
    made_up_aml_construct(aml, while_lead, sizeof(while_lead), body);
}

/**
 * @brief Appends `If (expression) { Return (a data local) }`, which leaves the method early.
 */
static void programs_early_return(ProgramsWriter_t *writer, MadeUpAml_t *aml)
{
    static const uint8_t if_lead[] = {0xA0u};
    MadeUpAml_t *body = &programs_bodies[writer->depth + 1u];
    const uint8_t leave[] = {0xA4u, (uint8_t) (PROGRAMS_LOCAL0 + programs_pick(writer, PROGRAMS_DATA_LOCALS))};

    made_up_aml_begin(body);
    programs_expression(writer, body, 2u);
    made_up_aml_raw(body, leave, sizeof(leave));
    made_up_aml_construct(aml, if_lead, sizeof(if_lead), body);
}

/**
 * @brief Appends a While that runs as a tight loop: the counter of its depth set, compared with a constant,
 *        and stepped by one statement, an Increment, a Decrement, an Add with a target under an LNot over
 *        LGreater, a Store of an Add, or an Add of a modulo by a data local, which may hold 0 or a String.
 */
static void programs_tight_while(ProgramsWriter_t *writer, MadeUpAml_t *aml)
{
    static const uint8_t while_lead[] = {0xA2u};
    MadeUpAml_t *body = &programs_bodies[writer->depth + 1u];
    const uint8_t counter = (uint8_t) (PROGRAMS_LOCAL0 + PROGRAMS_DATA_LOCALS + writer->depth);
    const uint8_t bound = (uint8_t) (2u + programs_pick(writer, 5u));
    const uint8_t data = (uint8_t) (PROGRAMS_LOCAL0 + programs_pick(writer, PROGRAMS_DATA_LOCALS));
    const uint8_t step = (uint8_t) (1u + programs_pick(writer, 3u));
    const uint8_t count_up[] = {0x70u, 0x00u, counter};
    const uint8_t count_down[] = {0x70u, 0x0Au, bound, counter};
    const uint8_t below[] = {0x95u, counter, 0x0Au, bound};
    const uint8_t above_zero[] = {0x94u, counter, 0x00u};
    const uint8_t at_most[] = {0x92u, 0x94u, counter, 0x0Au, bound};
    const uint8_t increment[] = {0x75u, counter};
    const uint8_t decrement[] = {0x76u, counter};
    const uint8_t add[] = {0x72u, counter, 0x0Au, step, counter};
    const uint8_t store_sum[] = {0x70u, 0x72u, counter, 0x01u, 0x00u, counter};
    const uint8_t add_modulo[] = {0x72u, counter, 0x72u, 0x01u, 0x85u, 0x00u, data, 0x00u, 0x00u, counter};

    made_up_aml_begin(body);
    switch (programs_pick(writer, 5u))
    {
    case 0u:
        made_up_aml_raw(aml, count_up, sizeof(count_up));
        made_up_aml_raw(body, below, sizeof(below));
        made_up_aml_raw(body, increment, sizeof(increment));
        break;
    case 1u:
        made_up_aml_raw(aml, count_down, sizeof(count_down));
        made_up_aml_raw(body, above_zero, sizeof(above_zero));
        made_up_aml_raw(body, decrement, sizeof(decrement));
        break;
    case 2u:
        made_up_aml_raw(aml, count_up, sizeof(count_up));
        made_up_aml_raw(body, at_most, sizeof(at_most));
        made_up_aml_raw(body, add, sizeof(add));
        break;
    case 3u:
        made_up_aml_raw(aml, count_up, sizeof(count_up));
        made_up_aml_raw(body, below, sizeof(below));
        made_up_aml_raw(body, store_sum, sizeof(store_sum));
        break;
    default:
        made_up_aml_raw(aml, count_up, sizeof(count_up));
        made_up_aml_raw(body, below, sizeof(below));
        made_up_aml_raw(body, add_modulo, sizeof(add_modulo));
        break;
    }
    made_up_aml_construct(aml, while_lead, sizeof(while_lead), body);
}

/**
 * @brief Appends an If, or a While that counts its turns in the local of its depth, its body the next
 *        builder, and an Else after an If one time in two.
 */
static void programs_construct(ProgramsWriter_t *writer, MadeUpAml_t *aml, bool loop)
{
    static const uint8_t if_lead[] = {0xA0u};
    static const uint8_t else_lead[] = {0xA1u};
    static const uint8_t while_lead[] = {0xA2u};
    MadeUpAml_t *body = &programs_bodies[writer->depth + 1u];
    const bool was_in_loop = writer->in_loop;
    const uint8_t counter = (uint8_t) (PROGRAMS_LOCAL0 + PROGRAMS_DATA_LOCALS + writer->depth);

    ++writer->depth;
    made_up_aml_begin(body);
    if (loop)
    {
        const uint8_t reset[] = {0x70u, 0x00u, counter};
        const uint8_t predicate[] = {0x95u, counter, 0x0Au, (uint8_t) (2u + programs_pick(writer, 5u)), 0x75u, counter};

        made_up_aml_raw(aml, reset, sizeof(reset));
        made_up_aml_raw(body, predicate, sizeof(predicate));
        writer->in_loop = true;
    }
    else
        programs_expression(writer, body, 3u);
    programs_statements(writer, body);
    made_up_aml_construct(aml, loop ? while_lead : if_lead, 1u, body);
    if (!loop && programs_pick(writer, 2u) == 0u)
    {
        made_up_aml_begin(body);
        programs_statements(writer, body);
        made_up_aml_construct(aml, else_lead, 1u, body);
    }
    writer->in_loop = was_in_loop;
    --writer->depth;
}

/**
 * @brief Appends one statement: a store, a step or an operator into a data local or NullName, a construct, a While
 *        that runs as a tight loop, a Break or a Continue inside a While, one that hands records back: a String
 *        stored to a local, a call, a While whose turns all go to the loop, or an If that returns.
 */
static void programs_statement(ProgramsWriter_t *writer, MadeUpAml_t *aml)
{
    const uint32_t choice = programs_pick(writer, 16u);
    const uint8_t local = (uint8_t) (PROGRAMS_LOCAL0 + programs_pick(writer, PROGRAMS_DATA_LOCALS));

    if (choice < 3u)
    {
        programs_byte(aml, 0x70u);
        programs_expression(writer, aml, 2u);
        programs_byte(aml, local);
    }
    else if (choice < 5u)
    {
        programs_byte(aml, (choice == 3u) ? 0x75u : 0x76u);
        programs_byte(aml, local);
    }
    else if (choice == 5u)
    {
        programs_byte(aml, 0x72u);
        programs_expression(writer, aml, 1u);
        programs_expression(writer, aml, 1u);
        programs_byte(aml, (programs_pick(writer, 4u) == 0u) ? PROGRAMS_NULL_NAME : local);
    }
    else if (choice < 9u && writer->depth < PROGRAMS_MAX_DEPTH)
        programs_construct(writer, aml, choice == 8u);
    else if (choice == 9u && writer->in_loop)
        programs_byte(aml, (programs_pick(writer, 3u) == 0u) ? 0x9Fu : 0xA5u);
    else if (choice == 10u)
    {
        const uint8_t string[] = {0x70u, 0x0Du, '7', 0x00u, local};

        made_up_aml_raw(aml, string, sizeof(string));
    }
    else if (choice == 11u)
        programs_tight_while(writer, aml);
    else if (choice == 13u)
        programs_call(writer, aml);
    else if (choice == 14u)
        programs_refused_while(writer, aml);
    else if (choice == 15u)
        programs_early_return(writer, aml);
    else
        programs_byte(aml, 0xA3u);
}

/**
 * @brief Appends one to PROGRAMS_MAX_STATEMENTS statements.
 */
static void programs_statements(ProgramsWriter_t *writer, MadeUpAml_t *aml)
{
    const uint32_t count = 1u + programs_pick(writer, PROGRAMS_MAX_STATEMENTS);

    for (uint32_t index = 0u; index < count; ++index)
        programs_statement(writer, aml);
}

/**
 * @brief Writes GLBL, an Integer, and the methods the statements call: BUMP, which increments GLBL, INCR (Arg0), which
 *        returns Arg0 + 1, and LOOP (Arg0), which counts Arg0 turns of a While of its own.
 */
static void programs_write_helpers(void)
{
    static const uint8_t bump[] = {0x75u, 'G', 'L', 'B', 'L'};
    static const uint8_t increment[] = {0xA4u, 0x72u, 0x68u, 0x01u, 0x00u};
    static const uint8_t loop[] = {0x70u, 0x00u, 0x60u, 0xA2u, 0x06u, 0x95u, 0x60u, 0x68u, 0x75u, 0x60u, 0xA4u, 0x60u};

    made_up_aml_name_integer(&programs_method, "GLBL", 0u);
    made_up_aml_begin(&programs_helper);
    made_up_aml_raw(&programs_helper, bump, sizeof(bump));
    made_up_aml_method(&programs_method, "BUMP", 0x00u, &programs_helper);
    made_up_aml_begin(&programs_helper);
    made_up_aml_raw(&programs_helper, increment, sizeof(increment));
    made_up_aml_method(&programs_method, "INCR", 0x01u, &programs_helper);
    made_up_aml_begin(&programs_helper);
    made_up_aml_raw(&programs_helper, loop, sizeof(loop));
    made_up_aml_method(&programs_method, "LOOP", 0x01u, &programs_helper);
}

/**
 * @brief Writes GLBL and the methods it calls, then MAIN (Arg0): its data locals set to constants, then a While of a
 *        few turns whose body the writer draws, then the sum of the data locals and GLBL returned.
 */
static void programs_write(ProgramsWriter_t *writer)
{
    static const uint8_t finish[] = {0xA4u, 0x72u, 0x72u, 0x72u, 0x60u, 0x61u, 0x00u, 0x72u, 0x62u,
                                     0x63u, 0x00u, 0x00u, 'G',   'L',   'B',   'L',   0x00u};
    MadeUpAml_t *body = &programs_bodies[0];

    made_up_aml_begin(body);
    for (uint32_t local = 0u; local < PROGRAMS_DATA_LOCALS; ++local)
    {
        programs_byte(body, 0x70u);
        programs_leaf(writer, body);
        programs_byte(body, (uint8_t) (PROGRAMS_LOCAL0 + local));
    }
    writer->depth = 0u;
    writer->in_loop = false;
    programs_construct(writer, body, true);
    made_up_aml_raw(body, finish, sizeof(finish));
    made_up_aml_begin(&programs_method);
    programs_write_helpers();
    made_up_aml_method(&programs_method, "MAIN", 0x01u, body);
}

/**
 * @brief The boot line, not a test, decides whether a load runs loops from records: W4's LOOP, on a load opened as
 *        the boot left it, runs from records unless `test-acpi --predecoding off` put lpl.acpi.predecoding=off on
 *        it. The test notes which, a remark tools/parity.sh does not compare, and CI checks that the run with
 *        records off notes the one, and the other run the other.
 */
KERNEL_TEST(the_boot_line_decides_whether_loops_run_from_records)
{
    static const uint8_t loop[] = {
        0x14u, 0x14u, 'L',   'O',   'O',   'P',   0x00u, 0x70u, 0x00u, 0x60u, 0xA2u,
        0x08u, 0x95u, 0x60u, 0x0Bu, 0xE8u, 0x03u, 0x75u, 0x60u, 0xA4u, 0x60u,
    };
    PredecodingRun_t run = {0};

    if (!predecoding_open_booted(test, loop, sizeof(loop)))
        return;
    predecoding_evaluate("\\LOOP", PREDECODING_NO_ARGUMENT, &run);
    kernel_test_check(test, (run.records_run != 0u) == predecoding_load.predecodes_loops,
                      "records run when the load runs loops from records, and only then");
    kernel_test_note(test, (run.records_run != 0u) ? "loops run from records" : "no loop runs from records");
    predecoding_close();
}

/**
 * @brief Methods written from a fixed seed, with While, If, Else, Break, Continue and Return nested in loops of a
 *        few turns, operators, comparisons and LNot over locals, Arg0 (its 32 high bits set but for the
 *        first) and constants of every width, calls, one into a loop of its own, statements that hand records
 *        back, While that run as tight loops, and While whose turns all go to the loop, leave what the loop
 *        leaves when their loops run from records: each runs with records off, then on, under both integer
 *        widths, and both leave the same fold. The control: records ran, and were handed back, While ran as
 *        tight loops, and loops were handed back whole, over the methods.
 */
KERNEL_TEST(methods_written_at_random_leave_what_the_loop_leaves)
{
    ProgramsWriter_t writer = {PROGRAMS_SEED, 0u, false};
    uint32_t agree = 0u;
    uint32_t written = 0u;
    uint64_t records_run = 0u;
    uint64_t declined = 0u;
    uint64_t tight_turns = 0u;
    uint64_t loops = 0u;

    for (uint32_t program = 0u; program < PROGRAMS_COUNT; ++program)
    {
        const uint8_t revision = (program % 2u == 0u) ? 2u : 1u;
        PredecodingRun_t off = {0};
        PredecodingRun_t on = {0};

        programs_write(&writer);
        if (programs_method.overflowed)
            continue;
        ++written;
        if (!predecoding_run(test, programs_method.bytes, programs_method.length, revision, "\\MAIN",
                             program * 0x100000001ull, false, &off) ||
            !predecoding_run(test, programs_method.bytes, programs_method.length, revision, "\\MAIN",
                             program * 0x100000001ull, true, &on))
            return;
        agree += (off.fold == on.fold && off.records_run == 0u) ? 1u : 0u;
        records_run += on.records_run;
        declined += on.declined;
        tight_turns += on.tight_turns;
        loops += on.loops;
    }
    kernel_test_check(test, written > PROGRAMS_COUNT / 2u, "most methods fit their builder");
    kernel_test_check(test, agree == written, "every method leaves with records what it leaves without");
    kernel_test_check(test, records_run != 0u && declined != 0u && tight_turns != 0u && loops != 0u,
                      "records ran, and were handed back, While ran as tight loops, and loops were handed back whole");
    kernel_test_measure(test, "methods", written);
    kernel_test_measure(test, "records_run", (uint32_t) records_run);
    kernel_test_measure(test, "tight_turns", (uint32_t) tight_turns);
    kernel_test_measure(test, "handed_back_loops", (uint32_t) loops);
}

/**
 * @brief A tight loop that never ends is stopped past 2^20 back-edges (§6.1) as the loop stops it:
 *        ENDL, `Local0 = 0; While (Local0 < 1) { Local0 = 0 }`, leaves the same fold, its status
 *        included, with records and without. The control: its first turn is decoded, its
 *        second runs from its two records, and every other one up to the failing back-edge, 2^20 - 1,
 *        runs as a tight loop.
 */
KERNEL_TEST(a_tight_loop_that_never_ends_is_stopped_as_the_loop_stops_it)
{
    static const uint8_t endless[] = {
        0x14u, 0x13u, 'E',   'N',   'D',   'L',   0x00u, 0x70u, 0x00u, 0x60u,
        0xA2u, 0x07u, 0x95u, 0x60u, 0x01u, 0x70u, 0x00u, 0x60u, 0xA4u, 0x60u,
    };
    PredecodingRun_t off = {0};
    PredecodingRun_t on = {0};

    if (!predecoding_run(test, endless, sizeof(endless), 2u, "\\ENDL", PREDECODING_NO_ARGUMENT, false, &off) ||
        !predecoding_run(test, endless, sizeof(endless), 2u, "\\ENDL", PREDECODING_NO_ARGUMENT, true, &on))
        return;
    kernel_test_check(test, off.fold == on.fold, "the records leave what the loop leaves");
    kernel_test_check(
        test, on.tight_turns == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_LOOP_ITERATIONS - 1u,
        "2^20 - 1 turns run as a tight loop");
    kernel_test_measure_hexadecimal(test, "fold", on.fold);
}
