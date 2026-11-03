#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>

#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_directory, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @name Where each table of the made-up firmware sits in its page
 * @{
 */
#define MADE_UP_FIRMWARE_RSDP_OFFSET       0x000u /**< The root system description pointer. */
#define MADE_UP_FIRMWARE_XSDT_OFFSET       0x040u /**< The XSDT. */
#define MADE_UP_FIRMWARE_RSDT_OFFSET       0x100u /**< The RSDT. */
#define MADE_UP_FIRMWARE_BOTH_OFFSET       0x200u /**< "TSTA", which both root tables list. */
#define MADE_UP_FIRMWARE_EXTENDED_OFFSET   0x240u /**< "TSTB", which only the XSDT lists. */
#define MADE_UP_FIRMWARE_CORRUPT_OFFSET    0x280u /**< "TSTC", whose checksum fails. */
#define MADE_UP_FIRMWARE_FADT_OFFSET       0x300u /**< The FADT the XSDT lists: 244 bytes, with X_DSDT. */
#define MADE_UP_FIRMWARE_SHORT_FADT_OFFSET 0x400u /**< The FADT the RSDT lists: the 116 bytes of ACPI 1.0. */
#define MADE_UP_FIRMWARE_DSDT_32_OFFSET    0x500u /**< The DSDT both FADTs name in their 32-bit field. */
#define MADE_UP_FIRMWARE_DSDT_64_OFFSET    0x600u /**< The DSDT X_DSDT names. */
#define MADE_UP_FIRMWARE_FACS_32_OFFSET    0x700u /**< Where FIRMWARE_CTRL puts a FACS, when a test names one. */
#define MADE_UP_FIRMWARE_FACS_64_OFFSET    0x740u /**< Where X_FIRMWARE_CTRL puts a FACS, when a test names one. */
/** @} */

#define MADE_UP_FIRMWARE_PHYSICAL_ADDRESS               0x7FFE0000u /**< Where the made-up firmware's page lies. */
#define MADE_UP_FIRMWARE_PAGE_BYTES                     4096u       /**< Bytes of the made-up firmware's page. */
#define MADE_UP_FIRMWARE_HEADER_BYTES                   36u /**< Bytes of a table header, and of a table that is nothing more. */
#define MADE_UP_FIRMWARE_FADT_BYTES                     244u /**< Bytes of a FADT of ACPI 6. */
#define MADE_UP_FIRMWARE_SHORT_FADT_BYTES               116u /**< Bytes of a FADT of ACPI 1.0, which ends before X_DSDT. */
#define MADE_UP_FIRMWARE_ABOVE_4_GIB                    0x100000000ull /**< Added to an address, puts it above 4 GiB. */
#define MADE_UP_FIRMWARE_FADT_FIRMWARE_CONTROL          36u            /**< Offset of a FADT's FIRMWARE_CTRL field. */
#define MADE_UP_FIRMWARE_FADT_DSDT                      40u            /**< Offset of a FADT's 32-bit DSDT field. */
#define MADE_UP_FIRMWARE_FADT_EXTENDED_FIRMWARE_CONTROL 132u           /**< Offset of a FADT's X_FIRMWARE_CTRL field. */
#define MADE_UP_FIRMWARE_FADT_EXTENDED_DSDT             140u           /**< Offset of a FADT's X_DSDT field. */
#define MADE_UP_FIRMWARE_FADT_PM1A_CONTROL              64u            /**< Offset of a FADT's PM1a_CNT_BLK field. */
#define MADE_UP_FIRMWARE_FADT_FLAGS                     112u           /**< Offset of a FADT's Flags field. */
#define MADE_UP_FIRMWARE_FADT_EXTENDED_PM1B_CONTROL     184u           /**< Offset of a FADT's X_PM1b_CNT_BLK field. */
#define MADE_UP_FIRMWARE_RSDP_VERSION_1                 20u  /**< Bytes the first checksum of a root pointer covers. */
#define MADE_UP_FIRMWARE_SELF_ADDRESS                   8u   /**< Where SELF's region address lies in its AML. */
#define MADE_UP_FIRMWARE_OTHER_ADDRESS                  35u  /**< Where the address of the region over TSTA lies. */
#define MADE_UP_FIRMWARE_SELF_BOUND                     75u  /**< Where the bound of SELF's While lies in its AML. */
#define MADE_UP_FIRMWARE_LATCH_BOUND                    81u  /**< Where it lies once the DSDT writes OTHR first. */
#define MADE_UP_FIRMWARE_EVICT_BOUND                    136u /**< Where the bound of EVCT's last While lies. */
#define MADE_UP_FIRMWARE_FIRST_ADDRESS                                                                                 \
    8u                                              /**< Where the first region of a write test's DSDT                 \
                                                         holds its address. */
#define MADE_UP_FIRMWARE_SECOND_ADDRESS 34u         /**< Where the second holds it. */
#define MADE_UP_FIRMWARE_ELSEWHERE      0x800u      /**< A byte of the page that no table holds. */
#define MADE_UP_FIRMWARE_FAR_PAGE       0x00800000u /**< A page apart from the firmware's. */
#define MADE_UP_FIRMWARE_UNALIGNED_DSDT                                                                                \
    0x603u /**< A DSDT whose first byte no DWord access starts                                                         \
                at, three bytes past MADE_UP_FIRMWARE_DSDT_64_OFFSET. */
#define MADE_UP_FIRMWARE_SELF_RESULT                                                                                   \
    4u /**< What SELF returns once its write over its bound is                                                         \
            read: the loop stops at 4. */

/**
 * @brief A made-up firmware: every table in one page, which a simulated host serves.
 */
typedef struct MadeUpFirmware {
    uint8_t *page;     /**< The page, as the test addresses it. */
    uint32_t physical; /**< Where the simulated host serves it. */
} MadeUpFirmware_t;

/**
 * @brief A DSDT whose method rewrites its own bytes: the field BND_ over the bound of SELF's While and the
 *        field OTHR over a byte of TSTA, which the directory lists and the load does not read, each a byte of
 *        a SystemMemory region whose address the test writes, at MADE_UP_FIRMWARE_SELF_ADDRESS and
 *        MADE_UP_FIRMWARE_OTHER_ADDRESS; then SELF: `OTHR = One; Local0 = 0; While (Local0 < 0x10)
 *        { Local0++; If (Local0 == 2) { BND_ = 4 } }; Return (Local0)`.
 */
static const uint8_t made_up_firmware_self[] = {
    0x5Bu, 0x80u, 'T',   'B',   'L',   'B',   0x00u, 0x0Cu, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x01u, 0x5Bu, 0x81u,
    0x0Bu, 'T',   'B',   'L',   'B',   0x01u, 'B',   'N',   'D',   '_',   0x08u, 0x5Bu, 0x80u, 'T',   'S',   'T',
    'R',   0x00u, 0x0Cu, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x01u, 0x5Bu, 0x81u, 0x0Bu, 'T',   'S',   'T',   'R',
    0x01u, 'O',   'T',   'H',   'R',   0x08u, 0x14u, 0x26u, 'S',   'E',   'L',   'F',   0x00u, 0x70u, 0x01u, 'O',
    'T',   'H',   'R',   0x70u, 0x00u, 0x60u, 0xA2u, 0x14u, 0x95u, 0x60u, 0x0Au, 0x10u, 0x75u, 0x60u, 0xA0u, 0x0Cu,
    0x93u, 0x60u, 0x0Au, 0x02u, 0x70u, 0x0Au, 0x04u, 'B',   'N',   'D',   '_',   0xA4u, 0x60u,
};

/**
 * @brief SELF's DSDT, whose table-level code writes OTHR, over a byte of TSTA, before SELF is declared: `OTHR =
 *        One` runs while the DSDT is declared, the first write through the firmware's page.
 */
static const uint8_t made_up_firmware_latch[] = {
    0x5Bu, 0x80u, 'T',   'B',   'L',   'B',   0x00u, 0x0Cu, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x01u, 0x5Bu,
    0x81u, 0x0Bu, 'T',   'B',   'L',   'B',   0x01u, 'B',   'N',   'D',   '_',   0x08u, 0x5Bu, 0x80u, 'T',
    'S',   'T',   'R',   0x00u, 0x0Cu, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x01u, 0x5Bu, 0x81u, 0x0Bu, 'T',
    'S',   'T',   'R',   0x01u, 'O',   'T',   'H',   'R',   0x08u, 0x70u, 0x01u, 'O',   'T',   'H',   'R',
    0x14u, 0x26u, 'S',   'E',   'L',   'F',   0x00u, 0x70u, 0x01u, 'O',   'T',   'H',   'R',   0x70u, 0x00u,
    0x60u, 0xA2u, 0x14u, 0x95u, 0x60u, 0x0Au, 0x10u, 0x75u, 0x60u, 0xA0u, 0x0Cu, 0x93u, 0x60u, 0x0Au, 0x02u,
    0x70u, 0x0Au, 0x04u, 'B',   'N',   'D',   '_',   0xA4u, 0x60u,
};

/**
 * @brief A DSDT whose method maps 64 pages before it writes its own bytes: BND_ over the bound of EVCT's last
 *        While, FARW a byte of a page apart, each a byte of a SystemMemory region whose address the test writes;
 *        TOUC (Arg0) reads a byte at Arg0 through a region of its own; EVCT: `FARW = One; Local1 = 0; While (Local1
 *        < 63) { TOUC (0x01000000 + Local1 * 0x1000); Local1++ }; Local0 = 0; While (Local0 < 0x10) { Local0++;
 *        If (Local0 == 2) { BND_ = 4 } }; Return (Local0)`: the write over BND_ takes the mapping of FARW's page,
 *        the least recently used of the 64 the cache holds.
 */
static const uint8_t made_up_firmware_evict[] = {
    0x5Bu, 0x80u, 'T',   'B',   'L',   'B',   0x00u, 0x0Cu, 0x78u, 0x56u, 0x34u, 0x12u, 0x01u, 0x5Bu, 0x81u, 0x0Bu,
    'T',   'B',   'L',   'B',   0x01u, 'B',   'N',   'D',   '_',   0x08u, 0x5Bu, 0x80u, 'F',   'A',   'R',   'R',
    0x00u, 0x0Cu, 0x78u, 0x56u, 0x34u, 0x12u, 0x01u, 0x5Bu, 0x81u, 0x0Bu, 'F',   'A',   'R',   'R',   0x01u, 'F',
    'A',   'R',   'W',   0x08u, 0x14u, 0x21u, 'T',   'O',   'U',   'C',   0x09u, 0x5Bu, 0x80u, 'S',   'C',   'A',
    'N',   0x00u, 0x68u, 0x01u, 0x5Bu, 0x81u, 0x0Bu, 'S',   'C',   'A',   'N',   0x01u, 'S',   'C',   'N',   'B',
    0x08u, 0xA4u, 'S',   'C',   'N',   'B',   0x14u, 0x43u, 0x04u, 'E',   'V',   'C',   'T',   0x00u, 0x70u, 0x01u,
    'F',   'A',   'R',   'W',   0x70u, 0x00u, 0x61u, 0xA2u, 0x18u, 0x95u, 0x61u, 0x0Au, 0x3Fu, 'T',   'O',   'U',
    'C',   0x72u, 0x0Cu, 0x00u, 0x00u, 0x00u, 0x01u, 0x77u, 0x61u, 0x0Bu, 0x00u, 0x10u, 0x00u, 0x00u, 0x75u, 0x61u,
    0x70u, 0x00u, 0x60u, 0xA2u, 0x14u, 0x95u, 0x60u, 0x0Au, 0x10u, 0x75u, 0x60u, 0xA0u, 0x0Cu, 0x93u, 0x60u, 0x0Au,
    0x02u, 0x70u, 0x0Au, 0x04u, 'B',   'N',   'D',   '_',   0xA4u, 0x60u,
};

/**
 * @brief A DSDT that writes One to five bytes or words whose addresses the test writes, each through a
 *        SystemMemory region of its own and a method: BEFO by WBEF, FRST by WFRS, LAST by WLST and AFTE by WAFT,
 *        bytes, and DWRD by WDWD, a DWordAcc unit of 32 bits.
 */
static const uint8_t made_up_firmware_edges[] = {
    0x5Bu, 0x80u, 'B',   'E',   'F',   'R',   0x00u, 0x0Cu, 0x78u, 0x56u, 0x34u, 0x12u, 0x01u, 0x5Bu, 0x81u, 0x0Bu,
    'B',   'E',   'F',   'R',   0x01u, 'B',   'E',   'F',   'O',   0x08u, 0x5Bu, 0x80u, 'F',   'R',   'S',   'R',
    0x00u, 0x0Cu, 0x78u, 0x56u, 0x34u, 0x12u, 0x01u, 0x5Bu, 0x81u, 0x0Bu, 'F',   'R',   'S',   'R',   0x01u, 'F',
    'R',   'S',   'T',   0x08u, 0x5Bu, 0x80u, 'L',   'S',   'T',   'R',   0x00u, 0x0Cu, 0x78u, 0x56u, 0x34u, 0x12u,
    0x01u, 0x5Bu, 0x81u, 0x0Bu, 'L',   'S',   'T',   'R',   0x01u, 'L',   'A',   'S',   'T',   0x08u, 0x5Bu, 0x80u,
    'A',   'F',   'T',   'R',   0x00u, 0x0Cu, 0x78u, 0x56u, 0x34u, 0x12u, 0x01u, 0x5Bu, 0x81u, 0x0Bu, 'A',   'F',
    'T',   'R',   0x01u, 'A',   'F',   'T',   'E',   0x08u, 0x5Bu, 0x80u, 'D',   'W',   'R',   'R',   0x00u, 0x0Cu,
    0x78u, 0x56u, 0x34u, 0x12u, 0x0Au, 0x04u, 0x5Bu, 0x81u, 0x0Bu, 'D',   'W',   'R',   'R',   0x03u, 'D',   'W',
    'R',   'D',   0x20u, 0x14u, 0x0Cu, 'W',   'B',   'E',   'F',   0x00u, 0x70u, 0x01u, 'B',   'E',   'F',   'O',
    0x14u, 0x0Cu, 'W',   'F',   'R',   'S',   0x00u, 0x70u, 0x01u, 'F',   'R',   'S',   'T',   0x14u, 0x0Cu, 'W',
    'L',   'S',   'T',   0x00u, 0x70u, 0x01u, 'L',   'A',   'S',   'T',   0x14u, 0x0Cu, 'W',   'A',   'F',   'T',
    0x00u, 0x70u, 0x01u, 'A',   'F',   'T',   'E',   0x14u, 0x0Cu, 'W',   'D',   'W',   'D',   0x00u, 0x70u, 0x01u,
    'D',   'W',   'R',   'D',
};

/**
 * @brief A DSDT whose LDTB runs `LoadTable ("TSTA", "", "", "", "", Zero); OTHR = One; Return (Zero)`, OTHR a byte
 *        of a SystemMemory region whose address the test writes.
 */
static const uint8_t made_up_firmware_load_table[] = {
    0x5Bu, 0x80u, 'T',   'S',   'T',   'R',   0x00u, 0x0Cu, 0x78u, 0x56u, 0x34u, 0x12u, 0x01u, 0x5Bu, 0x81u,
    0x0Bu, 'T',   'S',   'T',   'R',   0x01u, 'O',   'T',   'H',   'R',   0x08u, 0x14u, 0x1Fu, 'L',   'D',
    'T',   'B',   0x00u, 0x5Bu, 0x1Fu, 0x0Du, 'T',   'S',   'T',   'A',   0x00u, 0x0Du, 0x00u, 0x0Du, 0x00u,
    0x0Du, 0x00u, 0x0Du, 0x00u, 0x00u, 0x70u, 0x01u, 'O',   'T',   'H',   'R',   0xA4u, 0x00u,
};

/**
 * @brief A DSDT of a write test: its AML, where it lies in the firmware's page, and the physical address each of
 *        its regions is given, at the offsets of its AML that hold one.
 */
typedef struct MadeUpWriteTable {
    const uint8_t *aml;        /**< Its AML. */
    uint32_t length;           /**< Its length. */
    uint32_t offset;           /**< Where it lies in the page, which the long FADT's X_DSDT names. */
    const uint32_t *places;    /**< Where its AML holds the address of each region, a DWordConst. */
    const uint32_t *addresses; /**< The address each of them is given. */
    uint32_t count;            /**< How many. */
} MadeUpWriteTable_t;

/**
 * @brief What one load of a write test left.
 */
typedef struct MadeUpWriteRun {
    uint64_t result;       /**< What the method evaluated returned. */
    uint32_t table_writes; /**< Writes counted on the bytes of a table the load reads in place. */
    uint32_t flushes;      /**< Times the records were emptied. */
    uint32_t map_calls;    /**< Pages the mapping cache asked the host to map. */
} MadeUpWriteRun_t;

static uint8_t made_up_firmware_page[MADE_UP_FIRMWARE_PAGE_BYTES];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t made_up_firmware_host;
static AdvancedConfigurationAndPowerInterfaceDirectory_t made_up_firmware_directory;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t made_up_firmware_load;
static void (*made_up_firmware_host_write)(void *context, void *address, uint64_t physical_address, uint8_t width,
                                           uint32_t value);

/**
 * @brief Sets the byte at @p checksum_offset so that the @p length bytes at @p bytes sum to zero.
 */
static void made_up_firmware_seal(uint8_t *bytes, uint32_t length, uint32_t checksum_offset)
{
    uint8_t sum = 0u;

    bytes[checksum_offset] = 0u;
    for (uint32_t index = 0u; index < length; ++index)
        sum = (uint8_t) (sum + bytes[index]);
    bytes[checksum_offset] = (uint8_t) (0u - sum);
}

/**
 * @brief Writes a table header at @p offset of the firmware's page.
 *
 * @return The header, whose body the caller fills before made_up_firmware_seal_table.
 */
static AdvancedConfigurationAndPowerInterfaceSdtHeader_t *made_up_firmware_table(const MadeUpFirmware_t *firmware,
                                                                                 uint32_t offset, const char *signature,
                                                                                 uint32_t length,
                                                                                 const char *oem_table_id)
{
    AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header =
        (AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) (firmware->page + offset);

    memcpy(header->signature, signature, sizeof(header->signature));
    header->length = length;
    header->revision = 1u;
    memcpy(header->oem_table_id, oem_table_id, sizeof(header->oem_table_id));
    return header;
}

/**
 * @brief Sets the checksum of the table at @p offset of the firmware's page, once its body is written.
 */
static void made_up_firmware_seal_table(const MadeUpFirmware_t *firmware, uint32_t offset)
{
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header =
        (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) (firmware->page + offset);

    made_up_firmware_seal(firmware->page + offset, header->length,
                          offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, checksum));
}

