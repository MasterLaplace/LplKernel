#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_peripheral_component_interconnect_configuration,
                  KERNEL_TEST_STAGE_INITIALIZATION);

#define PCI_CONFIG       0x02u       /**< The RegionSpace byte of PCI_Config. */
#define PCI_BYTE_ACCESS  0x01u       /**< FieldFlags AccessType ByteAcc. */
#define PCI_WORD_ACCESS  0x02u       /**< FieldFlags AccessType WordAcc. */
#define PCI_HOST_BRIDGE  0x030AD041u /**< EisaId ("PNP0A03"), as ACPI 6.6 §19.3.4 packs it. */
#define PCI_EXPRESS_ROOT 0x080AD041u /**< EisaId ("PNP0A08"). */
#define PCI_STATUS(name) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define PCI_ACCESS(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_##kind
#define PCI_FUNCTION(segment, bus, device, function)                                                                   \
    (((uint64_t) (segment) << 16u) | ((uint64_t) (bus) << 8u) | ((uint64_t) (device) << 3u) |                          \
     (uint64_t) (function))                        /**< A function as the simulated host's log writes it. */
#define PCI_BRIDGE PCI_FUNCTION(0u, 2u, 0x1Cu, 0u) /**< \PCI0.BR1_: device 0x1C on the root's bus 2. */
#define PCI_DEVICE PCI_FUNCTION(0u, 5u, 3u, 1u)    /**< \PCI0.BR1_.DEV_: device 3, function 1, on bus 5. */

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t pci_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t pci_load;
static uint8_t pci_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static MadeUpAml_t pci_aml;
static MadeUpAml_t pci_root_body;
static MadeUpAml_t pci_bridge_body;
static MadeUpAml_t pci_device_body;
static MadeUpAml_t pci_method_body;
static MadeUpAml_t pci_inner;
static MadeUpAml_t pci_extra;
static uint8_t pci_bridge_space[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_CONFIGURATION_BYTES];
static uint8_t pci_device_space[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_CONFIGURATION_BYTES];
static uint8_t pci_other_space[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_CONFIGURATION_BYTES];

/**
 * @brief Opens the simulated host with the bridge \PCI0.BR1_, a multi-function PCI-to-PCI bridge
 *        whose secondary bus is 5, and its function \PCI0.BR1_.DEV_, of vendor 0x8086 and a type-0
 *        header; empties the AML and the builders, for a test to give them what it needs.
 */
static void pci_begin(void)
{
    advanced_configuration_and_power_interface_simulated_host_open(&pci_host);
    made_up_aml_begin(&pci_aml);
    made_up_aml_begin(&pci_inner);
    made_up_aml_begin(&pci_extra);
    memset(pci_bridge_space, 0, sizeof(pci_bridge_space));
    memset(pci_device_space, 0, sizeof(pci_device_space));
    memset(pci_other_space, 0, sizeof(pci_other_space));
    pci_bridge_space[0x0Eu] = 0x81u;
    pci_bridge_space[0x19u] = 0x05u;
    pci_device_space[0x00u] = 0x86u;
    pci_device_space[0x01u] = 0x80u;
    advanced_configuration_and_power_interface_simulated_host_add_function(&pci_host, 0u, 2u, 0x1Cu, 0u,
                                                                           pci_bridge_space);
    advanced_configuration_and_power_interface_simulated_host_add_function(&pci_host, 0u, 5u, 3u, 1u, pci_device_space);
}

/**
 * @brief Loads the AML written as a DSDT of revision 2 through the simulated host.
 */
static bool pci_run(KernelTest_t *test)
{
    if (!kernel_test_check(test, !pci_aml.overflowed, "the made-up AML fits"))
        return false;
    return made_up_load_on(test, &pci_load, &pci_host.host, pci_table, 2u, pci_aml.bytes, pci_aml.length);
}

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t pci_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&pci_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief Whether access @p index of the log is a PCI access of @p kind and @p width to @p function at
 *        @p offset, with @p value.
 */
static bool pci_access(uint32_t index, uint8_t kind, uint8_t width, uint64_t function, uint16_t offset, uint32_t value)
{
    const AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t *access = &pci_host.accesses[index];

    return index < pci_host.access_count &&
           index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES && access->kind == kind &&
           access->width == width && access->address == function && access->port == offset && access->value == value;
}

/**
 * @brief Closes the load and checks that no frame outlived its evaluation.
 */
static void pci_close(KernelTest_t *test)
{
    kernel_test_check(test, pci_load.evaluation.frame_count == 0u && pci_load.evaluation.operator_count == 0u,
                      "every frame and operator, internal ones included, is gone once the load ends");
    advanced_configuration_and_power_interface_machine_language_close(&pci_load);
    advanced_configuration_and_power_interface_simulated_host_close(&pci_host);
}

/**
 * @brief Appends Method (@p name) { Increment (\@p counter); Return (@p value) }, or without the
 *        Increment when @p counter is NULL.
 */
static void pci_method_returning(MadeUpAml_t *aml, const char *name, uint64_t value, const char *counter)
{
    static const uint8_t increment_from_root[] = {0x75u, '\\'};
    static const uint8_t return_byte = 0xA4u;

    made_up_aml_begin(&pci_method_body);
    if (counter != NULL)
    {
        made_up_aml_raw(&pci_method_body, increment_from_root, sizeof(increment_from_root));
        made_up_aml_name(&pci_method_body, counter);
    }
    made_up_aml_raw(&pci_method_body, &return_byte, 1u);
    made_up_aml_integer(&pci_method_body, value);
    made_up_aml_method(aml, name, 0x00u, &pci_method_body);
}

/**
 * @brief Writes \PCI0, a PCI Express root bridge whose _BBN is a method that counts its calls in \BBNC
 *        and gives bus 2; below it \PCI0.BR1_ at device 0x1C, holding @p bridge_terms; and below that
 *        \PCI0.BR1_.DEV_, whose _ADR is a method that counts its calls in \ADRC and gives device 3,
 *        function 1, holding @p device_terms.
 */
static void pci_write_tree(const MadeUpAml_t *bridge_terms, const MadeUpAml_t *device_terms)
{
    made_up_aml_name_integer(&pci_aml, "BBNC", 0u);
    made_up_aml_name_integer(&pci_aml, "ADRC", 0u);
    made_up_aml_begin(&pci_device_body);
    pci_method_returning(&pci_device_body, "_ADR", 0x00030001u, "ADRC");
    made_up_aml_raw(&pci_device_body, device_terms->bytes, device_terms->length);
    made_up_aml_begin(&pci_bridge_body);
    made_up_aml_name_integer(&pci_bridge_body, "_ADR", 0x001C0000u);
    made_up_aml_raw(&pci_bridge_body, bridge_terms->bytes, bridge_terms->length);
    made_up_aml_device(&pci_bridge_body, "DEV_", &pci_device_body);
    made_up_aml_begin(&pci_root_body);
    made_up_aml_name_integer(&pci_root_body, "_HID", PCI_EXPRESS_ROOT);
    made_up_aml_name_integer(&pci_root_body, "_CID", PCI_HOST_BRIDGE);
    pci_method_returning(&pci_root_body, "_BBN", 2u, "BBNC");
    made_up_aml_name_integer(&pci_root_body, "_SEG", 0u);
    made_up_aml_device(&pci_root_body, "BR1_", &pci_bridge_body);
    made_up_aml_device(&pci_aml, "PCI0", &pci_root_body);
    pci_aml.overflowed = pci_aml.overflowed || bridge_terms->overflowed || device_terms->overflowed;
}

