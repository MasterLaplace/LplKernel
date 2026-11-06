#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/log.h>
#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_log, KERNEL_TEST_STAGE_INITIALIZATION);

#define LOG_LEVEL(name)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_##name
#define LOG_TABLE_ADDRESS  0x00300000u /**< Where the table Load reads is placed. */
#define LOG_SYSTEM_MEMORY  0x00u       /**< The RegionSpace byte of SystemMemory. */
#define LOG_CHECKSUM_INDEX 9u          /**< The offset of a table's checksum byte. */
#define LOG_LONG_VALUE     300u        /**< Characters of a value no line holds. */

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t log_host;
static AdvancedConfigurationAndPowerInterfaceHost_t log_silent_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t log_load;
static uint8_t log_dsdt[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static uint8_t log_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static MadeUpAml_t log_aml;
static MadeUpAml_t log_body;

/**
 * @brief Prints @p text as one record of the running test.
 */
static void log_print_record(KernelTest_t *test, const char *key, const char *text, uint32_t length)
{
    char record[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_LINE_BYTES + 16u];
    const uint32_t key_length = (uint32_t) strlen(key);

    memcpy(record, key, key_length);
    memcpy(record + key_length, text, length);
    record[key_length + length] = '\0';
    kernel_test_note(test, record);
}

/**
 * @brief Prints each line the host kept as records, so that the host and ring 0 compare them (§8.7): its level,
 *        its event word, then each of its fields as it is.
 */
static void log_print_lines(KernelTest_t *test, const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *host)
{
    const uint32_t kept = (host->log_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_LOG_LINES) ?
                              host->log_count :
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_LOG_LINES;

    for (uint32_t index = 0u; index < kept; ++index)
    {
        const char *text = host->log_lines[index].text;
        const char *level = advanced_configuration_and_power_interface_log_level_name(host->log_lines[index].level);
        uint32_t start = 0u;

        log_print_record(test, "level=", level, (uint32_t) strlen(level));
        while (text[start] != '\0')
        {
            uint32_t end = start;

            while (text[end] != '\0' && text[end] != ' ')
                ++end;
            log_print_record(test, (start == 0u) ? "event=" : "", text + start, end - start);
            start = (text[end] == ' ') ? end + 1u : end;
        }
    }
}

/**
 * @brief Whether the host kept line @p index at @p level, as @p text.
 */
static bool log_line_is(const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *host, uint32_t index,
                        uint8_t level, const char *text)
{
    const AdvancedConfigurationAndPowerInterfaceSimulatedLogLine_t *line = &host->log_lines[index];
    const size_t length = strlen(text);

    return index < host->log_count && line->level == level && strlen(line->text) == length &&
           memcmp(line->text, text, length) == 0;
}

/**
 * @brief Opens a load on @p host and declares @p aml as a DSDT of revision 2 whose checksum fails when
 *        @p checksum_fails says so, its table-level code run, then decodes it.
 */
static bool log_declare(KernelTest_t *test, const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                        const uint8_t *aml, uint32_t length, bool checksum_fails)
{
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &log_load, host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
                           "the namespace opens"))
    {
        advanced_configuration_and_power_interface_machine_language_close(&log_load);
        return false;
    }

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header =
        made_up_definition_block(log_dsdt, "DSDT", 2u, aml, length);

    log_dsdt[LOG_CHECKSUM_INDEX] = (uint8_t) (log_dsdt[LOG_CHECKSUM_INDEX] + (checksum_fails ? 1u : 0u));
    advanced_configuration_and_power_interface_machine_language_declare_table(&log_load, header);
    advanced_configuration_and_power_interface_machine_language_decode_tables(&log_load);
    return true;
}

/**
 * @brief A store to Debug is logged at debug level, as the debug build keeps it (§5.2): an Integer by its value,
 *        a String by its characters, a space written as `_`, and a Buffer by its length and its bytes. The values
 *        are those the AML holds, written by hand.
 */