/**
 * @brief Writes a FADT at @p offset, @p length bytes long, naming the DSDT in its 32-bit field and,
 *        when it is long enough, @p extended_dsdt in X_DSDT.
 */
static void made_up_firmware_write_fadt(const MadeUpFirmware_t *firmware, uint32_t offset, uint32_t length,
                                        uint64_t extended_dsdt)
{
    const uint32_t dsdt = firmware->physical + MADE_UP_FIRMWARE_DSDT_32_OFFSET;

    made_up_firmware_table(firmware, offset, "FACP", length, "LPLFADT ");
    memcpy(firmware->page + offset + MADE_UP_FIRMWARE_FADT_DSDT, &dsdt, sizeof(dsdt));
    if (length >= MADE_UP_FIRMWARE_FADT_EXTENDED_DSDT + sizeof(extended_dsdt))
        memcpy(firmware->page + offset + MADE_UP_FIRMWARE_FADT_EXTENDED_DSDT, &extended_dsdt, sizeof(extended_dsdt));
    made_up_firmware_seal_table(firmware, offset);
}

/**
 * @brief Names a FACS in the long FADT's FIRMWARE_CTRL and X_FIRMWARE_CTRL fields, then seals the
 *        FADT again.
 */
static void made_up_firmware_name_facs(const MadeUpFirmware_t *firmware, uint32_t firmware_control,
                                       uint64_t extended_firmware_control)
{
    uint8_t *fadt = firmware->page + MADE_UP_FIRMWARE_FADT_OFFSET;

    memcpy(fadt + MADE_UP_FIRMWARE_FADT_FIRMWARE_CONTROL, &firmware_control, sizeof(firmware_control));
    memcpy(fadt + MADE_UP_FIRMWARE_FADT_EXTENDED_FIRMWARE_CONTROL, &extended_firmware_control,
           sizeof(extended_firmware_control));
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_FADT_OFFSET);
}

