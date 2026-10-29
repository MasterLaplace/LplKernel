#include "acpi.h"

#include "accommon.h"
#include "actables.h"
#include "aecommon.h"
#include "amlresrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ACPICA_FADT_LARGEST 0x10000u /**< More bytes than any FADT holds: a longer input is refused. */

/**
 * @brief Stops the program: @p name, which acpiexec's objects reference and building a local FADT never calls, was
 *        called.
 */
static void acpica_fadt_unreachable(const char *name)
{
    fprintf(stderr, "acpica-fadt: %s was called, which building a local FADT never does\n", name);
    abort();
}

/**
 * @brief Prints one Generic Address Structure of ACPICA's local FADT as `test-acpi --machine` prints the kernel's:
 *        one whose address is 0 as absent, every field 0, since ACPICA tells a register absent by its address alone.
 */
static void acpica_fadt_print_register(const char *name, const ACPI_GENERIC_ADDRESS *block)
{
    UINT64 address;

    memcpy(&address, &block->Address, sizeof(address));
    if (address == 0)
    {
        printf("fadt %s space=0 width=0 offset=0 access=0 address=0x0\n", name);
        return;
    }
    printf("fadt %s space=%u width=%u offset=%u access=%u address=0x%llX\n", name, block->SpaceId, block->BitWidth,
           block->BitOffset, block->AccessWidth, (unsigned long long) address);
}

/**
 * @brief The length ACPICA's GPE initialisation (evgpeinit.c, AcpiEvGpeInitialize) reads for a GPE block: its legacy
 *        length, and none when the length or the address is 0. Its check that drops a GPE1 block overlapping GPE0's
 *        numbers is GPE numbering, not the FADT's, and is not applied.
 */
static UINT8 acpica_fadt_event_block_length(UINT8 length, const ACPI_GENERIC_ADDRESS *block)
{
    UINT64 address;

    memcpy(&address, &block->Address, sizeof(address));
    return (address != 0 && length != 0) ? length : 0;
}

/**
 * @brief Prints the fields of ACPICA's local FADT the kernel's normalised FADT also holds, in its order: the reset
 *        register and its value only with RESET_REG_SUP, as AcpiReset (hwxface.c) reads them, and a GPE block's
 *        length only with its address, as AcpiEvGpeInitialize reads it.
 */
static void acpica_fadt_print(void)
{
    static const ACPI_GENERIC_ADDRESS absent;
    const BOOLEAN reset_supported = (AcpiGbl_FADT.Flags & ACPI_FADT_RESET_REGISTER) != 0;

    printf("fadt present=1 hardware_reduced=%u flags=0x%08X sci=%u smi_command=0x%X enable=0x%02X pstate=0x%02X "
           "cst=0x%02X reset_value=0x%02X gpe0_length=%u gpe1_length=%u gpe1_base=%u\n",
           AcpiGbl_ReducedHardware ? 1u : 0u, AcpiGbl_FADT.Flags, AcpiGbl_FADT.SciInterrupt, AcpiGbl_FADT.SmiCommand,
           AcpiGbl_FADT.AcpiEnable, AcpiGbl_FADT.PstateControl, AcpiGbl_FADT.CstControl,
           reset_supported ? AcpiGbl_FADT.ResetValue : 0u,
           acpica_fadt_event_block_length(AcpiGbl_FADT.Gpe0BlockLength, &AcpiGbl_FADT.XGpe0Block),
           acpica_fadt_event_block_length(AcpiGbl_FADT.Gpe1BlockLength, &AcpiGbl_FADT.XGpe1Block),
           AcpiGbl_FADT.Gpe1Base);
    acpica_fadt_print_register("pm1a_event", &AcpiGbl_FADT.XPm1aEventBlock);
    acpica_fadt_print_register("pm1b_event", &AcpiGbl_FADT.XPm1bEventBlock);
    acpica_fadt_print_register("pm1a_control", &AcpiGbl_FADT.XPm1aControlBlock);
    acpica_fadt_print_register("pm1b_control", &AcpiGbl_FADT.XPm1bControlBlock);
    acpica_fadt_print_register("pm_timer", &AcpiGbl_FADT.XPmTimerBlock);
    acpica_fadt_print_register("gpe0", &AcpiGbl_FADT.XGpe0Block);
    acpica_fadt_print_register("gpe1", &AcpiGbl_FADT.XGpe1Block);
    acpica_fadt_print_register("reset", reset_supported ? &AcpiGbl_FADT.ResetRegister : &absent);
    acpica_fadt_print_register("sleep_control", &AcpiGbl_FADT.SleepControl);
    acpica_fadt_print_register("sleep_status", &AcpiGbl_FADT.SleepStatus);
}

