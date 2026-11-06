#include "made_up_aml.h"
#include "predecoding_runs.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_executed, KERNEL_TEST_STAGE_INITIALIZATION);

#define EXECUTED_OPCODE(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##name
#define EXECUTED_STATUS(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define EXECUTED_OPCODES       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT
#define EXECUTED_SPACES        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SPACES
#define EXECUTED_ACCESS_TYPES  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ACCESS_TYPES
#define EXECUTED_MEMORY        0x10000u /**< Where the SystemMemory region of the field test lies. */
#define EXECUTED_REGION_BYTES  0x10u    /**< The length of each region of the field test. */
#define EXECUTED_SYSTEM_MEMORY 0x00u    /**< The RegionSpace byte of SystemMemory. */
#define EXECUTED_EMBEDDED      0x03u    /**< The RegionSpace byte of EmbeddedControl, which no handler serves. */
#define EXECUTED_BYTE_ACCESS   0x01u    /**< The FieldFlags of ByteAcc, NoLock, Preserve. */
#define EXECUTED_DWORD_ACCESS  0x03u    /**< The FieldFlags of DWordAcc, NoLock, Preserve. */

/**
 * @brief An opcode, and how many times a run executes it, counted by hand from the AML.
 */
typedef struct ExecutedCount {
    uint32_t opcode; /**< Its descriptor. */
    uint32_t count;  /**< The operations of it the run executes. */
} ExecutedCount_t;

static MadeUpAml_t executed_aml;
static MadeUpAml_t executed_method;
static MadeUpAml_t executed_construct;
static MadeUpAml_t executed_inner;

static const uint8_t executed_while[] = {0xA2u};
static const uint8_t executed_if[] = {0xA0u};
static const uint8_t executed_else[] = {0xA1u};

/**
 * @brief What the table-level code of executed_write_operations runs as its table loads: `Store (5, VAL0)`, an If
 *        taken and its Else stepped over, an If not taken and its Else entered, and `While (One) { Break }`. The Names
 *        the load declares are no operation of the loop.
 */
static const ExecutedCount_t executed_table_level[] = {
    {EXECUTED_OPCODE(STORE),         1u},
    {EXECUTED_OPCODE(IF),            2u},
    {EXECUTED_OPCODE(LOGICAL_EQUAL), 2u},
    {EXECUTED_OPCODE(ELSE),          1u},
    {EXECUTED_OPCODE(WHILE),         1u},
    {EXECUTED_OPCODE(BREAK),         1u},
};

/**
 * @brief What the table-level code and one evaluation of MAIN run: MAIN's 7 Stores, its two While predicates of 4 turns
 *        each, 6 Increments, 5 Ifs over 5 LEquals, 1 Multiply, 4 Adds, the Noop of the If taken, the LNot, the Else
 *        entered, a Package, an Index and the DerefOf that reads it, and the Return.
 */
static const ExecutedCount_t executed_after_main[] = {
    {EXECUTED_OPCODE(STORE),          8u},
    {EXECUTED_OPCODE(IF),             7u},
    {EXECUTED_OPCODE(LOGICAL_EQUAL),  7u},
    {EXECUTED_OPCODE(ELSE),           2u},
    {EXECUTED_OPCODE(WHILE),          9u},
    {EXECUTED_OPCODE(BREAK),          1u},
    {EXECUTED_OPCODE(LOGICAL_LESS),   8u},
    {EXECUTED_OPCODE(INCREMENT),      6u},
    {EXECUTED_OPCODE(MULTIPLY),       1u},
    {EXECUTED_OPCODE(ADD),            4u},
    {EXECUTED_OPCODE(NOOP),           1u},
    {EXECUTED_OPCODE(LOGICAL_NOT),    1u},
    {EXECUTED_OPCODE(PACKAGE),        1u},
    {EXECUTED_OPCODE(INDEX),          1u},
    {EXECUTED_OPCODE(DEREFERENCE_OF), 1u},
    {EXECUTED_OPCODE(RETURN),         1u},
};

/**
 * @brief Appends `lead { bytes, then tail }` to @p aml: a While, an If or an Else, its predicate first in @p bytes;
 *        @p tail may be NULL.
 */