/**
 * @brief Writes the made-up firmware into its page.
 *
 * @details The XSDT lists TSTA, TSTB, TSTC, TSTA again above 4 GiB, and the long FADT; the RSDT
 *          lists TSTA and the short FADT. Both FADTs name the DSDT "THIRTY2" in their 32-bit field;
 *          the long one names @p extended_dsdt in X_DSDT.
 *
 * @param rsdp_revision The root pointer's revision: 0 for ACPI 1.0, 2 for a pointer that names the XSDT.
 * @param extended_dsdt What the long FADT's X_DSDT holds.
 */
static void made_up_firmware_write(const MadeUpFirmware_t *firmware, uint8_t rsdp_revision, uint64_t extended_dsdt)
{
    const uint32_t base = firmware->physical;
    const uint64_t extended_entries[] = {
        base + MADE_UP_FIRMWARE_BOTH_OFFSET,    base + MADE_UP_FIRMWARE_EXTENDED_OFFSET,
        base + MADE_UP_FIRMWARE_CORRUPT_OFFSET, MADE_UP_FIRMWARE_ABOVE_4_GIB + base + MADE_UP_FIRMWARE_BOTH_OFFSET,
        base + MADE_UP_FIRMWARE_FADT_OFFSET,
    };
    const uint32_t entries[] = {base + MADE_UP_FIRMWARE_BOTH_OFFSET, base + MADE_UP_FIRMWARE_SHORT_FADT_OFFSET};

    memset(firmware->page, 0, MADE_UP_FIRMWARE_PAGE_BYTES);

    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_BOTH_OFFSET, "TSTA", MADE_UP_FIRMWARE_HEADER_BYTES, "BOTH    ");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_BOTH_OFFSET);
    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_EXTENDED_OFFSET, "TSTB", MADE_UP_FIRMWARE_HEADER_BYTES,
                           "EXTENDED");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_EXTENDED_OFFSET);
    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_CORRUPT_OFFSET, "TSTC", MADE_UP_FIRMWARE_HEADER_BYTES,
                           "CORRUPT ");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_CORRUPT_OFFSET);
    firmware->page[MADE_UP_FIRMWARE_CORRUPT_OFFSET +
                   offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, oem_table_id)] ^= 0x01u;

    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_DSDT_32_OFFSET, "DSDT", MADE_UP_FIRMWARE_HEADER_BYTES,
                           "THIRTY2 ");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_DSDT_32_OFFSET);
    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_DSDT_64_OFFSET, "DSDT", MADE_UP_FIRMWARE_HEADER_BYTES,
                           "SIXTY4  ");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    made_up_firmware_write_fadt(firmware, MADE_UP_FIRMWARE_FADT_OFFSET, MADE_UP_FIRMWARE_FADT_BYTES, extended_dsdt);
    made_up_firmware_write_fadt(firmware, MADE_UP_FIRMWARE_SHORT_FADT_OFFSET, MADE_UP_FIRMWARE_SHORT_FADT_BYTES, 0u);

    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_XSDT_OFFSET, "XSDT",
                           MADE_UP_FIRMWARE_HEADER_BYTES + sizeof(extended_entries), "ROOT64  ");
    memcpy(firmware->page + MADE_UP_FIRMWARE_XSDT_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES, extended_entries,
           sizeof(extended_entries));
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_XSDT_OFFSET);
    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_RSDT_OFFSET, "RSDT",
                           MADE_UP_FIRMWARE_HEADER_BYTES + sizeof(entries), "ROOT32  ");
    memcpy(firmware->page + MADE_UP_FIRMWARE_RSDT_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES, entries, sizeof(entries));
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_RSDT_OFFSET);

    AdvancedConfigurationAndPowerInterfaceRsdp_t *rsdp =
        (AdvancedConfigurationAndPowerInterfaceRsdp_t *) (firmware->page + MADE_UP_FIRMWARE_RSDP_OFFSET);

    memcpy(rsdp->signature, "RSD PTR ", sizeof(rsdp->signature));
    memcpy(rsdp->oem_id, "LPLTST", sizeof(rsdp->oem_id));
    rsdp->revision = rsdp_revision;
    rsdp->rsdt_address = base + MADE_UP_FIRMWARE_RSDT_OFFSET;
    rsdp->length = sizeof(*rsdp);
    rsdp->xsdt_address = base + MADE_UP_FIRMWARE_XSDT_OFFSET;
    made_up_firmware_seal((uint8_t *) rsdp, MADE_UP_FIRMWARE_RSDP_VERSION_1,
                          offsetof(AdvancedConfigurationAndPowerInterfaceRsdp_t, checksum));
    made_up_firmware_seal((uint8_t *) rsdp, sizeof(*rsdp),
                          offsetof(AdvancedConfigurationAndPowerInterfaceRsdp_t, extended_checksum));
}