/**
 * @brief Appends OperationRegion (@p region, PCI_Config, 0, 0x100) and a ByteAcc Field of @p units.
 */
static void pci_region(MadeUpAml_t *aml, const char *region, const MadeUpFieldUnit_t *units, uint32_t count)
{
    made_up_aml_region(aml, region, PCI_CONFIG, 0u, 0x100u);
    made_up_aml_field(aml, region, PCI_BYTE_ACCESS, units, count);
}

/**
 * @brief A field of a PCI_Config region reads its function, found from the root bridge's _BBN, then the
 *        _ADR of each device on the way down, the bridge among them moving the bus to its secondary
 *        one; the methods among those objects run in internal frames once, at the first access, and the
 *        second access reads the function at once (O13).
 */
KERNEL_TEST(a_region_reaches_its_function_through_the_root_bridge_and_its_bridges)
{
    static const MadeUpFieldUnit_t units[] = {
        {"VID_", 16u},
    };

    pci_begin();
    made_up_aml_name_integer(&pci_aml, "V1__", 0u);
    made_up_aml_name_integer(&pci_aml, "V2__", 0u);
    pci_region(&pci_inner, "PCFG", units, 1u);
    made_up_aml_store_name(&pci_inner, "VID_", "V1__");
    made_up_aml_store_name(&pci_inner, "VID_", "V2__");
    pci_write_tree(&pci_extra, &pci_inner);
    if (!pci_run(test))
        return;
    kernel_test_check(
        test, pci_integer("V1__") == 0x8086u && pci_integer("V2__") == 0x8086u && pci_load.evaluation.failures == 0u,
        "both reads of VID_ give the vendor of the function behind the bridge");
    kernel_test_check(test,
                      pci_integer("BBNC") == 1u && pci_load.regions.internal_evaluations == 2u &&
                          pci_load.regions.pci_attachments == 1u,
                      "_BBN and the device's _ADR run once each, in internal frames, for one attach");
    kernel_test_check(
        test,
        pci_host.access_count == 7u &&
            pci_access(0u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_BRIDGE, 0x0Eu, 0x81u) &&
            pci_access(1u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_BRIDGE, 0x19u, 0x05u) &&
            pci_access(2u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_DEVICE, 0x0Eu, 0x00u) &&
            pci_access(3u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_DEVICE, 0x00u, 0x86u) &&
            pci_access(4u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_DEVICE, 0x01u, 0x80u),
        "the attach reads the bridge's header type and secondary bus, then the function's header "
        "type; each read of VID_ is two byte reads of the function");
    pci_close(test);
}

/**
 * @brief A write below the subsystem vendor ID or from the expansion ROM to the interrupt line of a
 *        type-0 header is dropped and counted, while the subsystem vendor ID, the interrupt line and
 *        what follows the header are written; every byte of a bridge's header is protected. The first
 *        access is a write, whose attach runs an _ADR method that stores, and completes (§6.3).
 */
KERNEL_TEST(writes_to_a_function_s_protected_header_are_dropped_and_counted)
{
    static const MadeUpFieldUnit_t device_units[] = {
        {NULL,   32u },
        {"CMD_", 8u  },
        {NULL,   312u},
        {"SVID", 8u  },
        {NULL,   24u },
        {"ROM_", 8u  },
        {NULL,   88u },
        {"LINE", 8u  },
        {NULL,   24u },
        {"PRIV", 8u  },
    };
    static const MadeUpFieldUnit_t bridge_units[] = {
        {NULL,   352u},
        {"BSVI", 8u  },
        {NULL,   152u},
        {"BPRI", 8u  },
    };

    pci_begin();
    pci_device_space[0x04u] = 0x07u;
    pci_device_space[0x30u] = 0x22u;
    pci_region(&pci_extra, "BCFG", bridge_units, 4u);
    made_up_aml_store_integer(&pci_extra, 0xEEu, "BSVI");
    made_up_aml_store_integer(&pci_extra, 0xEFu, "BPRI");
    pci_region(&pci_inner, "PCFG", device_units, 10u);
    made_up_aml_store_integer(&pci_inner, 0xFFu, "CMD_");
    made_up_aml_store_integer(&pci_inner, 0xAAu, "SVID");
    made_up_aml_store_integer(&pci_inner, 0xBBu, "ROM_");
    made_up_aml_store_integer(&pci_inner, 0xCCu, "LINE");
    made_up_aml_store_integer(&pci_inner, 0xDDu, "PRIV");
    pci_write_tree(&pci_extra, &pci_inner);
    if (!pci_run(test))
        return;
    kernel_test_check(test,
                      pci_device_space[0x04u] == 0x07u && pci_device_space[0x30u] == 0x22u &&
                          pci_device_space[0x2Cu] == 0xAAu && pci_device_space[0x3Cu] == 0xCCu &&
                          pci_device_space[0x40u] == 0xDDu,
                      "the command register and the expansion ROM keep their bytes, the subsystem vendor ID, the "
                      "interrupt line and the byte past the header take theirs");
    kernel_test_check(test, pci_bridge_space[0x2Cu] == 0x00u && pci_bridge_space[0x40u] == 0xEFu,
                      "the bridge's header keeps its byte at 0x2C, and the byte past it is written");
    kernel_test_check(test, pci_load.regions.pci_protected_writes == 3u && pci_load.evaluation.failures == 0u,
                      "three writes are dropped and counted, none fails");
    kernel_test_check(test, pci_integer("ADRC") == 1u, "the _ADR the first write's attach runs stores once");
    pci_close(test);
}

/**
 * @brief An access past offset 0xFF, or in a segment other than 0, needs ECAM (#58): it fails with
 *        address_unreachable and is counted, without reaching the host; a root bridge in another
 *        segment evaluates no _ADR below it.
 */
