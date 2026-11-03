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
 * @brief Appends one statement: a store, a step or an operator into a data local, a construct, a Break
 *        or a Continue inside a While, or one that hands records back: a String stored to a local.
 */
static void programs_statement(ProgramsWriter_t *writer, MadeUpAml_t *aml)
{
    const uint32_t choice = programs_pick(writer, 12u);
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
        programs_byte(aml, local);
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
 * @brief Writes MAIN (Arg0): its data locals set to constants, then a While of a few turns whose body
 *        the writer draws, then the sum of the data locals returned.
 */
static void programs_write(ProgramsWriter_t *writer)
{
    static const uint8_t finish[] = {0xA4u, 0x72u, 0x72u, 0x60u, 0x61u, 0x00u, 0x72u, 0x62u, 0x63u, 0x00u, 0x00u};
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
 * @brief Methods written from a fixed seed, with While, If, Else, Break and Continue nested in loops of a
 *        few turns, operators, comparisons and LNot over locals, Arg0 (its 32 high bits set but for the
 *        first) and constants of every width, and
 *        statements that hand records back, leave what the loop leaves when their loops run from records:
 *        each runs with records off, then on, under both integer widths, and both leave the same fold.
 *        The control: records ran, and were handed back, over the methods.
 */
KERNEL_TEST(methods_written_at_random_leave_what_the_loop_leaves)
{
    ProgramsWriter_t writer = {PROGRAMS_SEED, 0u, false};
    uint32_t agree = 0u;
    uint32_t written = 0u;
    uint64_t records_run = 0u;
    uint64_t declined = 0u;

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
    }
    kernel_test_check(test, written > PROGRAMS_COUNT / 2u, "most methods fit their builder");
    kernel_test_check(test, agree == written, "every method leaves with records what it leaves without");
    kernel_test_check(test, records_run != 0u && declined != 0u, "records ran, and were handed back");
    kernel_test_measure(test, "methods", written);
    kernel_test_measure(test, "records_run", (uint32_t) records_run);
}