/**
 * @brief Serves the made-up firmware's page through a simulated host whose root pointer is the
 *        one at MADE_UP_FIRMWARE_RSDP_OFFSET.
 *
 * @details The page is served again MADE_UP_FIRMWARE_ABOVE_4_GIB higher, so that an address a test
 *          puts above 4 GiB names real bytes, and only the directory's refusal keeps them out.
 */
static void made_up_firmware_open(MadeUpFirmware_t *firmware)
{
    firmware->page = made_up_firmware_page;
    firmware->physical = MADE_UP_FIRMWARE_PHYSICAL_ADDRESS;
    advanced_configuration_and_power_interface_simulated_host_open(&made_up_firmware_host);
    (void) advanced_configuration_and_power_interface_simulated_host_place(&made_up_firmware_host, firmware->physical,
                                                                           firmware->page, MADE_UP_FIRMWARE_PAGE_BYTES);
    (void) advanced_configuration_and_power_interface_simulated_host_place(
        &made_up_firmware_host, MADE_UP_FIRMWARE_ABOVE_4_GIB + firmware->physical, firmware->page,
        MADE_UP_FIRMWARE_PAGE_BYTES);
    made_up_firmware_host.root_system_description_pointer = firmware->physical + MADE_UP_FIRMWARE_RSDP_OFFSET;
}

/**
 * @brief Reads the directory of the made-up firmware.
 *
 * @return Whether it was read.
 */
static bool made_up_firmware_read(void)
{
    return advanced_configuration_and_power_interface_read_directory(&made_up_firmware_host.host,
                                                                     &made_up_firmware_directory);
}

/**
 * @brief Unmaps what the directory read, and checks that every window it mapped is unmapped, each
 *        by the address and the length it was mapped with.
 */
static void made_up_firmware_release(KernelTest_t *test)
{
    advanced_configuration_and_power_interface_release_directory(&made_up_firmware_directory);
    kernel_test_check(test, made_up_firmware_host.outstanding_maps == 0u,
                      "every window the directory mapped is unmapped once it is released");
    kernel_test_check(test, made_up_firmware_host.mismatched_unmaps == 0u,
                      "every unmap names a window by the address and the length it was mapped with");
}

/**
 * @brief Whether the directory's DSDT is the one whose OEM table identifier is @p oem_table_id.
 */
static bool made_up_firmware_dsdt_is(const char *oem_table_id)
{
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *dsdt =
        advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "DSDT");

    return dsdt != NULL && memcmp(dsdt->oem_table_id, oem_table_id, sizeof(dsdt->oem_table_id)) == 0;
}

/**
 * @brief Whether the directory records the table at @p header as one whose checksum fails.
 */
static bool made_up_firmware_checksum_fails(const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header)
{
    for (uint32_t index = 0u; index < made_up_firmware_directory.table_count; ++index)
    {
        if (made_up_firmware_directory.tables[index].header == header)
            return !made_up_firmware_directory.tables[index].checksum_valid;
    }
    return false;
}

/**
 * @brief The I/O port of the PM1 control block @p index of the directory's FADT, or 0.
 */
static uint16_t made_up_firmware_control_port(uint32_t index)
{
    return advanced_configuration_and_power_interface_input_output_port(
        &made_up_firmware_directory.fixed_description.power_management_control_blocks[index], 2u);
}

/**
 * @brief Writes AML at the end of the table at @p offset, which grows by @p length bytes, then seals it;
 *        with @p corrupt, its checksum is then made to fail.
 */
static void made_up_firmware_append_aml(const MadeUpFirmware_t *firmware, uint32_t offset, const uint8_t *aml,
                                        uint32_t length, bool corrupt)
{
    AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header =
        (AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) (firmware->page + offset);

    memcpy(firmware->page + offset + header->length, aml, length);
    header->length += length;
    made_up_firmware_seal_table(firmware, offset);
    header->checksum ^= corrupt ? 0x01u : 0x00u;
}

/**
 * @brief Whether @p load declared a child of the root named @p name.
 */
static bool made_up_firmware_declares(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                      const char *name)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_find_child(
               &load->space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
               advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) name, 0u)) !=
           0u;
}

/**
 * @brief The simulated host's memory write, made through to the firmware's page too, so that a write over a
 *        table's bytes changes the bytes the load reads in place, as it would on a machine; the simulated host
 *        alone keeps what a region writes apart from the ranges it serves.
 */
static void made_up_firmware_write_through(void *context, void *address, uint64_t physical_address, uint8_t width,
                                           uint32_t value)
{
    made_up_firmware_host_write(context, address, physical_address, width, value);
    for (uint8_t offset = 0u; offset < width; ++offset)
        if (physical_address + offset >= MADE_UP_FIRMWARE_PHYSICAL_ADDRESS &&
            physical_address + offset < MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_PAGE_BYTES)
            made_up_firmware_page[physical_address + offset - MADE_UP_FIRMWARE_PHYSICAL_ADDRESS] =
                (uint8_t) (value >> (8u * offset));
}

/**
 * @brief Writes the made-up firmware with @p table as its DSDT, served through a host whose writes go through to
 *        the page when @p through holds, reads its directory, then loads it, records on when @p predecodes holds.
 *
 * @return false, the host closed, when the directory is not read or the namespace does not open.
 */
