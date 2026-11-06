#include "fixtures/windows_identity.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_windows_identity, KERNEL_TEST_STAGE_INITIALIZATION);

#define IDENTITY_TABLE_CAPACITY 2048u                 /**< Bytes of the buffer a made-up DSDT is written in. */
#define IDENTITY_ONES           0xFFFFFFFFFFFFFFFFull /**< Ones, and True, under a revision-2 table. */
#define IDENTITY_WINDOWS        0x7FFFFFu             /**< One bit per Windows string, "Windows 2000" to "2022". */
#define IDENTITY_WINDOWS_2015   14u                   /**< The version "Windows 2015" names, counted from 1. */
#define IDENTITY_WINDOWS_2022   23u                   /**< The version "Windows 2022" names, the newest. */
#define IDENTITY_STATUS(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name

static uint8_t identity_table[IDENTITY_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t identity_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t identity_load;

/**
 * @brief Loads @p aml as a DSDT of @p revision on a fresh simulated host.
 */
static bool identity_run(KernelTest_t *test, uint8_t revision, const uint8_t *aml, uint32_t length)
{
    return made_up_load_table(test, &identity_load, &identity_host, identity_table, revision, aml, length);
}

/**
 * @brief Evaluates the method at @p path, which takes no argument, and gives its Integer, or 0xDEAD;
 *        its status goes to @p out_status.
 */
static uint64_t identity_evaluate(const char *path,
                                  AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t *out_status)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    uint64_t integer = 0xDEADu;

    *out_status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&identity_load, path, NULL, 0u, &result);
    if (*out_status == IDENTITY_STATUS(OK) &&
        result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        integer = result.payload;
    advanced_configuration_and_power_interface_machine_language_value_release(&identity_load.heap, &identity_load.space,
                                                                              &result);
    return integer;
}

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD.
 */
static uint64_t identity_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&identity_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief Writes to @p port the access log of the simulated host holds, of any width.
 */
static uint32_t identity_port_writes(uint16_t port)
{
    uint32_t writes = 0u;

    for (uint32_t index = 0u; index < identity_host.access_count &&
                              index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES;
         ++index)
        writes +=
            (identity_host.accesses[index].kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_WRITE &&
             identity_host.accesses[index].port == port) ?
                1u :
                0u;
    return writes;
}

/**
 * @brief Closes the load and gives the simulated memory back.
 */
static void identity_close(void)
{
    advanced_configuration_and_power_interface_machine_language_close(&identity_load);
    advanced_configuration_and_power_interface_simulated_host_close(&identity_host);
}

/**
 * @brief `\_OSI` answers Ones for each of the 23 Windows strings, "Windows 2000" to "Windows 2022", and
 *        0 for every other string, "Linux", the vendor and feature strings, "Extended Address Space
 *        Descriptor" and "Processor Aggregator Device" included (§5.4); the table's own code calls it
 *        too, and the newest Windows string asked is recorded.
 */
KERNEL_TEST(osi_answers_true_for_every_windows_string_and_false_for_any_other)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = IDENTITY_STATUS(OK);

    if (!identity_run(test, 2u, identity_osi_aml, sizeof(identity_osi_aml)))
        return;
    kernel_test_check(test, identity_integer("TRU_") == IDENTITY_ONES, "the table's code reads Ones for Windows 2015");
    kernel_test_check(test, identity_evaluate("\\WINS", &status) == IDENTITY_WINDOWS && status == IDENTITY_STATUS(OK),
                      "every one of the 23 Windows strings answers Ones");
    kernel_test_check(test, identity_evaluate("\\OTHR", &status) == 0u && status == IDENTITY_STATUS(OK),
                      "none of the 20 other strings answers anything but 0");
    kernel_test_check(test, identity_load.services.windows_version == IDENTITY_WINDOWS_2022,
                      "the newest Windows asked is Windows 2022");
    kernel_test_check(test,
                      identity_load.services.operating_system_queries == 44u &&
                          identity_load.services.operating_system_queries_denied == 20u,
                      "44 queries, 20 of them answered False");
    kernel_test_measure(test, "queries", identity_load.services.operating_system_queries);
    kernel_test_measure(test, "windows_version", identity_load.services.windows_version);
    identity_close();
}

/**
 * @brief Under a revision-1 DSDT, the True `\_OSI` gives is Ones masked to 32 bits.
 */