KERNEL_TEST(a_store_to_debug_is_logged_at_debug_level)
{
    static const uint8_t aml[] = {
        0x70u, 0x0Au, 0x2Au, 0x5Bu, 0x31u, 0x70u, 0x0Du, 'H',   'i',   ' ',   't',   'h',   'e',   'r',
        'e',   0x00u, 0x5Bu, 0x31u, 0x70u, 0x11u, 0x05u, 0x0Au, 0x02u, 0x01u, 0xFEu, 0x5Bu, 0x31u,
    };

    if (!made_up_load_table(test, &log_load, &log_host, log_dsdt, 2u, aml, sizeof(aml)))
        return;
    log_print_lines(test, &log_host);
    kernel_test_check(test, log_load.evaluation.debug_stores == 3u && log_host.log_count == 3u,
                      "three stores to Debug, three lines");
    kernel_test_check(
        test, log_line_is(&log_host, 0u, LOG_LEVEL(DEBUG), "debug_stores kind=Integer value=0x000000000000002A"),
        "Store (0x2A, Debug) logs the Integer in sixteen digits");
    kernel_test_check(test,
                      log_line_is(&log_host, 1u, LOG_LEVEL(DEBUG), "debug_stores kind=String length=8 value=Hi_there"),
                      "Store (\"Hi there\", Debug) logs its characters, the space as _");
    kernel_test_check(test,
                      log_line_is(&log_host, 2u, LOG_LEVEL(DEBUG), "debug_stores kind=Buffer length=2 bytes=01FE"),
                      "Store (Buffer () { 0x01, 0xFE }, Debug) logs its length and its bytes");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief A store to Debug of a Buffer longer than the line shows, of a Package, of a reference and of an empty String
 *        is logged by what each is (§5.2): Buffer (20) {} by its length and its first 16 bytes, Package () { One, 2 }
 *        by its count, RefOf (PKG0) as a reference, and "" by its length alone, so that no field is left empty.
 */
KERNEL_TEST(a_store_to_debug_logs_each_kind_of_value)
{
    static const uint8_t aml[] = {
        0x08u, 'P',   'K',   'G',   '0',   0x12u, 0x03u, 0x01u, 0x01u, 0x70u, 0x11u, 0x03u, 0x0Au,
        0x14u, 0x5Bu, 0x31u, 0x70u, 0x12u, 0x05u, 0x02u, 0x01u, 0x0Au, 0x02u, 0x5Bu, 0x31u, 0x70u,
        0x71u, 'P',   'K',   'G',   '0',   0x5Bu, 0x31u, 0x70u, 0x0Du, 0x00u, 0x5Bu, 0x31u,
    };

    if (!made_up_load_table(test, &log_load, &log_host, log_dsdt, 2u, aml, sizeof(aml)))
        return;
    log_print_lines(test, &log_host);
    kernel_test_check(test, log_load.evaluation.debug_stores == 4u && log_host.log_count == 4u,
                      "four stores to Debug, four lines");
    kernel_test_check(test,
                      log_line_is(&log_host, 0u, LOG_LEVEL(DEBUG),
                                  "debug_stores kind=Buffer length=20 bytes=00000000000000000000000000000000"),
                      "Buffer (20) {} logs its length and its first 16 bytes");
    kernel_test_check(test, log_line_is(&log_host, 1u, LOG_LEVEL(DEBUG), "debug_stores kind=Package count=2"),
                      "Package () { One, 2 } logs its count");
    kernel_test_check(test, log_line_is(&log_host, 2u, LOG_LEVEL(DEBUG), "debug_stores kind=Reference"),
                      "RefOf (PKG0) logs a reference");
    kernel_test_check(test, log_line_is(&log_host, 3u, LOG_LEVEL(DEBUG), "debug_stores kind=String length=0"),
                      "\"\" logs its length and no empty value");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief Each level's call gives the host its level, and a level's name is "unknown" past the three a host is given:
 *        a line written at error, then at warning, reaches the simulated host at each.
 */
KERNEL_TEST(each_level_reaches_the_host_as_itself)
{
    static AdvancedConfigurationAndPowerInterfaceLogLine_t line;

    advanced_configuration_and_power_interface_simulated_host_open(&log_host);
    advanced_configuration_and_power_interface_log_open(&line, "probe");
    advanced_configuration_and_power_interface_log_error(&log_host.host, &line);
    advanced_configuration_and_power_interface_log_warning(&log_host.host, &line);
    advanced_configuration_and_power_interface_log_debug(&log_host.host, &line);
    kernel_test_check(test,
                      log_host.log_count == 3u && log_line_is(&log_host, 0u, LOG_LEVEL(ERROR), "probe") &&
                          log_line_is(&log_host, 1u, LOG_LEVEL(WARNING), "probe") &&
                          log_line_is(&log_host, 2u, LOG_LEVEL(DEBUG), "probe"),
                      "error, warning and debug reach the host as themselves");
    kernel_test_check(
        test,
        memcmp(advanced_configuration_and_power_interface_log_level_name(LOG_LEVEL(WARNING)), "warning", 8u) == 0 &&
            memcmp(advanced_configuration_and_power_interface_log_level_name(0u), "unknown", 8u) == 0 &&
            memcmp(advanced_configuration_and_power_interface_log_level_name(4u), "unknown", 8u) == 0 &&
            memcmp(advanced_configuration_and_power_interface_log_level_name(255u), "unknown", 8u) == 0,
        "warning is named, 0, 4 and 255 are unknown");
}

/**
 * @brief A DSDT whose checksum fails is loaded all the same, counted once and logged once as a warning that names
 *        it (§3.9, §5.1); the same DSDT whose checksum holds logs nothing.
 */
KERNEL_TEST(a_definition_block_whose_checksum_fails_is_logged_as_a_warning)
{
    static const uint8_t aml[] = {0x08u, 'D', 'N', 'A', 'M', 0x01u};

    advanced_configuration_and_power_interface_simulated_host_open(&log_host);
    if (!log_declare(test, &log_host.host, aml, sizeof(aml), true))
        return;
    log_print_lines(test, &log_host);
    kernel_test_check(test, log_load.checksum_bad_loaded == 1u && made_up_root_node(&log_load, "DNAM") != NULL,
                      "the DSDT is declared, and counted once");
    kernel_test_check(test,
                      log_host.log_count == 1u &&
                          log_line_is(&log_host, 0u, LOG_LEVEL(WARNING), "checksum_bad_loaded table=DSDT/MADEUPML"),
                      "one warning names the DSDT by its signature and its OEM table ID");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);

    advanced_configuration_and_power_interface_simulated_host_open(&log_host);
    if (!log_declare(test, &log_host.host, aml, sizeof(aml), false))
        return;
    kernel_test_check(test, log_load.checksum_bad_loaded == 0u && log_host.log_count == 0u,
                      "a DSDT whose checksum holds is neither counted nor logged");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief A table Load copies whose checksum fails is loaded all the same, counted once and logged once as a
 *        warning (§5.1): LDIT loads the SSDT at LOG_TABLE_ADDRESS, its checksum broken by one.
 */
KERNEL_TEST(a_table_load_copies_whose_checksum_fails_is_logged_as_a_warning)
{
    static const uint8_t loads[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', 'R', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t ssdt_aml[] = {0x08u, 'L', 'D', 'E', 'D', 0x0Au, 0x2Au};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *ssdt =
        made_up_definition_block(log_table, "SSDT", 2u, ssdt_aml, sizeof(ssdt_aml));

    log_table[LOG_CHECKSUM_INDEX] = (uint8_t) (log_table[LOG_CHECKSUM_INDEX] + 1u);
    advanced_configuration_and_power_interface_simulated_host_open(&log_host);
    (void) advanced_configuration_and_power_interface_simulated_host_poke(&log_host, LOG_TABLE_ADDRESS, log_table,
                                                                          ssdt->length);
    made_up_aml_begin(&log_aml);
    made_up_aml_region(&log_aml, "TBLR", LOG_SYSTEM_MEMORY, LOG_TABLE_ADDRESS, ssdt->length);
    made_up_aml_name_integer(&log_aml, "HND1", 0u);
    made_up_aml_begin(&log_body);
    made_up_aml_raw(&log_body, loads, sizeof(loads));
    made_up_aml_method(&log_aml, "LDIT", 0x00u, &log_body);
    if (!kernel_test_check(test, !log_aml.overflowed, "the made-up AML fits") ||
        !made_up_load_on(test, &log_load, &log_host.host, log_dsdt, 2u, log_aml.bytes, log_aml.length))
        return;
    kernel_test_check(test, log_host.log_count == 0u, "the DSDT, whose checksum holds, logs nothing");
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_evaluate(&log_load, "\\LDIT", NULL, 0u, &result) ==
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK,
        "LDIT runs");
    advanced_configuration_and_power_interface_machine_language_value_release(&log_load.heap, &log_load.space, &result);
    log_print_lines(test, &log_host);
    kernel_test_check(test,
                      log_load.tables_loaded == 1u && log_load.checksum_bad_loaded == 1u &&
                          made_up_root_node(&log_load, "LDED") != NULL,
                      "the SSDT is loaded, and counted once");
    kernel_test_check(test,
                      log_host.log_count == 1u &&
                          log_line_is(&log_host, 0u, LOG_LEVEL(WARNING), "checksum_bad_loaded table=SSDT/MADEUPML"),
                      "one warning names the SSDT");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
    advanced_configuration_and_power_interface_simulated_host_close(&log_host);
}

/**
 * @brief A host that keeps no log, its log call NULL, loses the lines and nothing else: the checksum and the store
 *        to Debug are counted as with one.
 */
KERNEL_TEST(a_host_without_a_log_loses_the_lines_and_nothing_else)
{
    static const uint8_t aml[] = {0x70u, 0x0Au, 0x2Au, 0x5Bu, 0x31u};

    advanced_configuration_and_power_interface_simulated_host_open(&log_host);
    log_silent_host = log_host.host;
    log_silent_host.log = NULL;
    if (!log_declare(test, &log_silent_host, aml, sizeof(aml), true))
        return;
    kernel_test_check(test, log_load.checksum_bad_loaded == 1u && log_load.evaluation.debug_stores == 1u,
                      "the checksum and the store to Debug are counted");
    kernel_test_check(test, log_host.log_count == 0u, "no line reaches the simulated host");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief A field that a line cannot hold cuts it: the line keeps its 255 characters, the last three "...", and takes
 *        no field after it.
 */
KERNEL_TEST(a_line_too_long_ends_with_three_dots)
{
    static AdvancedConfigurationAndPowerInterfaceLogLine_t line;
    static char value[LOG_LONG_VALUE + 1u];
    const uint32_t last = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_LINE_BYTES - 1u;
    bool kept_the_value = true;

    memset(value, 'a', LOG_LONG_VALUE);
    value[LOG_LONG_VALUE] = '\0';
    advanced_configuration_and_power_interface_log_open(&line, "event");
    advanced_configuration_and_power_interface_log_text(&line, "key", value);
    advanced_configuration_and_power_interface_log_unsigned(&line, "after", 7u);
    for (uint32_t index = 10u; index < last - 3u; ++index)
        kept_the_value = kept_the_value && line.text[index] == 'a';
    kernel_test_check(test, line.cut && line.length == last && line.text[last] == '\0',
                      "the line is cut at 255 characters");
    kernel_test_check(test, memcmp(line.text, "event key=", 10u) == 0 && kept_the_value,
                      "it keeps its event, its key and the value up to the cut");
    kernel_test_check(test, memcmp(line.text + last - 3u, "...", 3u) == 0, "it ends with ...");
}

/**
 * @brief A table is named by its signature and its OEM table ID, the identifier's trailing spaces and NULs left
 *        out and a space inside it written as `_`; decimal and hexadecimal fields are written in full.
 */
KERNEL_TEST(a_line_names_a_table_by_its_signature_and_identifier)
{
    static AdvancedConfigurationAndPowerInterfaceLogLine_t line;
    static AdvancedConfigurationAndPowerInterfaceSdtHeader_t header;

    memcpy(header.signature, "SSDT", 4u);
    memcpy(header.oem_table_id, "A B \0\0\0\0", 8u);
    advanced_configuration_and_power_interface_log_open(&line, "probe");
    advanced_configuration_and_power_interface_log_table(&line, &header);
    advanced_configuration_and_power_interface_log_unsigned(&line, "count", 4294967295u);
    advanced_configuration_and_power_interface_log_hexadecimal(&line, "offset", 0x1A2Bu, 8u);
    kernel_test_check(
        test,
        line.length == strlen("probe table=SSDT/A_B count=4294967295 offset=0x00001A2B") &&
            memcmp(line.text, "probe table=SSDT/A_B count=4294967295 offset=0x00001A2B", line.length) == 0 && !line.cut,
        "probe table=SSDT/A_B count=4294967295 offset=0x00001A2B");
}

/**
 * @brief A fault in a method is logged once as an error that names its table, the offset and the opcode of the term
 *        that met it, and the method (§3.6, §4.5): FindSetLeftBit, which no handler runs, at 0x33 of the table in
 *        \DEV0.MTH0, met as its term is decoded, and Divide by Zero at 0x3F in \DEV0.MTH1, met as it runs, which the
 *        line names rather than the term after its operands; MTH0 run again after it names FindSetLeftBit again. The
 *        offsets are the header's 36 bytes and the AML's, counted by hand.
 */
KERNEL_TEST(a_fault_in_a_method_names_its_table_offset_opcode_and_method)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x82u, 0x1Fu, 'D',   'E', 'V', '0', 0x14u, 0x0Bu, 'M',   'T',   'H',   '0',   0x00u, 0x70u, 0x81u, 0x01u,
        0x00u, 0x60u, 0x14u, 0x0Du, 'M', 'T', 'H', '1',   0x00u, 0x70u, 0x78u, 0x01u, 0x00u, 0x61u, 0x62u, 0x60u,
    };
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t unimplemented;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t divided;

    if (!made_up_load_table(test, &log_load, &log_host, log_dsdt, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, log_host.log_count == 0u, "the load meets no fault");
    unimplemented = advanced_configuration_and_power_interface_machine_language_evaluate(&log_load, "\\DEV0.MTH0", NULL,
                                                                                         0u, &result);
    advanced_configuration_and_power_interface_machine_language_value_release(&log_load.heap, &log_load.space, &result);
    divided = advanced_configuration_and_power_interface_machine_language_evaluate(&log_load, "\\DEV0.MTH1", NULL, 0u,
                                                                                   &result);
    advanced_configuration_and_power_interface_machine_language_value_release(&log_load.heap, &log_load.space, &result);
    (void) advanced_configuration_and_power_interface_machine_language_evaluate(&log_load, "\\DEV0.MTH0", NULL, 0u,
                                                                                &result);
    advanced_configuration_and_power_interface_machine_language_value_release(&log_load.heap, &log_load.space, &result);
    log_print_lines(test, &log_host);
    kernel_test_check(
        test,
        unimplemented == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_UNIMPLEMENTED_OPERATOR &&
            divided == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_DIVIDE_BY_ZERO,
        "MTH0 fails with unimplemented_operator, MTH1 with divide_by_zero");
    kernel_test_check(test, log_host.log_count == 3u, "three faults, three lines");
    kernel_test_check(test,
                      log_line_is(&log_host, 0u, LOG_LEVEL(ERROR),
                                  "unimplemented_operator table=DSDT/MADEUPML offset=0x00000033 opcode=FindSetLeftBit "
                                  "method=\\DEV0.MTH0"),
                      "the first names FindSetLeftBit at 0x33 in \\DEV0.MTH0");
    kernel_test_check(
        test,
        log_line_is(&log_host, 1u, LOG_LEVEL(ERROR),
                    "divide_by_zero table=DSDT/MADEUPML offset=0x0000003F opcode=Divide method=\\DEV0.MTH1"),
        "the second names the Divide that ran, at 0x3F in \\DEV0.MTH1");
    kernel_test_check(test,
                      log_line_is(&log_host, 2u, LOG_LEVEL(ERROR),
                                  "unimplemented_operator table=DSDT/MADEUPML offset=0x00000033 opcode=FindSetLeftBit "
                                  "method=\\DEV0.MTH0"),
                      "the third names FindSetLeftBit at 0x33 again, not the Divide before it");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief A fault of table-level code is logged once, in the scope the code runs in (§4.5, §6.2): FindSetLeftBit at
 *        0x2C, which the interpreter runs in \DEV1 and counts in table_level_failures, and the byte 0x02 at 0x37 in
 *        \DEV2, which the load meets and counts in load_errors; the decoding pass, which meets 0x02 again, counts it
 *        in decode_errors but does not log it twice. AFTR, declared after both, shows the load went on.
 */
KERNEL_TEST(a_fault_of_table_level_code_names_its_scope)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x82u, 0x0Au, 'D', 'E', 'V', '1',   0x70u, 0x81u, 0x01u, 0x00u, 0x60u, 0x5Bu,
        0x82u, 0x06u, 'D',   'E', 'V', '2', 0x02u, 0x08u, 'A',   'F',   'T',   'R',   0x01u,
    };

    if (!made_up_load_table(test, &log_load, &log_host, log_dsdt, 2u, aml, sizeof(aml)))
        return;
    log_print_lines(test, &log_host);
    kernel_test_check(test, made_up_root_node(&log_load, "AFTR") != NULL, "the load goes on to AFTR");
    kernel_test_check(test,
                      log_load.evaluation.failures == 1u && log_load.load_errors == 1u &&
                          log_host.log_count == log_load.evaluation.failures + log_load.load_errors,
                      "one failure of table-level code and one load error, one line each");
    kernel_test_check(test,
                      log_line_is(&log_host, 0u, LOG_LEVEL(ERROR),
                                  "unimplemented_operator table=DSDT/MADEUPML offset=0x0000002C opcode=FindSetLeftBit "
                                  "scope=\\DEV1"),
                      "the first names FindSetLeftBit at 0x2C, in the scope \\DEV1");
    kernel_test_check(test,
                      log_line_is(&log_host, 1u, LOG_LEVEL(ERROR),
                                  "invalid_opcode table=DSDT/MADEUPML offset=0x00000037 opcode=Invalid scope=\\DEV2"),
                      "the second names the byte at 0x37, in the scope \\DEV2");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief A fault the decoding pass meets in a method body is logged, in that method (§3.6): LIVE's Store, cut by the
 *        end of its body at 0x37; the same body in DEAD, which a branch table-level code did not take hides, is counted
 *        in undeclared_faults and not logged, since its names would resolve against objects that do not exist.
 */
KERNEL_TEST(a_fault_the_decoding_pass_meets_names_its_method)
{
    static const uint8_t aml[] = {
        0xA0u, 0x0Au, 0x00u, 0x14u, 0x07u, 'D', 'E', 'A',   'D',   0x00u,
        0x70u, 0x14u, 0x07u, 'L',   'I',   'V', 'E', 0x00u, 0x70u,
    };

    if (!made_up_load_table(test, &log_load, &log_host, log_dsdt, 2u, aml, sizeof(aml)))
        return;
    log_print_lines(test, &log_host);
    kernel_test_check(test, log_load.decode_errors == 1u && log_load.undeclared_faults == 1u,
                      "LIVE's cut Store is a decode error, DEAD's is counted apart");
    kernel_test_check(test,
                      log_host.log_count == 1u &&
                          log_line_is(&log_host, 0u, LOG_LEVEL(ERROR),
                                      "truncated table=DSDT/MADEUPML offset=0x00000037 opcode=End method=\\LIVE"),
                      "one line names the end of LIVE's body at 0x37, in \\LIVE");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief A failure of table-level code the load meets again, as it steps over the statement that failed, is logged
 *        once (§4.5): Store (<0x02>, Local0) at the root fails in the interpreter, counted in table_level_failures, and
 *        the load, which steps over the Store, meets the byte 0x02 at 0x25 again, counted in load_errors.
 */
KERNEL_TEST(a_failure_the_load_steps_over_is_logged_once)
{
    static const uint8_t aml[] = {0x70u, 0x02u, 0x60u, 0x08u, 'A', 'F', 'T', 'R', 0x01u};

    if (!made_up_load_table(test, &log_load, &log_host, log_dsdt, 2u, aml, sizeof(aml)))
        return;
    log_print_lines(test, &log_host);
    kernel_test_check(test, log_load.evaluation.failures == 1u && log_load.load_errors == 1u,
                      "the Store fails, and the load meets its operand");
    kernel_test_check(test,
                      log_host.log_count == 1u &&
                          log_line_is(&log_host, 0u, LOG_LEVEL(ERROR),
                                      "invalid_opcode table=DSDT/MADEUPML offset=0x00000025 opcode=Invalid scope=\\"),
                      "one line names the byte at 0x25, in the root");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief A loop stopped by the budget of a table's code is logged once, at its While (§6.1, §6.2): the endless loop
 *        of M009, which table-level code calls, at 0x2B in \M009, then the two table-level loops the spent budget stops
 *        at their first back-edge, at 0x33 and 0x37 in the root.
 */
KERNEL_TEST(a_loop_the_budget_stops_is_logged_at_its_while)
{
    static const uint8_t aml[] = {
        0x14u, 0x0Au, 'M',   '0',   '0',   '9',   0x00u, 0xA2u, 0x03u, 0x01u, 0xA3u, 'M', '0', '0',   '9',
        0xA2u, 0x03u, 0x01u, 0x9Fu, 0xA2u, 0x03u, 0x01u, 0xA3u, 0x08u, 'A',   'F',   'T', 'R', 0x01u,
    };

    if (!made_up_load_table(test, &log_load, &log_host, log_dsdt, 2u, aml, sizeof(aml)))
        return;
    log_print_lines(test, &log_host);
    kernel_test_check(test, log_host.log_count == 3u, "three loops stopped, three lines");
    kernel_test_check(test,
                      log_line_is(&log_host, 0u, LOG_LEVEL(ERROR),
                                  "limit_loop_iterations table=DSDT/MADEUPML offset=0x0000002B opcode=While "
                                  "method=\\M009"),
                      "the first names the While at 0x2B in \\M009");
    kernel_test_check(
        test,
        log_line_is(&log_host, 1u, LOG_LEVEL(ERROR),
                    "limit_loop_iterations table=DSDT/MADEUPML offset=0x00000033 opcode=While scope=\\") &&
            log_line_is(&log_host, 2u, LOG_LEVEL(ERROR),
                        "limit_loop_iterations table=DSDT/MADEUPML offset=0x00000037 opcode=While scope=\\"),
        "the others name the table-level Whiles at 0x33 and 0x37, in the root");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief The load leaves a failure out of its log only while it steps over the statement that failed: MBAD, which
 *        table-level code calls, fails on the byte 0x02 at 0x2C in its body, and the decoding pass, which decodes
 * MBAD's body once every table is declared, meets that byte again as a decode error, logged as one.
 */
KERNEL_TEST(a_failure_is_left_out_of_the_log_only_while_its_statement_is_stepped_over)
{
    static const uint8_t aml[] = {
        0x14u, 0x09u, 'M', 'B', 'A',   'D', 0x00u, 0x70u, 0x02u, 0x60u,
        'M',   'B',   'A', 'D', 0x08u, 'A', 'F',   'T',   'R',   0x01u,
    };
    static const char line[] = "invalid_opcode table=DSDT/MADEUPML offset=0x0000002C opcode=Invalid method=\\MBAD";

    if (!made_up_load_table(test, &log_load, &log_host, log_dsdt, 2u, aml, sizeof(aml)))
        return;
    log_print_lines(test, &log_host);
    kernel_test_check(test, log_load.evaluation.failures == 1u && log_load.decode_errors == 1u,
                      "the call of MBAD fails, and the decoding pass meets its byte 0x02");
    kernel_test_check(test,
                      log_host.log_count == 2u && log_line_is(&log_host, 0u, LOG_LEVEL(ERROR), line) &&
                          log_line_is(&log_host, 1u, LOG_LEVEL(ERROR), line),
                      "each names the byte at 0x2C in \\MBAD");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}

/**
 * @brief Two more faults §4.5 lists are logged where they are met: the read of FLD0, a field of EmbeddedControl, which
 *        has no handler, at its name 0x43 in \RDEC, and Load from the Buffer BUFF, at 0x5F in \LDBF.
 */
KERNEL_TEST(a_space_without_a_handler_and_a_load_from_a_buffer_are_logged)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x80u, 'R',   'E', 'G', '0',   0x03u, 0x00u, 0x0Au, 0x10u, 0x5Bu, 0x81u, 0x0Bu, 'R',   'E',
        'G',   '0',   0x01u, 'F', 'L', 'D',   '0',   0x08u, 0x14u, 0x0Bu, 'R',   'D',   'E',   'C',   0x00u,
        0xA4u, 'F',   'L',   'D', '0', 0x08u, 'B',   'U',   'F',   'F',   0x11u, 0x05u, 0x0Au, 0x24u, 0x53u,
        0x53u, 0x08u, 'H',   'N', 'D', '1',   0x00u, 0x14u, 0x12u, 'L',   'D',   'B',   'F',   0x00u, 0x5Bu,
        0x20u, '\\',  'B',   'U', 'F', 'F',   '\\',  'H',   'N',   'D',   '1',
    };
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t read;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t loaded;

    if (!made_up_load_table(test, &log_load, &log_host, log_dsdt, 2u, aml, sizeof(aml)))
        return;
    read = advanced_configuration_and_power_interface_machine_language_evaluate(&log_load, "\\RDEC", NULL, 0u, &result);
    advanced_configuration_and_power_interface_machine_language_value_release(&log_load.heap, &log_load.space, &result);
    loaded =
        advanced_configuration_and_power_interface_machine_language_evaluate(&log_load, "\\LDBF", NULL, 0u, &result);
    advanced_configuration_and_power_interface_machine_language_value_release(&log_load.heap, &log_load.space, &result);
    log_print_lines(test, &log_host);
    kernel_test_check(test,
                      read == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_SPACE_UNSUPPORTED &&
                          loaded ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LOAD_FORM_UNSUPPORTED,
                      "RDEC fails with space_unsupported, LDBF with load_form_unsupported");
    kernel_test_check(test,
                      log_host.log_count == 2u &&
                          log_line_is(&log_host, 0u, LOG_LEVEL(ERROR),
                                      "space_unsupported table=DSDT/MADEUPML offset=0x00000043 opcode=NameString "
                                      "method=\\RDEC") &&
                          log_line_is(&log_host, 1u, LOG_LEVEL(ERROR),
                                      "load_form_unsupported table=DSDT/MADEUPML offset=0x0000005F opcode=Load "
                                      "method=\\LDBF"),
                      "one line names FLD0 at 0x43 in \\RDEC, one the Load at 0x5F in \\LDBF");
    advanced_configuration_and_power_interface_machine_language_close(&log_load);
}