static bool made_up_firmware_load_writes(KernelTest_t *test, const MadeUpWriteTable_t *table, bool through,
                                         bool predecodes)
{
    MadeUpFirmware_t firmware;

    made_up_firmware_open(&firmware);
    made_up_firmware_host_write = made_up_firmware_host.host.write_memory;
    if (through)
        made_up_firmware_host.host.write_memory = made_up_firmware_write_through;
    made_up_firmware_write(&firmware, 2u, firmware.physical + table->offset);
    made_up_firmware_table(&firmware, table->offset, "DSDT", MADE_UP_FIRMWARE_HEADER_BYTES + table->length, "SIXTY4  ");
    memcpy(firmware.page + table->offset + MADE_UP_FIRMWARE_HEADER_BYTES, table->aml, table->length);
    for (uint32_t index = 0u; index < table->count; ++index)
        memcpy(firmware.page + table->offset + MADE_UP_FIRMWARE_HEADER_BYTES + table->places[index],
               &table->addresses[index], sizeof(table->addresses[index]));
    made_up_firmware_seal_table(&firmware, table->offset);
    if (!kernel_test_check(test, made_up_firmware_read(), "the directory is read") ||
        !kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &made_up_firmware_load, &made_up_firmware_host.host, 1024u, 1024u * 1024u),
                           "the namespace opens"))
    {
        advanced_configuration_and_power_interface_simulated_host_close(&made_up_firmware_host);
        return false;
    }
    made_up_firmware_load.predecodes_loops = predecodes;
    advanced_configuration_and_power_interface_machine_language_load_directory(&made_up_firmware_load,
                                                                               &made_up_firmware_directory);
    return true;
}

/**
 * @brief Closes the load of a write test, releases the directory and closes the host, every page it took for
 *        a write given back.
 */
static void made_up_firmware_unload_writes(KernelTest_t *test)
{
    advanced_configuration_and_power_interface_machine_language_close(&made_up_firmware_load);
    made_up_firmware_release(test);
    advanced_configuration_and_power_interface_simulated_host_close(&made_up_firmware_host);
}

/**
 * @brief Loads @p table, its writes made through to the page, records on when @p predecodes holds, evaluates
 *        @p path, and gives what it left.
 *
 * @return false when the load could not be made.
 */
static bool made_up_firmware_run_writes(KernelTest_t *test, const MadeUpWriteTable_t *table, const char *path,
                                        bool predecodes, MadeUpWriteRun_t *out_run)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result = {0};

    if (!made_up_firmware_load_writes(test, table, true, predecodes))
        return false;
    advanced_configuration_and_power_interface_machine_language_evaluate(&made_up_firmware_load, path, NULL, 0u,
                                                                         &result);
    out_run->result = result.payload;
    out_run->table_writes = made_up_firmware_load.regions.table_writes;
    out_run->flushes = made_up_firmware_load.predecoding.flushes;
    out_run->map_calls = made_up_firmware_load.regions.map_calls;
    made_up_firmware_unload_writes(test);
    return true;
}

/**
 * @brief Runs @p path of @p table without records, then with them, and checks that SELF's write over its bound,
 *        made at the second turn, is read both ways: the loop stops at 4, the write is counted once, and empties
 *        the records it finds built.
 *
 * @return What the run with records left.
 */
static MadeUpWriteRun_t made_up_firmware_check_bound_write(KernelTest_t *test, const MadeUpWriteTable_t *table,
                                                           const char *path)
{
    MadeUpWriteRun_t runs[2] = {
        {0u, 0u, 0u, 0u},
        {0u, 0u, 0u, 0u}
    };

    if (!made_up_firmware_run_writes(test, table, path, false, &runs[0]) ||
        !made_up_firmware_run_writes(test, table, path, true, &runs[1]))
        return runs[1];
    kernel_test_check(test, runs[0].result == MADE_UP_FIRMWARE_SELF_RESULT && runs[1].result == runs[0].result,
                      "the loop reads the new bound and stops at 4, with records as without");
    kernel_test_check(test, runs[0].table_writes == 1u && runs[1].table_writes == 1u,
                      "the write over the bound is counted once, with records as without");
    kernel_test_check(test, runs[0].flushes == 0u && runs[1].flushes == 1u, "it empties the records, once built");
    kernel_test_measure(test, "result_with_records", (uint32_t) runs[1].result);
    return runs[1];
}

/**
 * @brief A root pointer of revision 2 that names an XSDT has the XSDT walked: its 64-bit entries,
 *        and X_DSDT for the DSDT.
 */
KERNEL_TEST(the_extended_root_is_walked_when_the_pointer_names_one)
{
    MadeUpFirmware_t firmware;

    made_up_firmware_open(&firmware);
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);

    const bool walked = made_up_firmware_read();
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *corrupt =
        advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTC");

    kernel_test_check(test, walked && made_up_firmware_directory.walked, "the directory is read");
    kernel_test_check(test,
                      made_up_firmware_directory.extended_root && made_up_firmware_directory.root_physical_address ==
                                                                      firmware.physical + MADE_UP_FIRMWARE_XSDT_OFFSET,
                      "the XSDT is the root walked");
    kernel_test_check(
        test, advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTB") != NULL,
        "a table only the XSDT lists is found");
    kernel_test_check(test, corrupt != NULL && made_up_firmware_checksum_fails(corrupt),
                      "a table whose checksum fails is given out, recorded as failing");
    kernel_test_check(test, made_up_firmware_directory.unreachable_count == 1u,
                      "the entry above 4 GiB is counted unreachable, not truncated");
    kernel_test_check(test, made_up_firmware_directory.table_count == 5u,
                      "three tables, the FADT and the DSDT are recorded, and nothing else");
    kernel_test_check(test, made_up_firmware_dsdt_is("SIXTY4  "), "the DSDT is the one X_DSDT names");
    kernel_test_measure(test, "tables", made_up_firmware_directory.table_count);
    kernel_test_measure(test, "unreachable", made_up_firmware_directory.unreachable_count);
    made_up_firmware_release(test);
}

/**
 * @brief A root pointer of ACPI 1.0 has the RSDT walked, and the 32-bit DSDT of its FADT taken.
 */
KERNEL_TEST(an_acpi_1_pointer_walks_the_root_of_32_bit_entries)
{
    MadeUpFirmware_t firmware;

    made_up_firmware_open(&firmware);
    made_up_firmware_write(&firmware, 0u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);

    const bool walked = made_up_firmware_read();

    kernel_test_check(test, walked, "the directory is read");
    kernel_test_check(test,
                      !made_up_firmware_directory.extended_root && made_up_firmware_directory.root_physical_address ==
                                                                       firmware.physical + MADE_UP_FIRMWARE_RSDT_OFFSET,
                      "the RSDT is the root walked, although the pointer's XSDT field is filled");
    kernel_test_check(
        test, !advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTB"),
        "a table only the XSDT lists is not found");
    kernel_test_check(test, made_up_firmware_directory.table_count == 3u,
                      "one table, the FADT and the DSDT are recorded");
    kernel_test_check(test, made_up_firmware_dsdt_is("THIRTY2 "), "the DSDT is the one a FADT of ACPI 1.0 names");
    kernel_test_measure(test, "tables", made_up_firmware_directory.table_count);
    made_up_firmware_release(test);
}

/**
 * @brief An XSDT whose checksum fails gives way to the RSDT, and a pointer whose own checksum fails
 *        is refused.
 */