static void executed_append_construct(MadeUpAml_t *aml, const uint8_t *lead, const uint8_t *bytes, uint32_t length,
                                      const MadeUpAml_t *tail)
{
    static MadeUpAml_t content;

    made_up_aml_begin(&content);
    made_up_aml_raw(&content, bytes, length);
    if (tail != NULL)
        made_up_aml_raw(&content, tail->bytes, tail->length);
    made_up_aml_construct(aml, lead, 1u, &content);
}

/**
 * @brief Writes the table-level code of executed_table_level into executed_aml: `Name (VAL0, 0); Store (5, VAL0);
 *        If (VAL0 == 5) { Name (THEN, 1) } Else { Name (OTHR, 1) }; If (VAL0 == 6) { Name (NOPE, 1) } Else { Name
 *        (ELSN, 1) }; While (One) { Break }`.
 */
static void executed_write_table_level(void)
{
    static const uint8_t store[] = {0x70u, 0x0Au, 0x05u, 'V', 'A', 'L', '0'};
    static const uint8_t equals_five[] = {0x93u, 'V', 'A', 'L', '0', 0x0Au, 0x05u};
    static const uint8_t equals_six[] = {0x93u, 'V', 'A', 'L', '0', 0x0Au, 0x06u};
    static const uint8_t forever_break[] = {0x01u, 0xA5u};

    made_up_aml_begin(&executed_aml);
    made_up_aml_name_integer(&executed_aml, "VAL0", 0u);
    made_up_aml_raw(&executed_aml, store, sizeof(store));
    made_up_aml_begin(&executed_inner);
    made_up_aml_name_integer(&executed_inner, "THEN", 1u);
    executed_append_construct(&executed_aml, executed_if, equals_five, sizeof(equals_five), &executed_inner);
    made_up_aml_begin(&executed_inner);
    made_up_aml_name_integer(&executed_inner, "OTHR", 1u);
    executed_append_construct(&executed_aml, executed_else, NULL, 0u, &executed_inner);
    made_up_aml_begin(&executed_inner);
    made_up_aml_name_integer(&executed_inner, "NOPE", 1u);
    executed_append_construct(&executed_aml, executed_if, equals_six, sizeof(equals_six), &executed_inner);
    made_up_aml_begin(&executed_inner);
    made_up_aml_name_integer(&executed_inner, "ELSN", 1u);
    executed_append_construct(&executed_aml, executed_else, NULL, 0u, &executed_inner);
    executed_append_construct(&executed_aml, executed_while, forever_break, sizeof(forever_break), NULL);
}

/**
 * @brief Writes MAIN into executed_aml: `Local0 = 0; While (Local0 < 3) { Local0++ }; Local0 = 0; Local2 = 0;
 *        While (Local0 < 3) { Local0++; If (Local0 == 2) { Local2 = Local0 * 2 }; Local3 = Local0 + Local2 };
 *        If (Local3 == 7) { Noop } Else { Local3 += 1 }; If (!(Local3 == 7)) { Noop } Else { Local3 += 1 };
 *        Local4 = DerefOf (Index (Package () { 1, 2 }, 1)); Return (Local3)`, so 8: its first loop a tight one, the
 *        Multiply of its second first run from a record on the second turn, its DerefOf read at once (O8).
 */