KERNEL_TEST(an_access_past_offset_0xff_or_in_another_segment_fails_as_address_unreachable)
{
    static const MadeUpFieldUnit_t units[] = {
        {"XLOW", 8u  },
        {NULL,   120u},
        {"XTND", 8u  },
    };
    static const MadeUpFieldUnit_t other_units[] = {
        {"OVID", 16u},
    };

    pci_begin();
    pci_device_space[0xF0u] = 0x5Au;
    made_up_aml_name_integer(&pci_aml, "V1__", 0u);
    made_up_aml_name_integer(&pci_aml, "V2__", 0u);
    made_up_aml_name_integer(&pci_aml, "V3__", 0u);
    made_up_aml_name_integer(&pci_aml, "ADR1", 0u);
    made_up_aml_region(&pci_inner, "XCFG", PCI_CONFIG, 0xF0u, 0x20u);
    made_up_aml_field(&pci_inner, "XCFG", PCI_BYTE_ACCESS, units, 3u);
    made_up_aml_store_name(&pci_inner, "XLOW", "V1__");
    made_up_aml_store_name(&pci_inner, "XTND", "V2__");
    pci_write_tree(&pci_extra, &pci_inner);
    made_up_aml_begin(&pci_device_body);
    pci_method_returning(&pci_device_body, "_ADR", 0u, "ADR1");
    pci_region(&pci_device_body, "OCFG", other_units, 1u);
    made_up_aml_store_name(&pci_device_body, "OVID", "V3__");
    made_up_aml_begin(&pci_root_body);
    made_up_aml_name_integer(&pci_root_body, "_HID", PCI_HOST_BRIDGE);
    made_up_aml_name_integer(&pci_root_body, "_SEG", 1u);
    made_up_aml_device(&pci_root_body, "OTH_", &pci_device_body);
    made_up_aml_device(&pci_aml, "PCI1", &pci_root_body);
    if (!pci_run(test))
        return;
    kernel_test_check(test, pci_integer("V1__") == 0x5Au && pci_integer("V2__") == 0u && pci_integer("V3__") == 0u,
                      "the byte at 0xF0 reads, the one at 0x100 and the one in segment 1 do not");
    kernel_test_check(test,
                      pci_load.evaluation.statuses[PCI_STATUS(ADDRESS_UNREACHABLE)] == 2u &&
                          pci_load.regions.pci_ecam_accesses == 2u,
                      "both fail with address_unreachable, counted as accesses that need ECAM");
    kernel_test_check(test, pci_host.access_count == 4u && pci_integer("ADR1") == 0u,
                      "neither reaches the host, and no _ADR below the root of segment 1 is evaluated");
    pci_close(test);
}

/**
 * @brief A function behind a bridge with no secondary bus, or behind an absent bridge, cannot be
 *        reached: its reads give all ones and its writes are dropped, counted, and the host is never
 *        asked for it.
 */
KERNEL_TEST(a_function_behind_an_unconfigured_or_absent_bridge_reads_all_ones)
{
    static const MadeUpFieldUnit_t units[] = {
        {"VID_", 16u},
    };
    static const MadeUpFieldUnit_t lost_units[] = {
        {"LVID", 16u},
    };

    pci_begin();
    pci_bridge_space[0x19u] = 0x00u;
    made_up_aml_name_integer(&pci_aml, "V1__", 0u);
    made_up_aml_name_integer(&pci_aml, "V2__", 0u);
    pci_region(&pci_inner, "PCFG", units, 1u);
    made_up_aml_store_name(&pci_inner, "VID_", "V1__");
    made_up_aml_store_integer(&pci_inner, 0x1234u, "VID_");
    pci_write_tree(&pci_extra, &pci_inner);
    made_up_aml_begin(&pci_device_body);
    made_up_aml_name_integer(&pci_device_body, "_ADR", 0u);
    pci_region(&pci_device_body, "LCFG", lost_units, 1u);
    made_up_aml_store_name(&pci_device_body, "LVID", "V2__");
    made_up_aml_begin(&pci_bridge_body);
    made_up_aml_name_integer(&pci_bridge_body, "_ADR", 0x00010000u);
    made_up_aml_device(&pci_bridge_body, "LST_", &pci_device_body);
    made_up_aml_begin(&pci_root_body);
    made_up_aml_name_integer(&pci_root_body, "_HID", PCI_HOST_BRIDGE);
    made_up_aml_device(&pci_root_body, "ABS_", &pci_bridge_body);
    made_up_aml_device(&pci_aml, "PCI1", &pci_root_body);
    if (!pci_run(test))
        return;
    kernel_test_check(test, pci_integer("V1__") == 0xFFFFu && pci_integer("V2__") == 0xFFFFu,
                      "the function behind the unconfigured bridge and the one behind the absent bridge read all ones");
    kernel_test_check(test, pci_load.regions.pci_lost_accesses == 6u && pci_load.evaluation.failures == 0u,
                      "their two reads and the write, each two byte accesses, are counted, and none fails");
    kernel_test_check(
        test,
        pci_host.access_count == 3u &&
            pci_access(0u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_BRIDGE, 0x0Eu, 0x81u) &&
            pci_access(1u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_BRIDGE, 0x19u, 0x00u) &&
            pci_access(2u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_FUNCTION(0u, 0u, 1u, 0u), 0x0Eu,
                       0xFFu),
        "the host sees only the probes of the two bridges");
    pci_close(test);
}

/**
 * @brief An attach whose own _BBN reads a field of the region it attaches does not start itself
 *        again: the inner read fails, which makes _BBN an absent object (§3.6); the root has no _ADR,
 *        so the region reaches no function and reads all ones.
 */
KERNEL_TEST(an_attach_that_needs_its_own_region_takes_its_object_as_absent)
{
    static const MadeUpFieldUnit_t units[] = {
        {NULL,   200u},
        {"BUSN", 8u  },
    };
    static const uint8_t return_bus[] = {0xA4u, 'B', 'U', 'S', 'N'};

    pci_begin();
    made_up_aml_name_integer(&pci_aml, "V1__", 7u);
    made_up_aml_begin(&pci_root_body);
    made_up_aml_name_integer(&pci_root_body, "_HID", PCI_HOST_BRIDGE);
    pci_region(&pci_root_body, "HCFG", units, 2u);
    made_up_aml_begin(&pci_method_body);
    made_up_aml_raw(&pci_method_body, return_bus, sizeof(return_bus));
    made_up_aml_method(&pci_root_body, "_BBN", 0x00u, &pci_method_body);
    made_up_aml_store_name(&pci_root_body, "BUSN", "V1__");
    made_up_aml_device(&pci_aml, "PCI0", &pci_root_body);
    if (!pci_run(test))
        return;
    kernel_test_check(test, pci_load.evaluation.failures == 0u && pci_integer("V1__") == 0xFFu,
                      "the outer read succeeds, and reads all ones");
    kernel_test_check(test,
                      pci_load.regions.internal_evaluations == 1u && pci_load.regions.internal_failures == 1u &&
                          pci_load.regions.pci_attachments == 1u && pci_load.regions.pci_lost_accesses == 1u &&
                          pci_host.access_count == 0u,
                      "_BBN runs once and fails, the region is attached to no function, and the host is never asked");
    pci_close(test);
}

/**
 * @brief A method of the attach that fails, a _BBN dividing by zero, is an absent object, as ACPICA
 *        and uACPI take it: the bus is 0, the access succeeds, and the attach is done once.
 */