KERNEL_TEST(a_root_whose_checksum_fails_is_not_walked)
{
    MadeUpFirmware_t firmware;

    made_up_firmware_open(&firmware);
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    firmware.page[MADE_UP_FIRMWARE_XSDT_OFFSET +
                  offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, oem_table_id)] ^= 0x01u;

    const bool walked = made_up_firmware_read();

    kernel_test_check(test, walked && !made_up_firmware_directory.extended_root, "the RSDT is walked instead");
    kernel_test_check(
        test, !advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTB"),
        "nothing the XSDT lists is taken from it");
    made_up_firmware_release(test);

    firmware.page[MADE_UP_FIRMWARE_RSDP_OFFSET + offsetof(AdvancedConfigurationAndPowerInterfaceRsdp_t, oem_id)] ^=
        0x01u;
    kernel_test_check(test, !made_up_firmware_read() && made_up_firmware_directory.table_count == 0u,
                      "a root pointer whose checksum fails is refused, and nothing is recorded");
    made_up_firmware_release(test);
}

/**
 * @brief An X_DSDT above 4 GiB gives way to the 32-bit DSDT, which this kernel can map.
 */
KERNEL_TEST(a_dsdt_above_4_gib_gives_way_to_the_32_bit_one)
{
    MadeUpFirmware_t firmware;

    made_up_firmware_open(&firmware);
    made_up_firmware_write(&firmware, 2u,
                           MADE_UP_FIRMWARE_ABOVE_4_GIB + firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);

    const bool walked = made_up_firmware_read();

    kernel_test_check(test, walked && made_up_firmware_directory.extended_root, "the XSDT is walked");
    kernel_test_check(test, made_up_firmware_dsdt_is("THIRTY2 "), "the DSDT is the one the 32-bit field names");
    made_up_firmware_release(test);
}

/**
 * @brief Of two tables with one signature, the one whose checksum holds is given out, although the
 *        one whose checksum fails comes first.
 */
KERNEL_TEST(a_table_whose_checksum_holds_wins_over_one_that_fails)
{
    MadeUpFirmware_t firmware;

    made_up_firmware_open(&firmware);
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    firmware.page[MADE_UP_FIRMWARE_BOTH_OFFSET +
                  offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, oem_table_id)] ^= 0x01u;
    memcpy(firmware.page + MADE_UP_FIRMWARE_EXTENDED_OFFSET, "TSTA", 4u);
    made_up_firmware_seal_table(&firmware, MADE_UP_FIRMWARE_EXTENDED_OFFSET);

    const bool walked = made_up_firmware_read();
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *table =
        advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTA");

    kernel_test_check(test, walked, "the directory is read");
    kernel_test_check(test, table != NULL && memcmp(table->oem_table_id, "EXTENDED", sizeof(table->oem_table_id)) == 0,
                      "the second TSTA, whose checksum holds, is the one given out");
    made_up_firmware_release(test);
}

/**
 * @brief Of two tables with one signature whose checksums both fail, the first is given out, its record with it.
 */
KERNEL_TEST(of_two_tables_whose_checksum_fails_the_first_is_given_out)
{
    MadeUpFirmware_t firmware;

    made_up_firmware_open(&firmware);
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    firmware.page[MADE_UP_FIRMWARE_BOTH_OFFSET +
                  offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, oem_table_id)] ^= 0x01u;
    memcpy(firmware.page + MADE_UP_FIRMWARE_EXTENDED_OFFSET, "TSTA", 4u);

    const bool walked = made_up_firmware_read();
    const AdvancedConfigurationAndPowerInterfaceTable_t *entry =
        advanced_configuration_and_power_interface_find_entry_in_directory(&made_up_firmware_directory, "TSTA");

    kernel_test_check(test, walked, "the directory is read");
    kernel_test_check(test,
                      entry != NULL && !entry->checksum_valid &&
                          entry->physical_address == firmware.physical + MADE_UP_FIRMWARE_BOTH_OFFSET &&
                          advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory,
                                                                                       "TSTA") == entry->header,
                      "the first TSTA, whose checksum fails as the second's does, is the one given out");
    made_up_firmware_release(test);
}

/**
 * @brief The FACS is the one FIRMWARE_CTRL names when it is not 0, and the one X_FIRMWARE_CTRL names
 *        otherwise, as ACPICA and uACPI choose.
 */
KERNEL_TEST(the_facs_is_the_one_the_32_bit_field_names_when_it_is_set)
{
    MadeUpFirmware_t firmware;

    made_up_firmware_open(&firmware);

    const uint32_t facs_32 = firmware.physical + MADE_UP_FIRMWARE_FACS_32_OFFSET;
    const uint32_t facs_64 = firmware.physical + MADE_UP_FIRMWARE_FACS_64_OFFSET;

    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    made_up_firmware_name_facs(&firmware, facs_32, facs_64);
    kernel_test_check(
        test, made_up_firmware_read() && made_up_firmware_directory.firmware_control_physical_address == facs_32,
        "with both fields set, the 32-bit one is taken");
    made_up_firmware_release(test);

    made_up_firmware_name_facs(&firmware, 0u, facs_64);
    kernel_test_check(
        test, made_up_firmware_read() && made_up_firmware_directory.firmware_control_physical_address == facs_64,
        "with the 32-bit field at 0, X_FIRMWARE_CTRL is taken");
    made_up_firmware_release(test);

    made_up_firmware_name_facs(&firmware, 0u, MADE_UP_FIRMWARE_ABOVE_4_GIB + facs_64);
    kernel_test_check(test,
                      made_up_firmware_read() && made_up_firmware_directory.firmware_control_physical_address == 0u,
                      "an X_FIRMWARE_CTRL above 4 GiB, with the 32-bit field at 0, gives no FACS");
    made_up_firmware_release(test);
}

/**
 * @brief A host that gives no root pointer, or one above 4 GiB, has no directory read, and nothing
 *        mapped; one above 4 GiB is counted unreachable.
 */
KERNEL_TEST(a_host_without_a_root_pointer_below_4_gib_gives_no_directory)
{
    MadeUpFirmware_t firmware;

    made_up_firmware_open(&firmware);
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);

    made_up_firmware_host.root_system_description_pointer = 0u;
    kernel_test_check(test, !made_up_firmware_read() && !made_up_firmware_directory.walked,
                      "a host that gives no root pointer has no directory");
    made_up_firmware_release(test);

    made_up_firmware_host.root_system_description_pointer =
        MADE_UP_FIRMWARE_ABOVE_4_GIB + firmware.physical + MADE_UP_FIRMWARE_RSDP_OFFSET;
    kernel_test_check(test, !made_up_firmware_read() && made_up_firmware_directory.table_count == 0u,
                      "a root pointer above 4 GiB is not truncated to one below");
    kernel_test_check(test, made_up_firmware_directory.unreachable_count == 1u,
                      "a root pointer above 4 GiB is counted unreachable, not taken for none");
    made_up_firmware_release(test);
}

/**
 * @brief The directory takes the PM1a and PM1b control ports from the FADT, a 32-bit block when it is
 *        set, else an extended block in the I/O space, and reads the hardware-reduced flag.
 */