static void executed_write_main(void)
{
    static const uint8_t clear_first[] = {0x70u, 0x00u, 0x60u};
    static const uint8_t count_to_three[] = {0x95u, 0x60u, 0x0Au, 0x03u, 0x75u, 0x60u};
    static const uint8_t clear_both[] = {0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x62u};
    static const uint8_t double_at_two[] = {0x93u, 0x60u, 0x0Au, 0x02u, 0x77u, 0x60u, 0x0Au, 0x02u, 0x62u};
    static const uint8_t store_sum[] = {0x70u, 0x72u, 0x60u, 0x62u, 0x00u, 0x63u};
    static const uint8_t equals_seven[] = {0x93u, 0x63u, 0x0Au, 0x07u, 0xA3u};
    static const uint8_t differs_from_seven[] = {0x92u, 0x93u, 0x63u, 0x0Au, 0x07u, 0xA3u};
    static const uint8_t add_one[] = {0x72u, 0x63u, 0x01u, 0x63u};
    static const uint8_t read_element[] = {0x70u, 0x83u, 0x88u, 0x12u, 0x05u, 0x02u,
                                           0x01u, 0x0Au, 0x02u, 0x01u, 0x00u, 0x64u};
    static const uint8_t return_sum[] = {0xA4u, 0x63u};

    made_up_aml_begin(&executed_method);
    made_up_aml_raw(&executed_method, clear_first, sizeof(clear_first));
    executed_append_construct(&executed_method, executed_while, count_to_three, sizeof(count_to_three), NULL);
    made_up_aml_raw(&executed_method, clear_both, sizeof(clear_both));
    made_up_aml_begin(&executed_construct);
    executed_append_construct(&executed_construct, executed_if, double_at_two, sizeof(double_at_two), NULL);
    made_up_aml_raw(&executed_construct, store_sum, sizeof(store_sum));
    executed_append_construct(&executed_method, executed_while, count_to_three, sizeof(count_to_three),
                              &executed_construct);
    executed_append_construct(&executed_method, executed_if, equals_seven, sizeof(equals_seven), NULL);
    executed_append_construct(&executed_method, executed_else, add_one, sizeof(add_one), NULL);
    executed_append_construct(&executed_method, executed_if, differs_from_seven, sizeof(differs_from_seven), NULL);
    executed_append_construct(&executed_method, executed_else, add_one, sizeof(add_one), NULL);
    made_up_aml_raw(&executed_method, read_element, sizeof(read_element));
    made_up_aml_raw(&executed_method, return_sum, sizeof(return_sum));
    made_up_aml_method(&executed_aml, "MAIN", 0u, &executed_method);
}

/**
 * @brief Writes the DSDT of the tests on operations: the table-level code of executed_table_level, MAIN, and UNIM,
 *        `Return (Nor (1, 1))`, whose Nor the interpreter does not run (§4.5).
 */
static void executed_write_operations(void)
{
    static const uint8_t return_nor[] = {0xA4u, 0x7Eu, 0x01u, 0x01u, 0x00u};

    executed_write_table_level();
    executed_write_main();
    made_up_aml_begin(&executed_method);
    made_up_aml_raw(&executed_method, return_nor, sizeof(return_nor));
    made_up_aml_method(&executed_aml, "UNIM", 0u, &executed_method);
}

/**
 * @brief Writes the DSDT of the test on field accesses: MEMR, 16 bytes of SystemMemory at EXECUTED_MEMORY, with the
 *        DWordAcc units DW0_ (32 bits) and QW1_ (64 bits, two dwords); ECRG, 16 bytes of EmbeddedControl, with the
 *        ByteAcc unit BADF; and MAIN: `DW0_ = 0x12345678; Local0 = DW0_; Local1 = QW1_; Local2 = BADF`, which fails
 *        at BADF with space_unsupported.
 */
static void executed_write_fields(void)
{
    static const MadeUpFieldUnit_t memory_units[] = {
        {"DW0_", 32u},
        {"QW1_", 64u}
    };
    static const MadeUpFieldUnit_t embedded_units[] = {
        {"BADF", 8u}
    };
    static const uint8_t accesses[] = {
        0x70u, 0x0Cu, 0x78u, 0x56u, 0x34u, 0x12u, 'D', 'W',   '0',   '_', 0x70u, 'D', 'W', '0',
        '_',   0x60u, 0x70u, 'Q',   'W',   '1',   '_', 0x61u, 0x70u, 'B', 'A',   'D', 'F', 0x62u,
    };

    made_up_aml_begin(&executed_aml);
    made_up_aml_region(&executed_aml, "MEMR", EXECUTED_SYSTEM_MEMORY, EXECUTED_MEMORY, EXECUTED_REGION_BYTES);
    made_up_aml_field(&executed_aml, "MEMR", EXECUTED_DWORD_ACCESS, memory_units, 2u);
    made_up_aml_region(&executed_aml, "ECRG", EXECUTED_EMBEDDED, 0u, EXECUTED_REGION_BYTES);
    made_up_aml_field(&executed_aml, "ECRG", EXECUTED_BYTE_ACCESS, embedded_units, 1u);
    made_up_aml_begin(&executed_method);
    made_up_aml_raw(&executed_method, accesses, sizeof(accesses));
    made_up_aml_method(&executed_aml, "MAIN", 0u, &executed_method);
}

/**
 * @brief The operations the open load executed, over every opcode.
 */