KERNEL_TEST(a_failing_attach_method_is_an_absent_object_and_runs_once)
{
    static const MadeUpFieldUnit_t units[] = {
        {"VID_", 16u},
    };
    static const uint8_t divide_by_zero[] = {0xA4u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u};

    pci_begin();
    pci_other_space[0x00u] = 0x86u;
    pci_other_space[0x01u] = 0x80u;
    advanced_configuration_and_power_interface_simulated_host_add_function(&pci_host, 0u, 0u, 3u, 1u, pci_other_space);
    made_up_aml_name_integer(&pci_aml, "V1__", 0u);
    made_up_aml_name_integer(&pci_aml, "V2__", 0u);
    made_up_aml_begin(&pci_device_body);
    made_up_aml_name_integer(&pci_device_body, "_ADR", 0x00030001u);
    pci_region(&pci_device_body, "PCFG", units, 1u);
    made_up_aml_store_name(&pci_device_body, "VID_", "V1__");
    made_up_aml_store_name(&pci_device_body, "VID_", "V2__");
    made_up_aml_begin(&pci_root_body);
    made_up_aml_name_integer(&pci_root_body, "_HID", PCI_HOST_BRIDGE);
    made_up_aml_begin(&pci_method_body);
    made_up_aml_raw(&pci_method_body, divide_by_zero, sizeof(divide_by_zero));
    made_up_aml_method(&pci_root_body, "_BBN", 0x00u, &pci_method_body);
    made_up_aml_device(&pci_root_body, "DEV_", &pci_device_body);
    made_up_aml_device(&pci_aml, "PCI0", &pci_root_body);
    if (!pci_run(test))
        return;
    kernel_test_check(
        test, pci_load.evaluation.failures == 0u && pci_integer("V1__") == 0x8086u && pci_integer("V2__") == 0x8086u,
        "both reads succeed, from device 3 function 1 on bus 0");
    kernel_test_check(test,
                      pci_load.regions.internal_evaluations == 1u && pci_load.regions.internal_failures == 1u &&
                          pci_load.regions.pci_attachments == 1u,
                      "_BBN runs and fails once, for one attach");
    pci_close(test);
}

/**
 * @brief A limit reached inside an attach method, a _BBN that calls itself until the frames run out,
 *        still fails the access: only a failure that is not a limit makes the object absent.
 */
KERNEL_TEST(a_limit_inside_an_attach_method_still_fails_the_access)
{
    static const MadeUpFieldUnit_t units[] = {
        {"VID_", 16u},
    };
    static const uint8_t call_itself[] = {0xA4u, '_', 'B', 'B', 'N'};

    pci_begin();
    made_up_aml_name_integer(&pci_aml, "V1__", 7u);
    made_up_aml_begin(&pci_device_body);
    made_up_aml_name_integer(&pci_device_body, "_ADR", 0x00030001u);
    pci_region(&pci_device_body, "PCFG", units, 1u);
    made_up_aml_store_name(&pci_device_body, "VID_", "V1__");
    made_up_aml_begin(&pci_root_body);
    made_up_aml_name_integer(&pci_root_body, "_HID", PCI_HOST_BRIDGE);
    made_up_aml_begin(&pci_method_body);
    made_up_aml_raw(&pci_method_body, call_itself, sizeof(call_itself));
    made_up_aml_method(&pci_root_body, "_BBN", 0x00u, &pci_method_body);
    made_up_aml_device(&pci_root_body, "DEV_", &pci_device_body);
    made_up_aml_device(&pci_aml, "PCI0", &pci_root_body);
    if (!pci_run(test))
        return;
    kernel_test_check(test,
                      pci_load.evaluation.statuses[PCI_STATUS(LIMIT_FRAMES)] == 1u && pci_integer("V1__") == 7u &&
                          pci_load.regions.internal_failures == 0u && pci_load.regions.pci_attachments == 0u,
                      "the read fails as limit_frames, and nothing is attached");
    pci_close(test);
}

/**
 * @brief A host-initiated read of a field unit by its path attaches the region first, its methods in
 *        internal frames above a host frame; the next read needs none.
 */
KERNEL_TEST(a_host_read_of_a_field_unit_runs_the_attach_s_methods_first)
{
    static const MadeUpFieldUnit_t units[] = {
        {"VID_", 16u},
    };
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t first;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t second;

    pci_begin();
    pci_region(&pci_inner, "PCFG", units, 1u);
    pci_write_tree(&pci_extra, &pci_inner);
    if (!pci_run(test))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t first_status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&pci_load, "\\PCI0.BR1_.DEV_.VID_", NULL,
                                                                             0u, &first);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t second_status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&pci_load, "\\PCI0.BR1_.DEV_.VID_", NULL,
                                                                             0u, &second);

    kernel_test_check(test,
                      first_status == PCI_STATUS(OK) && second_status == PCI_STATUS(OK) &&
                          first.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER &&
                          first.payload == 0x8086u && second.payload == 0x8086u,
                      "both reads give the vendor");
    kernel_test_check(test, pci_integer("BBNC") == 1u && pci_load.regions.internal_evaluations == 2u,
                      "_BBN and the _ADR run once, for the first read");
    pci_close(test);
}

/**
 * @brief The root bridge is the closest ancestor whose _HID or _CID names PNP0A03 or PNP0A08, as an
 *        EisaId or a String, in a Package or alone: a device above it that is a root bridge too is not
 *        used, and a region of the root bridge itself belongs to the function its own _ADR names.
 */
KERNEL_TEST(the_root_bridge_is_the_closest_device_whose_hid_or_cid_names_one)
{
    static const uint8_t hardware_identifier[] = {0x08u, '_', 'H', 'I', 'D', 0x0Du, 'A',  'C',
                                                  'P',   'I', '0', '0', '1', '6',   0x00u};
    static const uint8_t compatible_identifiers[] = {0x08u, '_', 'C', 'I', 'D', 0x12u, 0x0Bu, 0x01u, 0x0Du,
                                                     'P',   'N', 'P', '0', 'A', '0',   '8',   0x00u};
    static const MadeUpFieldUnit_t units[] = {
        {"VID_", 16u},
    };
    static const MadeUpFieldUnit_t root_units[] = {
        {"RVID", 16u},
    };

    pci_begin();
    pci_other_space[0x00u] = 0x22u;
    pci_other_space[0x01u] = 0x10u;
    advanced_configuration_and_power_interface_simulated_host_add_function(&pci_host, 0u, 4u, 2u, 0u, pci_other_space);
    pci_device_space[0x00u] = 0x34u;
    pci_device_space[0x01u] = 0x12u;
    advanced_configuration_and_power_interface_simulated_host_add_function(&pci_host, 0u, 4u, 0u, 0u, pci_device_space);
    made_up_aml_name_integer(&pci_aml, "V1__", 0u);
    made_up_aml_name_integer(&pci_aml, "V2__", 0u);
    made_up_aml_begin(&pci_device_body);
    made_up_aml_name_integer(&pci_device_body, "_ADR", 0x00020000u);
    pci_region(&pci_device_body, "PCFG", units, 1u);
    made_up_aml_store_name(&pci_device_body, "VID_", "V1__");
    made_up_aml_begin(&pci_bridge_body);
    made_up_aml_raw(&pci_bridge_body, hardware_identifier, sizeof(hardware_identifier));
    made_up_aml_raw(&pci_bridge_body, compatible_identifiers, sizeof(compatible_identifiers));
    made_up_aml_name_integer(&pci_bridge_body, "_BBN", 4u);
    made_up_aml_name_integer(&pci_bridge_body, "_ADR", 0u);
    pci_region(&pci_bridge_body, "RCFG", root_units, 1u);
    made_up_aml_store_name(&pci_bridge_body, "RVID", "V2__");
    made_up_aml_device(&pci_bridge_body, "DEV_", &pci_device_body);
    made_up_aml_begin(&pci_root_body);
    made_up_aml_name_integer(&pci_root_body, "_HID", PCI_HOST_BRIDGE);
    made_up_aml_name_integer(&pci_root_body, "_BBN", 9u);
    made_up_aml_device(&pci_root_body, "PCI0", &pci_bridge_body);
    made_up_aml_device(&pci_aml, "OUT_", &pci_root_body);
    if (!pci_run(test))
        return;
    kernel_test_check(test, pci_integer("V1__") == 0x1022u && pci_load.evaluation.failures == 0u,
                      "VID_ reads the function at device 2 on bus 4, the _BBN of the root its _CID names");
    kernel_test_check(test, pci_integer("V2__") == 0x1234u,
                      "a region of the root bridge itself reads the function its own _ADR names");
    pci_close(test);
}