KERNEL_TEST(the_power_management_control_ports_and_the_hardware_reduced_flag_are_read)
{
    static const uint8_t extended_io[12] = {0x01u, 16u, 0u, 2u, 0x05u, 0x08u};
    static const uint8_t extended_memory[12] = {0x00u, 16u, 0u, 2u, 0x05u, 0x08u};
    MadeUpFirmware_t firmware;
    uint8_t *fadt = NULL;
    const uint32_t pm1a = 0x0604u;
    const uint32_t beyond_ports = 0x10604u;
    const uint32_t reduced = 1u << 20u;

    made_up_firmware_open(&firmware);
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    fadt = firmware.page + MADE_UP_FIRMWARE_FADT_OFFSET;
    memcpy(fadt + MADE_UP_FIRMWARE_FADT_PM1A_CONTROL, &pm1a, sizeof(pm1a));
    memcpy(fadt + MADE_UP_FIRMWARE_FADT_EXTENDED_PM1B_CONTROL, extended_io, sizeof(extended_io));
    made_up_firmware_seal_table(&firmware, MADE_UP_FIRMWARE_FADT_OFFSET);
    kernel_test_check(test,
                      made_up_firmware_read() && made_up_firmware_control_port(0u) == 0x0604u &&
                          made_up_firmware_control_port(1u) == 0x0805u &&
                          !made_up_firmware_directory.fixed_description.hardware_reduced,
                      "PM1a from its 32-bit block, PM1b from its extended block in the I/O space");
    made_up_firmware_release(test);
    memcpy(fadt + MADE_UP_FIRMWARE_FADT_EXTENDED_PM1B_CONTROL, extended_memory, sizeof(extended_memory));
    made_up_firmware_seal_table(&firmware, MADE_UP_FIRMWARE_FADT_OFFSET);
    kernel_test_check(test,
                      made_up_firmware_read() && made_up_firmware_control_port(1u) == 0u &&
                          made_up_firmware_directory.fixed_description.power_management_control_blocks[1].address ==
                              0x0805u,
                      "an extended block in memory is kept, and gives no port");
    made_up_firmware_release(test);
    memcpy(fadt + MADE_UP_FIRMWARE_FADT_FLAGS, &reduced, sizeof(reduced));
    made_up_firmware_seal_table(&firmware, MADE_UP_FIRMWARE_FADT_OFFSET);
    kernel_test_check(test,
                      made_up_firmware_read() && made_up_firmware_directory.fixed_description.hardware_reduced &&
                          made_up_firmware_control_port(0u) == 0u,
                      "HW_REDUCED_ACPI is read, and leaves no PM1 control port");
    made_up_firmware_release(test);
    memcpy(fadt + MADE_UP_FIRMWARE_FADT_PM1A_CONTROL, &beyond_ports, sizeof(beyond_ports));
    memset(fadt + MADE_UP_FIRMWARE_FADT_FLAGS, 0, sizeof(reduced));
    made_up_firmware_seal_table(&firmware, MADE_UP_FIRMWARE_FADT_OFFSET);
    kernel_test_check(test,
                      made_up_firmware_read() && made_up_firmware_control_port(0u) == 0u &&
                          made_up_firmware_directory.fixed_description.power_management_control_blocks[0].address ==
                              beyond_ports,
                      "a 32-bit block past port 0xFFFF gives no port, never a truncated one");
    made_up_firmware_release(test);
}

/**
 * @brief A load of the directory takes its global lock from the FACS the FADT names, unless the FADT
 *        says the platform is hardware-reduced, which has none.
 */
KERNEL_TEST(a_load_takes_its_global_lock_from_the_facs_unless_the_platform_is_hardware_reduced)
{
    static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;
    MadeUpFirmware_t firmware;
    const uint32_t length = 64u;
    const uint32_t reduced = 1u << 20u;
    uint32_t facs = 0u;

    made_up_firmware_open(&firmware);
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    facs = firmware.physical + MADE_UP_FIRMWARE_FACS_32_OFFSET;
    memcpy(firmware.page + MADE_UP_FIRMWARE_FACS_32_OFFSET, "FACS", 4u);
    memcpy(firmware.page + MADE_UP_FIRMWARE_FACS_32_OFFSET + 4u, &length, sizeof(length));
    made_up_firmware_name_facs(&firmware, facs, 0u);
    for (uint32_t pass = 0u; pass < 2u; ++pass)
    {
        if (pass == 1u)
        {
            memcpy(firmware.page + MADE_UP_FIRMWARE_FADT_OFFSET + MADE_UP_FIRMWARE_FADT_FLAGS, &reduced,
                   sizeof(reduced));
            made_up_firmware_seal_table(&firmware, MADE_UP_FIRMWARE_FADT_OFFSET);
        }
        if (!kernel_test_check(test, made_up_firmware_read(), "the directory is read") ||
            !kernel_test_check(test,
                               advanced_configuration_and_power_interface_machine_language_open(
                                   &load, &made_up_firmware_host.host, 1024u, 1024u * 1024u),
                               "the namespace opens"))
            return;
        advanced_configuration_and_power_interface_machine_language_load_directory(&load, &made_up_firmware_directory);
        kernel_test_check(test, load.regions.global_lock_address == ((pass == 0u) ? (uint64_t) facs + 16u : 0u),
                          (pass == 0u) ? "the FACS's GlobalLock word is the load's global lock" :
                                         "a hardware-reduced platform gives the load no global lock");
        advanced_configuration_and_power_interface_machine_language_close(&load);
        made_up_firmware_release(test);
    }
}

/**
 * @brief A load of the directory declares a definition block whose checksum fails all the same, and
 *        counts it once in checksum_bad_loaded (§5.1): the DSDT, then an SSDT, fail in turn, and a
 *        firmware whose blocks all hold counts none.
 */
KERNEL_TEST(a_load_of_the_directory_counts_each_definition_block_whose_checksum_fails)
{
    static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;
    static const uint8_t dsdt_aml[] = {0x08u, 'D', 'N', 'A', 'M', 0x01u};
    static const uint8_t ssdt_aml[] = {0x08u, 'S', 'N', 'A', 'M', 0x01u};
    static const char *const claims[] = {
        "a DSDT whose checksum fails is declared and counted once",
        "an SSDT whose checksum fails is declared and counted once",
        "a firmware whose definition blocks all hold counts none",
    };
    MadeUpFirmware_t firmware;

    for (uint32_t pass = 0u; pass < 3u; ++pass)
    {
        made_up_firmware_open(&firmware);
        made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
        made_up_firmware_append_aml(&firmware, MADE_UP_FIRMWARE_DSDT_64_OFFSET, dsdt_aml, sizeof(dsdt_aml), pass == 0u);
        memcpy(firmware.page + MADE_UP_FIRMWARE_EXTENDED_OFFSET, "SSDT", 4u);
        made_up_firmware_append_aml(&firmware, MADE_UP_FIRMWARE_EXTENDED_OFFSET, ssdt_aml, sizeof(ssdt_aml),
                                    pass == 1u);
        if (!kernel_test_check(test, made_up_firmware_read(), "the directory is read") ||
            !kernel_test_check(test,
                               advanced_configuration_and_power_interface_machine_language_open(
                                   &load, &made_up_firmware_host.host, 1024u, 1024u * 1024u),
                               "the namespace opens"))
            return;
        advanced_configuration_and_power_interface_machine_language_load_directory(&load, &made_up_firmware_directory);
        kernel_test_check(test,
                          load.table_count == 2u && load.checksum_bad_loaded == ((pass < 2u) ? 1u : 0u) &&
                              made_up_firmware_declares(&load, "DNAM") && made_up_firmware_declares(&load, "SNAM"),
                          claims[pass]);
        advanced_configuration_and_power_interface_machine_language_close(&load);
        made_up_firmware_release(test);
    }
}

/**
 * @brief A write to the bytes of a table the load reads in place is counted, and empties the records built
 *        from them (§3.6, §5.3): SELF's While reads its bound, 0x10, in the DSDT, and at its second turn
 *        writes 4 over it through a SystemMemory field, with the host's writes made through to the firmware's
 *        page; the loop then stops at 4, with records as without, the write is counted once, its write over
 *        TSTA, a table the load does not read, never, and the records, which the loop ran from by then, are
 *        emptied once.
 */