KERNEL_TEST(osi_answers_ones_masked_to_32_bits_under_revision_1)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = IDENTITY_STATUS(OK);

    if (!identity_run(test, 1u, identity_osi_aml, sizeof(identity_osi_aml)))
        return;
    kernel_test_check(test, identity_integer("TRU_") == 0xFFFFFFFFu, "TRU_ holds 0xFFFFFFFF");
    kernel_test_check(test, identity_evaluate("\\WINS", &status) == IDENTITY_WINDOWS,
                      "each Windows string still compares equal to Ones");
    identity_close();
}

/**
 * @brief A ladder that asks `\_OSI` for each Windows year, then for "Linux" and "FreeBSD", reads the
 *        newest Windows, 2022: a ladder told True for "Linux" would read 1000, the control of §8.7.
 */
KERNEL_TEST(a_firmware_ladder_reads_the_newest_windows)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = IDENTITY_STATUS(OK);

    if (!identity_run(test, 2u, identity_ladder_aml, sizeof(identity_ladder_aml)))
        return;
    (void) identity_evaluate("\\_SB_._INI", &status);
    kernel_test_check(test, status == IDENTITY_STATUS(OK), "\\_SB._INI runs");
    kernel_test_check(test, identity_integer("OSYS") == 2022u, "OSYS reads 2022: no Linux nor FreeBSD path");
    kernel_test_check(test,
                      identity_load.services.windows_version == IDENTITY_WINDOWS_2022 &&
                          identity_load.services.operating_system_queries == 10u &&
                          identity_load.services.operating_system_queries_denied == 2u,
                      "ten queries, the two non-Windows ones denied, Windows 2022 the newest asked");
    kernel_test_measure(test, "osys", (uint32_t) identity_integer("OSYS"));
    identity_close();
}

/**
 * @brief Loads the ladder @p aml as a revision-2 DSDT, runs `\_SB._INI`, and folds the evaluations record (§8.7) of
 *        OSYS, which must evaluate; 0 when the table does not load.
 */
static uint32_t identity_ladder_fold(KernelTest_t *test, const uint8_t *aml, uint32_t length)
{
    static const char *const paths[] = {"\\OSYS"};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = IDENTITY_STATUS(OK);
    uint32_t succeeded = 0u;

    if (!identity_run(test, 2u, aml, length))
        return 0u;
    (void) identity_evaluate("\\_SB_._INI", &status);

    const uint32_t fold = advanced_configuration_and_power_interface_machine_language_evaluations_fold(
        &identity_load, paths, 1u, &succeeded);

    kernel_test_check(test, status == IDENTITY_STATUS(OK) && succeeded == 1u, "\\_SB._INI runs and OSYS evaluates");
    identity_close();
    return fold;
}

/**
 * @brief The control of §8.7: the ladder whose "Linux" line is taken reads 1000, not 2022, so OSYS, the
 *        record the ladder test measures, tells a Linux answer from a Windows one, and so does the evaluations
 *        record of OSYS.
 */
KERNEL_TEST(a_ladder_that_takes_its_linux_line_reads_another_year)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = IDENTITY_STATUS(OK);

    if (!identity_run(test, 2u, identity_ladder_linux_aml, sizeof(identity_ladder_linux_aml)))
        return;
    (void) identity_evaluate("\\_SB_._INI", &status);
    kernel_test_check(test, status == IDENTITY_STATUS(OK) && identity_integer("OSYS") == 1000u,
                      "OSYS reads 1000, the Linux year, where the ladder reads 2022");
    kernel_test_measure(test, "osys", (uint32_t) identity_integer("OSYS"));
    identity_close();

    const uint32_t windows_fold = identity_ladder_fold(test, identity_ladder_aml, sizeof(identity_ladder_aml));
    const uint32_t linux_line_fold =
        identity_ladder_fold(test, identity_ladder_linux_aml, sizeof(identity_ladder_linux_aml));

    kernel_test_check(test, windows_fold != linux_line_fold,
                      "the evaluations record of OSYS tells the Linux line from Windows");
    kernel_test_measure_hexadecimal(test, "evaluations_fold_windows", windows_fold);
    kernel_test_measure_hexadecimal(test, "evaluations_fold_linux", linux_line_fold);
}

/**
 * @brief The Windows version recorded is the newest asked, neither the first nor the last: Windows
 *        2001, Windows 2015, Windows 2009, then Linux, leave Windows 2015.
 */