static uint32_t executed_total(void)
{
    uint32_t total = 0u;

    for (uint32_t opcode = 0u; opcode < EXECUTED_OPCODES; ++opcode)
        total += predecoding_load.evaluation.executed[opcode];
    return total;
}

/**
 * @brief Checks that the open load executed what @p expected lists, and no other operation.
 */
static void executed_check(KernelTest_t *test, const ExecutedCount_t *expected, uint32_t count, const char *claim)
{
    uint32_t listed = 0u;
    bool same = true;

    for (uint32_t index = 0u; index < count; ++index)
    {
        same = same && predecoding_load.evaluation.executed[expected[index].opcode] == expected[index].count;
        listed += expected[index].count;
    }
    kernel_test_check(test, same && executed_total() == listed, claim);
}

/**
 * @brief Loads executed_aml as a DSDT of revision 2, records on when @p predecodes holds, checks what its
 *        table-level code executed, evaluates MAIN, and checks what the load executed then.
 */
static void executed_run_operations(KernelTest_t *test, bool predecodes)
{
    PredecodingRun_t run = {0};

    if (!predecoding_open(test, 2u, executed_aml.bytes, executed_aml.length, predecodes))
    {
        predecoding_close();
        return;
    }
    executed_check(test, executed_table_level, sizeof(executed_table_level) / sizeof(executed_table_level[0]),
                   "table-level code counts its Store, If, LEqual, the Else it enters, While and Break, and no Name");
    predecoding_evaluate("\\MAIN", PREDECODING_NO_ARGUMENT, &run);
    kernel_test_check(test, run.status == EXECUTED_STATUS(OK) && run.result == 8u, "MAIN returns 8");
    kernel_test_check(test, predecodes == (run.records_run != 0u), "records run only where they are on");
    executed_check(test, executed_after_main, sizeof(executed_after_main) / sizeof(executed_after_main[0]),
                   "MAIN counts each operation it runs, once per run, and no constant, local or untaken branch");
    kernel_test_measure(test, predecodes ? "executed_with_records" : "executed_without_records", executed_total());
    predecoding_close();
}

/**
 * @brief An evaluation counts each operation the loop runs, as the AML says it runs, with the records of
 *        pre-decoded loops and without (§4.1, §3.6): an operator when its operands are there, If and While each
 *        time they read their predicate, an Else when its body is entered, Break and Noop when they run; table-level
 *        code counts what the interpreter runs of it, and the declarations the load makes count nothing.
 */
KERNEL_TEST(an_evaluation_counts_each_operation_it_runs)
{
    executed_write_operations();
    if (!kernel_test_check(test, !executed_aml.overflowed, "the made-up table fits its builder"))
        return;
    executed_run_operations(test, false);
    executed_run_operations(test, true);
}

/**
 * @brief An opcode the interpreter does not run fails as unimplemented, counted there and not as executed, and the
 *        Return waiting for it never runs: UNIM fails with unimplemented_operator, Nor counted once, and nothing is
 *        executed past the table-level code.
 */
KERNEL_TEST(an_opcode_the_loop_does_not_run_is_counted_unimplemented_only)
{
    PredecodingRun_t run = {0};

    executed_write_operations();
    if (!predecoding_open(test, 2u, executed_aml.bytes, executed_aml.length, true))
    {
        predecoding_close();
        return;
    }
    predecoding_evaluate("\\UNIM", PREDECODING_NO_ARGUMENT, &run);
    kernel_test_check(test, run.status == EXECUTED_STATUS(UNIMPLEMENTED_OPERATOR), "UNIM fails as unimplemented");
    kernel_test_check(test, predecoding_load.evaluation.unimplemented[EXECUTED_OPCODE(NOR)] == 1u,
                      "its Nor is counted unimplemented once");
    executed_check(test, executed_table_level, sizeof(executed_table_level) / sizeof(executed_table_level[0]),
                   "UNIM executes nothing: neither Nor nor the Return waiting for it");
    predecoding_close();
}

/**
 * @brief The run list says which opcodes the needs path executes (§4.1): in a method, the operators the interpreter
 *        runs and the constants, locals and arguments it reads, never a declaration the load makes, but for a Mutex
 *        or an Event, its temporaries (§5.1), or an opcode that fails as unimplemented; at table level, those and
 *        every declaration the load takes, never code the interpreter does not run, nor a data object it does not
 *        build, such as Revision, nor a DataTableRegion, whose statement the interpreter refuses (§4.5). The opcodes
 *        a row of §4.5, §5.2 or §6.3 gives a behaviour of their own run where they stand.
 */