/**
 * @brief A region with no root bridge above it belongs to segment 0, bus 0, and its device's _ADR.
 */
KERNEL_TEST(a_region_with_no_root_bridge_above_it_uses_bus_0_and_its_device_s_address)
{
    static const MadeUpFieldUnit_t units[] = {
        {"VID_", 16u},
    };

    pci_begin();
    pci_other_space[0x00u] = 0x02u;
    pci_other_space[0x01u] = 0x10u;
    advanced_configuration_and_power_interface_simulated_host_add_function(&pci_host, 0u, 0u, 0x1Fu, 0u,
                                                                           pci_other_space);
    made_up_aml_name_integer(&pci_aml, "V1__", 0u);
    made_up_aml_begin(&pci_device_body);
    made_up_aml_name_integer(&pci_device_body, "_ADR", 0x001F0000u);
    pci_region(&pci_device_body, "PCFG", units, 1u);
    made_up_aml_store_name(&pci_device_body, "VID_", "V1__");
    made_up_aml_device(&pci_aml, "LPC_", &pci_device_body);
    if (!pci_run(test))
        return;
    kernel_test_check(test, pci_integer("V1__") == 0x1002u && pci_load.regions.pci_attachments == 1u,
                      "VID_ reads device 0x1F on bus 0");
    pci_close(test);
}

/**
 * @brief An IndexField whose registers lie in a PCI_Config region attaches it before its index is
 *        written: the index register takes the unit's byte offset, then the data register is read.
 */
KERNEL_TEST(an_index_field_attaches_the_region_of_its_registers_first)
{
    static const MadeUpFieldUnit_t registers[] = {
        {NULL,   576u},
        {"IDX_", 8u  },
        {"DAT_", 8u  },
    };
    static const MadeUpFieldUnit_t units[] = {
        {NULL,   40u},
        {"IFLD", 8u },
    };

    pci_begin();
    pci_device_space[0x49u] = 0x3Cu;
    made_up_aml_name_integer(&pci_aml, "V1__", 0u);
    pci_region(&pci_inner, "PCFG", registers, 3u);
    made_up_aml_index_field(&pci_inner, "IDX_", "DAT_", PCI_BYTE_ACCESS, units, 2u);
    made_up_aml_store_name(&pci_inner, "IFLD", "V1__");
    pci_write_tree(&pci_extra, &pci_inner);
    if (!pci_run(test))
        return;
    kernel_test_check(test, pci_integer("V1__") == 0x3Cu && pci_integer("BBNC") == 1u,
                      "IFLD reads the data register once the region is attached");
    kernel_test_check(
        test,
        pci_host.access_count == 5u &&
            pci_access(3u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_WRITE), 1u, PCI_DEVICE, 0x48u, 0x05u) &&
            pci_access(4u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_DEVICE, 0x49u, 0x3Cu),
        "after the attach, the index register takes 5 and the data register is read");
    pci_close(test);
}

/**
 * @brief A datum at an offset its width does not divide is made byte by byte, counted, so that no
 *        access of the host is unaligned.
 */
KERNEL_TEST(a_datum_its_width_does_not_divide_is_read_byte_by_byte)
{
    static const MadeUpFieldUnit_t units[] = {
        {"UWRD", 16u},
    };

    pci_begin();
    pci_device_space[0x41u] = 0x34u;
    pci_device_space[0x42u] = 0x12u;
    made_up_aml_name_integer(&pci_aml, "V1__", 0u);
    made_up_aml_region(&pci_inner, "UCFG", PCI_CONFIG, 0x41u, 2u);
    made_up_aml_field(&pci_inner, "UCFG", PCI_WORD_ACCESS, units, 1u);
    made_up_aml_store_name(&pci_inner, "UWRD", "V1__");
    pci_write_tree(&pci_extra, &pci_inner);
    if (!pci_run(test))
        return;
    kernel_test_check(test, pci_integer("V1__") == 0x1234u && pci_load.regions.unaligned_accesses == 1u,
                      "the word at 0x41 reads 0x1234, counted as unaligned");
    kernel_test_check(
        test,
        pci_host.access_count == 5u &&
            pci_access(3u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_DEVICE, 0x41u, 0x34u) &&
            pci_access(4u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_READ), 1u, PCI_DEVICE, 0x42u, 0x12u),
        "it is two byte reads");
    pci_close(test);
}

/**
 * @brief A function of uACPI's test runner, on segment 0 here: its bus, device and function, its
 *        header type, and the secondary bus of a bridge.
 */
typedef struct PciFunction {
    uint8_t bus;       /**< Its bus. */
    uint8_t device;    /**< Its device. */
    uint8_t function;  /**< Its function. */
    uint8_t header;    /**< Its header type: 0, 1 for a PCI-to-PCI bridge, 2 for a CardBus bridge. */
    uint8_t secondary; /**< A bridge's secondary bus, 0 when it has none. */
} PciFunction_t;

static uint8_t pci_spaces[11][ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_CONFIGURATION_BYTES];
static MadeUpAml_t pci_level[4];

/**
 * @brief The dword a function's configuration space starts with here: 0xA0, then its bus, device and
 *        function, each a byte.
 */
static uint32_t pci_identity(uint8_t bus, uint8_t device, uint8_t function)
{
    return 0xA0000000u | ((uint32_t) bus << 16u) | ((uint32_t) device << 8u) | function;
}

/**
 * @brief Gives the simulated host the functions of uACPI's case pci-devices-below-bridges, on segment
 *        0, each starting with its identity.
 */