KERNEL_TEST(osi_keeps_the_newest_windows_asked)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = IDENTITY_STATUS(OK);

    if (!identity_run(test, 2u, identity_osi_aml, sizeof(identity_osi_aml)))
        return;
    identity_load.services.windows_version = 0u;
    (void) identity_evaluate("\\NEWS", &status);
    kernel_test_check(test,
                      status == IDENTITY_STATUS(OK) && identity_load.services.windows_version == IDENTITY_WINDOWS_2015,
                      "Windows 2015 stays the newest");
    identity_close();
}

/**
 * @brief `\_OSI` given an Integer fails the evaluation with type_mismatch, as ACPICA and uACPI refuse it.
 */
KERNEL_TEST(osi_refuses_an_argument_that_is_no_string)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = IDENTITY_STATUS(OK);

    if (!identity_run(test, 2u, identity_osi_aml, sizeof(identity_osi_aml)))
        return;
    (void) identity_evaluate("\\BADA", &status);
    kernel_test_check(test, status == IDENTITY_STATUS(TYPE_MISMATCH), "BADA fails with type_mismatch");
    identity_close();
}

/**
 * @brief The RTC, the DMA and the other ports Windows' ACPI driver protects are reachable until AML
 *        asks `\_OSI` for Windows XP or newer, and protected afterwards: a read gives 0, a write is
 *        dropped, counted; Windows 2000 does not count; the PIC is protected throughout; only the first
 *        port of an access is checked, and the RTC's neighbours stay reachable (§5.3).
 */
KERNEL_TEST(the_rtc_is_protected_once_osi_asks_for_windows_xp)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = IDENTITY_STATUS(OK);
    static const struct {
        uint16_t port;
        uint32_t value;
    } ports[] = {
        {0x0020u, 0x11u      },
        {0x006Eu, 0xAABBCCDDu},
        {0x006Fu, 0xCCu      },
        {0x0070u, 0xBBu      },
        {0x0072u, 0x77u      },
        {0x000Fu, 0xBEEFu    },
    };

    advanced_configuration_and_power_interface_simulated_host_open(&identity_host);
    for (uint32_t index = 0u; index < sizeof(ports) / sizeof(ports[0]); ++index)
        advanced_configuration_and_power_interface_simulated_host_set_port(&identity_host, ports[index].port,
                                                                           ports[index].value);
    if (!made_up_load_on(test, &identity_load, &identity_host.host, identity_table, 2u, identity_ports_aml,
                         sizeof(identity_ports_aml)))
        return;
    kernel_test_check(test, identity_evaluate("\\RTCR", &status) == 0xBBu, "before any _OSI, the RTC index reads");
    kernel_test_check(test, identity_evaluate("\\DMAW", &status) == 0xBEEFu, "and so does the last DMA port");
    kernel_test_check(test, identity_evaluate("\\PICR", &status) == 0u, "the PIC reads 0 throughout");
    kernel_test_check(test, identity_evaluate("\\A200", &status) == IDENTITY_ONES, "Windows 2000 is answered True");
    kernel_test_check(test, identity_evaluate("\\RTCR", &status) == 0xBBu, "Windows 2000 leaves the RTC reachable");
    kernel_test_check(test, identity_evaluate("\\A201", &status) == IDENTITY_ONES, "Windows 2001 is answered True");
    kernel_test_check(test, identity_evaluate("\\RTCR", &status) == 0u, "from Windows XP on, the RTC index reads 0");
    kernel_test_check(test, identity_evaluate("\\PICR", &status) == 0u, "the PIC still reads 0");
    kernel_test_check(test, identity_evaluate("\\DMAW", &status) == 0u,
                      "a word that starts at the last DMA port reads 0");
    (void) identity_evaluate("\\WRTC", &status);
    kernel_test_check(test, status == IDENTITY_STATUS(OK) && identity_port_writes(0x0070u) == 0u,
                      "a write to the RTC index is dropped, and the evaluation goes on");
    kernel_test_check(test, identity_evaluate("\\NEIG", &status) == 0xCC77u, "ports 0x6F and 0x72 still read");
    kernel_test_check(test, identity_evaluate("\\DWRD", &status) == 0xAABBCCDDu,
                      "a double word from 0x6E, which only runs into the RTC, goes through");
    kernel_test_check(test, identity_load.regions.protected_port_accesses == 5u,
                      "five accesses refused: two reads of the PIC, a read and a write of the RTC index, a DMA word");
    kernel_test_measure(test, "protected_port_accesses", identity_load.regions.protected_port_accesses);
    identity_close();
}