KERNEL_TEST(the_run_list_says_which_opcodes_the_needs_path_executes)
{
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(STORE), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(WHILE), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(ELSE), false),
        "a method runs Store, While and Else");
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(ZERO), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(LOCAL7), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(ARGUMENT6), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(DEBUG), false),
        "a method reads Zero, Local7, Arg6 and Debug");
    kernel_test_check(
        test,
        !advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(NOR), false) &&
            !advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(REVISION), false) &&
            !advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(METHOD), false),
        "a method runs neither Nor, Revision nor a Method declared inside it");
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(MUTEX), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(EVENT), false),
        "a method declares a Mutex and an Event, its temporaries (§5.1)");
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(METHOD), true) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(DEVICE), true) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(STORE), true),
        "table-level code declares a Method and a Device, and runs Store");
    kernel_test_check(
        test,
        !advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(NOR), true) &&
            !advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(INVALID), true),
        "table-level code runs no Nor, and no byte that is no opcode");
    kernel_test_check(
        test,
        !advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(REVISION), true) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(PACKAGE), true) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(STRING_PREFIX), true),
        "table-level code builds a Package and a String, but no Revision, which no one builds");
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(UNLOAD), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(FATAL), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(BREAK_POINT), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(WAIT), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(RESET), false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(FIND_SET_LEFT_BIT),
                                                                                 false) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(FIND_SET_RIGHT_BIT),
                                                                                 false),
        "a method runs Unload, Fatal, BreakPoint, Wait, Reset, FindSetLeftBit and FindSetRightBit");
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(BREAK_POINT), true) &&
            advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(FATAL), true),
        "table-level code runs BreakPoint and Fatal");
    kernel_test_check(
        test,
        !advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(DATA_REGION), true) &&
            !advanced_configuration_and_power_interface_machine_language_executes(EXECUTED_OPCODE(DATA_REGION), false),
        "a DataTableRegion runs neither at table level, where the load does not take it, nor in a method");
}

/**
 * @brief Field accesses are counted per region space and per AccessType (§4.1): MAIN's write of DW0_ and its read are
 *        one dword access each, its read of QW1_ two, all in SystemMemory, and its read of BADF one byte access in
 *        EmbeddedControl, refused; DW0_ and QW1_ are DWordAcc units accessed three times, BADF a ByteAcc unit once.
 *        The Store whose source fails is not counted.
 */
KERNEL_TEST(field_accesses_count_per_region_space_and_access_type)
{
    PredecodingRun_t run = {0};
    uint32_t spaces = 0u;
    uint32_t access_types = 0u;

    executed_write_fields();
    if (!predecoding_open(test, 2u, executed_aml.bytes, executed_aml.length, true))
    {
        predecoding_close();
        return;
    }
    predecoding_evaluate("\\MAIN", PREDECODING_NO_ARGUMENT, &run);

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t *regions = &predecoding_load.regions;

    for (uint32_t space = 0u; space < EXECUTED_SPACES; ++space)
        spaces += regions->space_accesses[space];
    for (uint32_t access = 0u; access < EXECUTED_ACCESS_TYPES; ++access)
        access_types += regions->type_accesses[access];
    kernel_test_check(test, run.status == EXECUTED_STATUS(SPACE_UNSUPPORTED), "MAIN fails at BADF");
    kernel_test_check(test,
                      regions->space_accesses[EXECUTED_SYSTEM_MEMORY] == 4u &&
                          regions->space_accesses[EXECUTED_EMBEDDED] == 1u && spaces == 5u,
                      "four dword accesses in SystemMemory, one refused in EmbeddedControl, none elsewhere");
    kernel_test_check(test,
                      regions->type_accesses[EXECUTED_DWORD_ACCESS] == 3u &&
                          regions->type_accesses[EXECUTED_BYTE_ACCESS] == 1u && access_types == 4u,
                      "three accesses of DWordAcc units, one of a ByteAcc unit, none of another type");
    kernel_test_check(test,
                      predecoding_load.evaluation.executed[EXECUTED_OPCODE(STORE)] == 3u && executed_total() == 3u,
                      "the three Stores that ran are counted, not the one whose source failed");
    predecoding_close();
}