static void pci_add_bridges_and_endpoints(void)
{
    static const PciFunction_t functions[] = {
        {0x10u, 0x00u, 0u, 0x00u, 0x00u},
        {0x10u, 0x02u, 0u, 0x00u, 0x00u},
        {0x10u, 0x03u, 0u, 0x00u, 0x00u},
        {0x10u, 0x1Cu, 0u, 0x01u, 0x15u},
        {0x10u, 0x1Du, 0u, 0x01u, 0x00u},
        {0x10u, 0x1Fu, 0u, 0x02u, 0x19u},
        {0x15u, 0x00u, 0u, 0x00u, 0x00u},
        {0x15u, 0x01u, 0u, 0x01u, 0x16u},
        {0x16u, 0x02u, 3u, 0x00u, 0x00u},
        {0x19u, 0x00u, 0u, 0x00u, 0x00u},
    };

    for (uint32_t index = 0u; index < sizeof(functions) / sizeof(functions[0]); ++index)
    {
        const PciFunction_t *entry = &functions[index];
        const uint32_t identity = pci_identity(entry->bus, entry->device, entry->function);

        memset(pci_spaces[index], 0, sizeof(pci_spaces[index]));
        memcpy(pci_spaces[index], &identity, sizeof(identity));
        pci_spaces[index][0x0Eu] = entry->header;
        pci_spaces[index][0x19u] = entry->secondary;
        advanced_configuration_and_power_interface_simulated_host_add_function(&pci_host, 0u, entry->bus, entry->device,
                                                                               entry->function, pci_spaces[index]);
    }
}

/**
 * @brief Appends OperationRegion (CFG_, PCI_Config, 0, 4), Field (CFG_, DWordAcc) { ID__, 32 }, and
 *        Store (ID__, @p target), @p target a Name at the root.
 */
static void pci_identity_region(MadeUpAml_t *aml, const char *target)
{
    static const MadeUpFieldUnit_t identity[] = {
        {"ID__", 32u},
    };

    made_up_aml_region(aml, "CFG_", PCI_CONFIG, 0u, 4u);
    made_up_aml_field(aml, "CFG_", 0x03u, identity, 1u);
    made_up_aml_store_name(aml, "ID__", target);
}

/**
 * @brief Appends Device (@p name) { Name (_ADR, @p address) } and its identity region stored to
 *        @p target, the _ADR left out when @p address is ~0, the region when @p target is NULL; the
 *        device's other terms are those @p children holds, or none.
 */
static void pci_level_device(MadeUpAml_t *aml, uint32_t depth, const char *name, uint64_t address, const char *target,
                             const MadeUpAml_t *children)
{
    MadeUpAml_t *body = &pci_level[depth];

    made_up_aml_begin(body);
    if (address != ~0ull)
        made_up_aml_name_integer(body, "_ADR", address);
    if (target != NULL)
        pci_identity_region(body, target);
    if (children != NULL)
        made_up_aml_raw(body, children->bytes, children->length);
    made_up_aml_device(aml, name, body);
}

/**
 * @brief Appends a root bridge @p name: _HID "PNP0A08", _BBN 0x10, an _ADR of 0 when @p with_address
 *        holds, its own identity region stored to @p target, and @p children.
 */
static void pci_level_root(const char *name, bool with_address, const char *target, const MadeUpAml_t *children)
{
    static const uint8_t express_root[] = {0x08u, '_', 'H', 'I', 'D', 0x0Du, 'P', 'N', 'P', '0', 'A', '0', '8', 0x00u};
    MadeUpAml_t *body = &pci_level[0];

    made_up_aml_begin(body);
    made_up_aml_raw(body, express_root, sizeof(express_root));
    made_up_aml_name_integer(body, "_BBN", 0x10u);
    if (with_address)
        made_up_aml_name_integer(body, "_ADR", 0u);
    pci_identity_region(body, target);
    made_up_aml_raw(body, children->bytes, children->length);
    made_up_aml_device(&pci_aml, name, body);
}

/**
 * @brief The walk down from a root bridge to a region follows uACPI's case pci-devices-below-bridges
 *        on segment 0: a region belongs to the closest PCI device at or above it, reached through each
 *        Device with an _ADR, a PCI-to-PCI or CardBus bridge moving the bus to its secondary one; a
 *        device without an _ADR, or anything that is not a Device, ends the walk; whatever lies below
 *        a function that is not a bridge belongs to it, whatever its _ADR; an unconfigured or absent
 *        bridge, an _ADR past a bridge that names no device and function, or a root with no _ADR and
 *        no device below it, reaches nothing.
 */