/**
 * @brief Stands in for the root pointer acpiexec's operating system layer would find; never called here.
 *
 * @return Nothing: it stops the program.
 */
ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer(void)
{
    acpica_fadt_unreachable("AcpiOsGetRootPointer");
    return 0;
}

/**
 * @brief Stands in for acpiexec's table override, which its operating system layer calls; never called here.
 *
 * @param existing_table The table firmware gave.
 * @param new_table      Receives the table to use instead.
 */
void AeTableOverride(ACPI_TABLE_HEADER *existing_table, ACPI_TABLE_HEADER **new_table)
{
    (void) existing_table;
    (void) new_table;
    acpica_fadt_unreachable("AeTableOverride");
}

/**
 * @brief Stands in for acpiexec's `-fi` file lookup, which its interpreter calls; never called here.
 *
 * @param pathname   The object's path.
 * @param out_object Receives the object's initial value.
 * @return Nothing: it stops the program.
 */
ACPI_STATUS AeLookupInitFileEntry(char *pathname, ACPI_OPERAND_OBJECT **out_object)
{
    (void) pathname;
    (void) out_object;
    acpica_fadt_unreachable("AeLookupInitFileEntry");
    return AE_ERROR;
}

/**
 * @brief Stands in for iASL's record of GPIO connections, which the disassembler calls; never called here.
 *
 * @param operation   The parse node.
 * @param resource    The descriptor.
 * @param pin_count   Its pins.
 * @param pins        The pin list.
 * @param device_name The device it connects.
 */
void MpSaveGpioInfo(ACPI_PARSE_OBJECT *operation, AML_RESOURCE *resource, UINT32 pin_count, UINT16 *pins,
                    char *device_name)
{
    (void) operation;
    (void) resource;
    (void) pin_count;
    (void) pins;
    (void) device_name;
    acpica_fadt_unreachable("MpSaveGpioInfo");
}

/**
 * @brief Stands in for iASL's record of serial bus connections, which the disassembler calls; never called here.
 *
 * @param operation   The parse node.
 * @param resource    The descriptor.
 * @param device_name The device it connects.
 */
void MpSaveSerialInfo(ACPI_PARSE_OBJECT *operation, AML_RESOURCE *resource, char *device_name)
{
    (void) operation;
    (void) resource;
    (void) device_name;
    acpica_fadt_unreachable("MpSaveSerialInfo");
}

/**
 * @brief Reads one FADT from standard input, has ACPICA build its local FADT from it as AcpiTbParseFadt does, and
 *        prints it; prints `fadt present=0` when the input is empty. ACPICA's option that rewrites the PM register
 *        widths to their defaults is off, so that the widths compared are those firmware declares, which the kernel
 *        keeps (specification §3.9). ACPICA's warnings about the table go to standard error.
 *
 * @return 0 on success, 1 when the input is not a whole table.
 */
int main(void)
{
    static UINT8 table[ACPICA_FADT_LARGEST];
    ACPI_TABLE_HEADER header;
    const size_t length = fread(table, 1, sizeof(table), stdin);

    if (length == 0)
    {
        puts("fadt present=0");
        return 0;
    }
    if (length < sizeof(header) || length == sizeof(table))
    {
        fprintf(stderr, "acpica-fadt: %zu bytes is not a FADT\n", length);
        return 1;
    }
    memcpy(&header, table, sizeof(header));
    if (header.Length > length)
    {
        fprintf(stderr, "acpica-fadt: the header claims %u bytes, the input holds %zu\n", header.Length, length);
        return 1;
    }
    AcpiGbl_OutputFile = stderr;
    AcpiGbl_UseDefaultRegisterWidths = FALSE;
    AcpiTbCreateLocalFadt((ACPI_TABLE_HEADER *) table, header.Length);
    acpica_fadt_print();
    return 0;
}