KERNEL_TEST(a_write_to_a_table_read_in_place_empties_the_records_built_from_it)
{
    const uint32_t places[] = {MADE_UP_FIRMWARE_SELF_ADDRESS, MADE_UP_FIRMWARE_OTHER_ADDRESS};
    const uint32_t addresses[] = {
        MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_DSDT_64_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES +
            MADE_UP_FIRMWARE_SELF_BOUND,
        MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_BOTH_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES - 1u,
    };
    const MadeUpWriteTable_t table = {
        made_up_firmware_self, sizeof(made_up_firmware_self), MADE_UP_FIRMWARE_DSDT_64_OFFSET, places, addresses, 2u};

    made_up_firmware_check_bound_write(test, &table, "\\SELF");
}

/**
 * @brief A write the code of a table makes while the table is declared is checked against the directory too
 *        (§5.3): the DSDT's table-level code writes OTHR, over TSTA, before SELF runs; the page still learns that
 *        it holds the DSDT, and SELF's write over its bound is counted, with records as without.
 */
KERNEL_TEST(a_write_the_code_of_a_table_makes_as_it_is_declared_is_checked)
{
    const uint32_t places[] = {MADE_UP_FIRMWARE_SELF_ADDRESS, MADE_UP_FIRMWARE_OTHER_ADDRESS};
    const uint32_t addresses[] = {
        MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_DSDT_64_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES +
            MADE_UP_FIRMWARE_LATCH_BOUND,
        MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_BOTH_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES - 1u,
    };
    const MadeUpWriteTable_t table = {
        made_up_firmware_latch, sizeof(made_up_firmware_latch), MADE_UP_FIRMWARE_DSDT_64_OFFSET, places, addresses, 2u};

    made_up_firmware_check_bound_write(test, &table, "\\SELF");
}

/**
 * @brief A page learns from its first write whether it holds a table, wherever in the page that write lands
 *        (§5.3): SELF writes OTHR, at 0x800 of the firmware's page, which no table holds, before its write over
 *        its bound, which is still counted, with records as without.
 */
KERNEL_TEST(a_first_write_elsewhere_in_a_page_still_learns_its_tables)
{
    const uint32_t places[] = {MADE_UP_FIRMWARE_SELF_ADDRESS, MADE_UP_FIRMWARE_OTHER_ADDRESS};
    const uint32_t addresses[] = {
        MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_DSDT_64_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES +
            MADE_UP_FIRMWARE_SELF_BOUND,
        MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_ELSEWHERE,
    };
    const MadeUpWriteTable_t table = {
        made_up_firmware_self, sizeof(made_up_firmware_self), MADE_UP_FIRMWARE_DSDT_64_OFFSET, places, addresses, 2u};

    made_up_firmware_check_bound_write(test, &table, "\\SELF");
}

/**
 * @brief A mapping the cache takes again for another page learns that page's tables again (§5.3): EVCT writes
 *        FARW, in a page that holds none, then reads 63 other pages, which fills the cache's 64 mappings, then
 *        writes over its bound, through the mapping FARW's page had; that write is counted, with records as
 *        without. The control: 65 pages were mapped.
 */
KERNEL_TEST(a_mapping_taken_again_learns_the_tables_of_its_new_page)
{
    const uint32_t places[] = {MADE_UP_FIRMWARE_FIRST_ADDRESS, MADE_UP_FIRMWARE_SECOND_ADDRESS};
    const uint32_t addresses[] = {
        MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_DSDT_64_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES +
            MADE_UP_FIRMWARE_EVICT_BOUND,
        MADE_UP_FIRMWARE_FAR_PAGE,
    };
    const MadeUpWriteTable_t table = {
        made_up_firmware_evict, sizeof(made_up_firmware_evict), MADE_UP_FIRMWARE_DSDT_64_OFFSET, places, addresses, 2u};
    const MadeUpWriteRun_t run = made_up_firmware_check_bound_write(test, &table, "\\EVCT");

    kernel_test_check(test,
                      run.map_calls == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_MAPPINGS + 1u,
                      "65 pages were mapped, the last through the mapping of the first");
}

/**
 * @brief A write is counted when one of its bytes lands on a table the load reads in place, and only then
 *        (§5.3): with the DSDT three bytes past a DWord boundary, a byte written just before its first byte is
 *        not counted, one on its first byte is, one on its last byte is, one just after it is not, and a DWord
 *        written at the boundary, whose last byte alone lands on the DSDT, is. The writes stay apart from the
 *        page, so that the one over the DSDT's last byte, in WDWD's code, leaves that code as it is.
 */
KERNEL_TEST(a_write_is_counted_when_one_of_its_bytes_lands_on_a_table)
{
    static const char *const methods[] = {"\\WBEF", "\\WFRS", "\\WLST", "\\WAFT", "\\WDWD"};
    static const uint32_t expected[] = {0u, 1u, 1u, 0u, 1u};
    const uint32_t places[] = {8u, 34u, 60u, 86u, 112u};
    const uint32_t dsdt = MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_UNALIGNED_DSDT;
    const uint32_t end = dsdt + MADE_UP_FIRMWARE_HEADER_BYTES + (uint32_t) sizeof(made_up_firmware_edges);
    const uint32_t addresses[] = {dsdt - 1u, dsdt, end - 1u, end, dsdt - 3u};
    const MadeUpWriteTable_t table = {
        made_up_firmware_edges, sizeof(made_up_firmware_edges), MADE_UP_FIRMWARE_UNALIGNED_DSDT, places, addresses, 5u};
    uint32_t agree = 0u;

    if (!made_up_firmware_load_writes(test, &table, false, true))
        return;
    for (uint32_t index = 0u; index < sizeof(methods) / sizeof(methods[0]); ++index)
    {
        AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result = {0};
        const uint32_t before = made_up_firmware_load.regions.table_writes;

        advanced_configuration_and_power_interface_machine_language_evaluate(&made_up_firmware_load, methods[index],
                                                                             NULL, 0u, &result);
        agree += (made_up_firmware_load.regions.table_writes - before == expected[index]) ? 1u : 0u;
    }
    kernel_test_check(test, agree == sizeof(methods) / sizeof(methods[0]),
                      "the bytes before and after the DSDT are not counted; its first, its last and a DWord over its "
                      "first are");
    kernel_test_measure(test, "table_writes", made_up_firmware_load.regions.table_writes);
    made_up_firmware_unload_writes(test);
}

/**
 * @brief A write to a table LoadTable found in the directory is counted, the load reading that table in place
 *        (§5.3): LDTB loads TSTA, then writes the last byte of its header.
 */
KERNEL_TEST(a_write_to_a_table_load_table_found_is_counted)
{
    const uint32_t places[] = {MADE_UP_FIRMWARE_FIRST_ADDRESS};
    const uint32_t addresses[] = {
        MADE_UP_FIRMWARE_PHYSICAL_ADDRESS + MADE_UP_FIRMWARE_BOTH_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES - 1u,
    };
    const MadeUpWriteTable_t table = {made_up_firmware_load_table,
                                      sizeof(made_up_firmware_load_table),
                                      MADE_UP_FIRMWARE_DSDT_64_OFFSET,
                                      places,
                                      addresses,
                                      1u};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result = {0};
    uint32_t tables = 0u;

    if (!made_up_firmware_load_writes(test, &table, true, true))
        return;
    tables = made_up_firmware_load.table_count;
    advanced_configuration_and_power_interface_machine_language_evaluate(&made_up_firmware_load, "\\LDTB", NULL, 0u,
                                                                         &result);
    kernel_test_check(test, made_up_firmware_load.table_count == tables + 1u, "LoadTable reads TSTA in place");
    kernel_test_check(test, made_up_firmware_load.regions.table_writes == 1u, "the write over TSTA is counted");
    made_up_firmware_unload_writes(test);
}