KERNEL_TEST(the_walk_down_follows_uacpi_s_devices_below_bridges)
{
    static const char *const names[] = {"V00_", "V01_", "V02_", "V03_", "V04_", "V05_", "V06_",
                                        "V07_", "V08_", "V09_", "V10_", "V11_", "V12_", "V13_",
                                        "V14_", "V15_", "V16_", "V17_", "V18_", "V19_", "V20_"};
    static const uint8_t method_with_one_argument[] = {0xA4u, 0x0Cu, 0x00u, 0x00u, 0x03u, 0x00u};
    static const uint32_t lost = 0xFFFFFFFFu;
    const uint32_t expected[] = {
        lost,
        lost,
        pci_identity(0x10u, 0x1Cu, 0u),
        pci_identity(0x15u, 0x00u, 0u),
        pci_identity(0x10u, 0x1Cu, 0u),
        pci_identity(0x10u, 0x1Cu, 0u),
        pci_identity(0x16u, 0x02u, 3u),
        pci_identity(0x10u, 0x1Du, 0u),
        lost,
        lost,
        pci_identity(0x19u, 0x00u, 0u),
        pci_identity(0x10u, 0x02u, 0u),
        pci_identity(0x10u, 0x02u, 0u),
        pci_identity(0x10u, 0x02u, 0u),
        pci_identity(0x10u, 0x02u, 0u),
        pci_identity(0x10u, 0x00u, 0u),
        pci_identity(0x10u, 0x00u, 0u),
        pci_identity(0x10u, 0x00u, 0u),
        pci_identity(0x10u, 0x00u, 0u),
        lost,
        lost,
    };
    uint32_t agree = 0u;

    pci_begin();
    pci_add_bridges_and_endpoints();
    for (uint32_t index = 0u; index < 21u; ++index)
        made_up_aml_name_integer(&pci_aml, names[index], 0xDEADu);

    made_up_aml_begin(&pci_root_body);
    pci_level_device(&pci_root_body, 1u, "NADR", ~0ull, "V01_", NULL);
    made_up_aml_begin(&pci_bridge_body);
    pci_level_device(&pci_bridge_body, 2u, "PXSX", 0u, "V03_", NULL);
    made_up_aml_begin(&pci_device_body);
    pci_level_device(&pci_device_body, 3u, "PXSX", 0u, "V05_", NULL);
    pci_level_device(&pci_bridge_body, 2u, "CONT", ~0ull, "V04_", &pci_device_body);
    made_up_aml_begin(&pci_device_body);
    pci_level_device(&pci_device_body, 3u, "DEV0", 0x00020003u, "V06_", NULL);
    pci_level_device(&pci_bridge_body, 2u, "BRG2", 0x00010000u, NULL, &pci_device_body);
    pci_level_device(&pci_bridge_body, 2u, "BADF", 0x00000008u, "V20_", NULL);
    pci_level_device(&pci_root_body, 1u, "RP01", 0x001C0000u, "V02_", &pci_bridge_body);
    made_up_aml_begin(&pci_bridge_body);
    pci_level_device(&pci_bridge_body, 2u, "PXSX", 0u, "V08_", NULL);
    pci_level_device(&pci_root_body, 1u, "RP02", 0x001D0000u, "V07_", &pci_bridge_body);
    made_up_aml_begin(&pci_bridge_body);
    pci_level_device(&pci_bridge_body, 2u, "PXSX", 0u, "V09_", NULL);
    pci_level_device(&pci_root_body, 1u, "RP03", 0x001E0000u, NULL, &pci_bridge_body);
    made_up_aml_begin(&pci_bridge_body);
    pci_level_device(&pci_bridge_body, 2u, "CARD", 0u, "V10_", NULL);
    pci_level_device(&pci_root_body, 1u, "CBUS", 0x001F0000u, NULL, &pci_bridge_body);
    made_up_aml_begin(&pci_bridge_body);
    pci_level_device(&pci_bridge_body, 2u, "CHLD", 0x00030000u, "V11_", NULL);
    pci_level_device(&pci_bridge_body, 2u, "PRT1", 1u, "V12_", NULL);
    pci_level_device(&pci_bridge_body, 2u, "PRT9", 9u, "V13_", NULL);
    pci_level_device(&pci_bridge_body, 2u, "SATP", 0x0000FFFFu, "V14_", NULL);
    pci_level_device(&pci_root_body, 1u, "EP00", 0x00020000u, NULL, &pci_bridge_body);
    pci_level_root("PCI0", false, "V00_", &pci_root_body);

    made_up_aml_begin(&pci_root_body);
    pci_level_device(&pci_root_body, 1u, "NADR", ~0ull, "V16_", NULL);
    made_up_aml_begin(&pci_bridge_body);
    pci_level_device(&pci_bridge_body, 2u, "DEVX", 0u, "V17_", NULL);
    made_up_aml_begin(&pci_device_body);
    made_up_aml_name_integer(&pci_device_body, "_ADR", 0x00030000u);
    made_up_aml_raw(&pci_device_body, pci_bridge_body.bytes, pci_bridge_body.length);
    made_up_aml_begin(&pci_inner);
    made_up_aml_device(&pci_inner, "THZ0", &pci_device_body);
    pci_inner.bytes[1] = 0x85u;
    made_up_aml_raw(&pci_root_body, pci_inner.bytes, pci_inner.length);
    made_up_aml_begin(&pci_device_body);
    made_up_aml_begin(&pci_method_body);
    made_up_aml_raw(&pci_method_body, method_with_one_argument, sizeof(method_with_one_argument));
    made_up_aml_method(&pci_device_body, "_ADR", 0x01u, &pci_method_body);
    pci_identity_region(&pci_device_body, "V18_");
    made_up_aml_device(&pci_root_body, "MARG", &pci_device_body);
    pci_level_root("PCI1", true, "V15_", &pci_root_body);

    pci_level_device(&pci_aml, 1u, "NPCI", ~0ull, "V19_", NULL);
    if (!pci_run(test))
        return;
    for (uint32_t index = 0u; index < 21u; ++index)
    {
        const bool holds = pci_integer(names[index]) == expected[index];

        agree += holds ? 1u : 0u;
        if (!holds)
            kernel_test_check(test, false, names[index]);
    }
    kernel_test_check(test, agree == 21u && pci_load.evaluation.failures == 0u,
                      "each of the 21 regions reads the function uACPI's walk gives it, or all ones");
    pci_close(test);
}

/**
 * @brief Appends Device (@p name) { Name (_ADR, @p address) } with a region of the first 0x48 bytes,
 *        whose ByteAcc units of uACPI's case pci-config-write-filter are written 0x5A, then its
 *        DWordAcc units 0xA5A5A5A5, by table-level code.
 */
static void pci_filtered_device(const char *name, uint64_t address)
{
    static const MadeUpFieldUnit_t bytes[] = {
        {NULL,   32u },
        {"B04_", 8u  },
        {NULL,   56u },
        {"B0C_", 8u  },
        {NULL,   240u},
        {"B2B_", 8u  },
        {"B2C_", 8u  },
        {NULL,   16u },
        {"B2F_", 8u  },
        {"B30_", 8u  },
        {NULL,   80u },
        {"B3B_", 8u  },
        {"B3C_", 8u  },
        {NULL,   16u },
        {"B3F_", 8u  },
        {NULL,   32u },
        {"B44_", 8u  },
    };
    static const MadeUpFieldUnit_t dwords[] = {
        {NULL,   128u},
        {"D10_", 32u },
        {NULL,   160u},
        {"D28_", 32u },
        {"D2C_", 32u },
        {NULL,   64u },
        {"D38_", 32u },
        {"D3C_", 32u },
        {NULL,   32u },
        {"D44_", 32u },
    };
    static const char *const byte_names[] = {"B04_", "B0C_", "B2B_", "B2C_", "B2F_",
                                             "B30_", "B3B_", "B3C_", "B3F_", "B44_"};
    static const char *const dword_names[] = {"D10_", "D28_", "D2C_", "D38_", "D3C_", "D44_"};

    made_up_aml_begin(&pci_device_body);
    made_up_aml_name_integer(&pci_device_body, "_ADR", address);
    made_up_aml_region(&pci_device_body, "CFG_", PCI_CONFIG, 0u, 0x48u);
    made_up_aml_field(&pci_device_body, "CFG_", PCI_BYTE_ACCESS, bytes, sizeof(bytes) / sizeof(bytes[0]));
    made_up_aml_field(&pci_device_body, "CFG_", 0x03u, dwords, sizeof(dwords) / sizeof(dwords[0]));
    for (uint32_t index = 0u; index < sizeof(byte_names) / sizeof(byte_names[0]); ++index)
        made_up_aml_store_integer(&pci_device_body, 0x5Au, byte_names[index]);
    for (uint32_t index = 0u; index < sizeof(dword_names) / sizeof(dword_names[0]); ++index)
        made_up_aml_store_integer(&pci_device_body, 0xA5A5A5A5u, dword_names[index]);
    made_up_aml_device(&pci_root_body, name, &pci_device_body);
}

/**
 * @brief The header write filter holds at each edge, for bytes, dwords and an unaligned word: on a
 *        type-0 header, 0x2C to 0x2F, 0x3C to 0x3F and what follows the header are written, and the
 *        rest of the header kept; on a PCI-to-PCI or CardBus bridge, the whole header is kept; a word
 *        at 0x2B is two bytes, the first dropped and the second written.
 */
KERNEL_TEST(the_header_write_filter_holds_at_each_edge_and_width)
{
    static const uint8_t express_root[] = {0x08u, '_', 'H', 'I', 'D', 0x0Du, 'P', 'N', 'P', '0', 'A', '0', '8', 0x00u};
    static const MadeUpFieldUnit_t word[] = {
        {"UWRD", 16u},
    };
    static const uint8_t endpoint_kept[] = {0x04u, 0x0Cu, 0x0Eu, 0x10u, 0x28u, 0x2Bu, 0x30u, 0x38u, 0x3Bu};
    static const uint8_t endpoint_written[] = {0x2Cu, 0x2Fu, 0x3Cu, 0x3Fu, 0x44u, 0x47u};
    static const uint8_t bridge_kept[] = {0x04u, 0x0Cu, 0x10u, 0x28u, 0x2Bu, 0x2Cu,
                                          0x2Fu, 0x30u, 0x38u, 0x3Bu, 0x3Cu, 0x3Fu};
    bool holds = true;

    pci_begin();
    pci_add_bridges_and_endpoints();
    made_up_aml_begin(&pci_root_body);
    made_up_aml_raw(&pci_root_body, express_root, sizeof(express_root));
    made_up_aml_name_integer(&pci_root_body, "_BBN", 0x10u);
    pci_filtered_device("EP00", 0x00020000u);
    pci_filtered_device("RP01", 0x001C0000u);
    pci_filtered_device("CBUS", 0x001F0000u);
    made_up_aml_begin(&pci_device_body);
    made_up_aml_name_integer(&pci_device_body, "_ADR", 0x00030000u);
    made_up_aml_region(&pci_device_body, "UCFG", PCI_CONFIG, 0x2Bu, 2u);
    made_up_aml_field(&pci_device_body, "UCFG", PCI_WORD_ACCESS, word, 1u);
    made_up_aml_store_integer(&pci_device_body, 0xBBAAu, "UWRD");
    made_up_aml_device(&pci_root_body, "EP03", &pci_device_body);
    made_up_aml_device(&pci_aml, "PCI0", &pci_root_body);
    if (!pci_run(test))
        return;
    for (uint32_t index = 0u; index < sizeof(endpoint_kept); ++index)
        holds = holds && pci_spaces[1][endpoint_kept[index]] == 0u;
    kernel_test_check(test, holds, "the endpoint keeps the header bytes AML may not write");
    holds = true;
    for (uint32_t index = 0u; index < sizeof(endpoint_written); ++index)
        holds = holds && pci_spaces[1][endpoint_written[index]] == 0xA5u;
    kernel_test_check(test, holds, "the endpoint takes 0x2C to 0x2F, 0x3C to 0x3F and the bytes past the header");
    holds = true;
    for (uint32_t index = 0u; index < sizeof(bridge_kept); ++index)
        holds = holds && pci_spaces[3][bridge_kept[index]] == 0u && pci_spaces[5][bridge_kept[index]] == 0u;
    kernel_test_check(test,
                      holds && pci_spaces[3][0x44u] == 0xA5u && pci_spaces[5][0x44u] == 0xA5u &&
                          pci_spaces[3][0x0Eu] == 0x01u && pci_spaces[5][0x0Eu] == 0x02u,
                      "both bridges keep their whole header and take the bytes past it");
    kernel_test_check(test, pci_spaces[2][0x2Bu] == 0u && pci_spaces[2][0x2Cu] == 0xBBu,
                      "the word at 0x2B is two bytes: 0x2B dropped, 0x2C written");
    kernel_test_check(test, pci_load.evaluation.failures == 0u && pci_load.regions.pci_protected_writes == 37u,
                      "no write fails, and the 37 dropped are counted");
    pci_close(test);
}

/**
 * @brief A datum that crosses offset 0x100 needs ECAM, as one past it does, and fails as
 *        address_unreachable before any byte is written; an 8-byte datum, wider than any
 *        configuration access, fails as access_width_invalid (§4.4).
 */
KERNEL_TEST(a_datum_across_offset_0x100_or_eight_bytes_wide_is_refused)
{
    static const MadeUpFieldUnit_t straddle[] = {
        {"XDW_", 32u},
    };
    static const MadeUpFieldUnit_t quad[] = {
        {"XQW_", 64u},
    };

    pci_begin();
    made_up_aml_name_integer(&pci_aml, "V1__", 7u);
    made_up_aml_region(&pci_inner, "SCFG", PCI_CONFIG, 0xFEu, 4u);
    made_up_aml_field(&pci_inner, "SCFG", 0x03u, straddle, 1u);
    made_up_aml_region(&pci_inner, "QCFG", PCI_CONFIG, 0x48u, 8u);
    made_up_aml_field(&pci_inner, "QCFG", 0x04u, quad, 1u);
    made_up_aml_store_integer(&pci_inner, 0x11223344u, "XDW_");
    made_up_aml_store_name(&pci_inner, "XQW_", "V1__");
    pci_write_tree(&pci_extra, &pci_inner);
    if (!pci_run(test))
        return;
    kernel_test_check(test,
                      pci_load.evaluation.statuses[PCI_STATUS(ADDRESS_UNREACHABLE)] == 1u &&
                          pci_load.evaluation.statuses[PCI_STATUS(ACCESS_WIDTH_INVALID)] == 1u &&
                          pci_load.regions.pci_ecam_accesses == 1u && pci_integer("V1__") == 7u,
                      "the dword at 0xFE fails as address_unreachable, the qword as access_width_invalid");
    kernel_test_check(test,
                      pci_host.access_count == 6u && pci_device_space[0xFEu] == 0u && pci_device_space[0xFFu] == 0u,
                      "the host sees the two attaches alone, and no byte is written");
    pci_close(test);
}

/**
 * @brief A BankField whose bank register lies in a PCI_Config region attaches it before the bank is
 *        selected: the register takes the BankValue, then the unit's own region is read.
 */
KERNEL_TEST(a_bank_field_attaches_the_region_of_its_bank_register_first)
{
    static const uint8_t memory[] = {0x6Bu};
    static const MadeUpFieldUnit_t registers[] = {
        {NULL,   576u},
        {"BNK_", 8u  },
    };
    static const MadeUpFieldUnit_t units[] = {
        {"BF__", 8u},
    };

    pci_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&pci_host, 0x10000u, memory, sizeof(memory));
    made_up_aml_name_integer(&pci_aml, "V1__", 0u);
    pci_region(&pci_inner, "PCFG", registers, 2u);
    made_up_aml_region(&pci_inner, "BMEM", 0x00u, 0x10000u, 1u);
    made_up_aml_bank_field(&pci_inner, "BMEM", "BNK_", 1u, PCI_BYTE_ACCESS, units, 1u);
    made_up_aml_store_name(&pci_inner, "BF__", "V1__");
    pci_write_tree(&pci_extra, &pci_inner);
    if (!pci_run(test))
        return;
    kernel_test_check(test, pci_integer("V1__") == 0x6Bu && pci_integer("BBNC") == 1u,
                      "BF__ reads its byte once the bank register's region is attached");
    kernel_test_check(test,
                      pci_access(3u, PCI_ACCESS(PERIPHERAL_COMPONENT_INTERCONNECT_WRITE), 1u, PCI_DEVICE, 0x48u, 1u),
                      "after the attach, the bank register takes the BankValue 1");
    pci_close(test);
}
