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
 * @brief The `\_OSI` answers: methods WINS and OTHR set one bit per string `\_OSI` answers Ones for, the
 *        23 Windows strings and 20 others (Linux, Darwin, FreeBSD, HP-UX, OpenVMS, a vendor Linux
 *        string, the feature strings, the empty string, Windows strings that do not exist, a wrong
 *        case, a trailing space, a prefix); NEWS asks for "Windows 2001", "Windows 2015", "Windows
 *        2009", then "Linux"; BADA passes an Integer; the table's code stores `\_OSI ("Windows 2015")`
 *        in TRU_.
 */
static const uint8_t identity_osi_aml[] = {
    0x08u, 'T',   'R',   'U',   '_',   0x0Au, 0x00u, 0x14u, 0x16u, 'W',   'C',   'H',   'K',   0x02u, 0xA0u, 0x0Du,
    0x93u, '_',   'O',   'S',   'I',   0x68u, 0xFFu, 0xA4u, 0x79u, 0x01u, 0x69u, 0x00u, 0xA4u, 0x00u, 0x14u, 0x17u,
    'O',   'C',   'H',   'K',   0x02u, 0xA0u, 0x0Eu, 0x92u, 0x93u, '_',   'O',   'S',   'I',   0x68u, 0x00u, 0xA4u,
    0x79u, 0x01u, 0x69u, 0x00u, 0xA4u, 0x00u, 0x14u, 0x47u, 0x23u, 'W',   'I',   'N',   'S',   0x00u, 0x70u, 'W',
    'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '0',
    0x00u, 0x0Au, 0x00u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu,
    0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '1',   0x00u, 0x0Au, 0x01u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',
    'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '1',   0x20u, 'S',
    'P',   '1',   0x00u, 0x0Au, 0x02u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu,
    0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '1',   0x2Eu, 0x31u, 0x00u, 0x0Au, 0x03u, 0x60u, 0x7Du,
    0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',
    '0',   '1',   0x20u, 'S',   'P',   '2',   0x00u, 0x0Au, 0x04u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',
    0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '1',   0x2Eu, 0x31u, 0x20u,
    'S',   'P',   '1',   0x00u, 0x0Au, 0x05u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u,
    0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '6',   0x00u, 0x0Au, 0x06u, 0x60u, 0x7Du, 0x60u,
    'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',
    '6',   0x2Eu, 0x31u, 0x00u, 0x0Au, 0x07u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u,
    0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '6',   0x20u, 'S',   'P',   '1',   0x00u, 0x0Au,
    0x08u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u,
    0x20u, '2',   '0',   '0',   '6',   0x20u, 'S',   'P',   '2',   0x00u, 0x0Au, 0x09u, 0x60u, 0x7Du, 0x60u, 'W',
    'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '9',
    0x00u, 0x0Au, 0x0Au, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu,
    0x77u, 0x73u, 0x20u, '2',   '0',   '1',   '2',   0x00u, 0x0Au, 0x0Bu, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',
    'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '1',   '3',   0x00u, 0x0Au,
    0x0Cu, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u,
    0x20u, '2',   '0',   '1',   '5',   0x00u, 0x0Au, 0x0Du, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du,
    0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '1',   '6',   0x00u, 0x0Au, 0x0Eu, 0x60u,
    0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',
    '0',   '1',   '7',   0x00u, 0x0Au, 0x0Fu, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u,
    0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '1',   '7',   0x2Eu, 0x32u, 0x00u, 0x0Au, 0x10u, 0x60u,
    0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',
    '0',   '1',   '8',   0x00u, 0x0Au, 0x11u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u,
    0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '1',   '8',   0x2Eu, 0x32u, 0x00u, 0x0Au, 0x12u, 0x60u,
    0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',
    '0',   '1',   '9',   0x00u, 0x0Au, 0x13u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u,
    0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '2',   '0',   0x00u, 0x0Au, 0x14u, 0x60u, 0x7Du, 0x60u,
    'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '2',
    '1',   0x00u, 0x0Au, 0x15u, 0x60u, 0x7Du, 0x60u, 'W',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u,
    0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '2',   '2',   0x00u, 0x0Au, 0x16u, 0x60u, 0xA4u, 0x60u, 0x14u, 0x46u,
    0x1Eu, 'O',   'T',   'H',   'R',   0x00u, 0x70u, 'O',   'C',   'H',   'K',   0x0Du, 0x4Cu, 0x69u, 0x6Eu, 0x75u,
    0x78u, 0x00u, 0x0Au, 0x00u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x44u, 0x61u, 0x72u, 0x77u,
    0x69u, 0x6Eu, 0x00u, 0x0Au, 0x01u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x46u, 0x72u, 0x65u,
    0x65u, 'B',   'S',   'D',   0x00u, 0x0Au, 0x02u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 'H',
    'P',   0x2Du, 'U',   'X',   0x00u, 0x0Au, 0x03u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x4Fu,
    0x70u, 0x65u, 0x6Eu, 'V',   'M',   'S',   0x00u, 0x0Au, 0x04u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',
    0x0Du, 0x4Cu, 0x69u, 0x6Eu, 0x75u, 0x78u, 0x2Du, 0x44u, 0x65u, 0x6Cu, 0x6Cu, 0x2Du, 0x56u, 0x69u, 0x64u, 0x65u,
    0x6Fu, 0x00u, 0x0Au, 0x05u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x4Du, 0x6Fu, 0x64u, 0x75u,
    0x6Cu, 0x65u, 0x20u, 0x44u, 0x65u, 0x76u, 0x69u, 0x63u, 0x65u, 0x00u, 0x0Au, 0x06u, 0x60u, 0x7Du, 0x60u, 'O',
    'C',   'H',   'K',   0x0Du, 0x50u, 0x72u, 0x6Fu, 0x63u, 0x65u, 0x73u, 0x73u, 0x6Fu, 0x72u, 0x20u, 0x44u, 0x65u,
    0x76u, 0x69u, 0x63u, 0x65u, 0x00u, 0x0Au, 0x07u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x33u,
    0x2Eu, 0x30u, 0x20u, 0x54u, 0x68u, 0x65u, 0x72u, 0x6Du, 0x61u, 0x6Cu, 0x20u, 0x4Du, 0x6Fu, 0x64u, 0x65u, 0x6Cu,
    0x00u, 0x0Au, 0x08u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x33u, 0x2Eu, 0x30u, 0x20u, '_',
    'S',   'C',   'P',   0x20u, 0x45u, 0x78u, 0x74u, 0x65u, 0x6Eu, 0x73u, 0x69u, 0x6Fu, 0x6Eu, 0x73u, 0x00u, 0x0Au,
    0x09u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x50u, 0x72u, 0x6Fu, 0x63u, 0x65u, 0x73u, 0x73u,
    0x6Fu, 0x72u, 0x20u, 0x41u, 0x67u, 0x67u, 0x72u, 0x65u, 0x67u, 0x61u, 0x74u, 0x6Fu, 0x72u, 0x20u, 0x44u, 0x65u,
    0x76u, 0x69u, 0x63u, 0x65u, 0x00u, 0x0Au, 0x0Au, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x45u,
    0x78u, 0x74u, 0x65u, 0x6Eu, 0x64u, 0x65u, 0x64u, 0x20u, 0x41u, 0x64u, 0x64u, 0x72u, 0x65u, 0x73u, 0x73u, 0x20u,
    0x53u, 0x70u, 0x61u, 0x63u, 0x65u, 0x20u, 0x44u, 0x65u, 0x73u, 0x63u, 0x72u, 0x69u, 0x70u, 0x74u, 0x6Fu, 0x72u,
    0x00u, 0x0Au, 0x0Bu, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x44u, 0x69u, 0x73u, 0x70u, 0x6Cu,
    0x61u, 0x79u, 0x4Du, 0x75u, 0x78u, 0x00u, 0x0Au, 0x0Cu, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du,
    0x00u, 0x0Au, 0x0Du, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu,
    0x77u, 0x73u, 0x20u, '2',   '0',   '1',   '4',   0x00u, 0x0Au, 0x0Eu, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',
    'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '9',   '9',   '9',   '9',   '9',   0x00u,
    0x0Au, 0x0Fu, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x77u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u,
    0x73u, 0x20u, '2',   '0',   '1',   '5',   0x00u, 0x0Au, 0x10u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',
    0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '1',   '5',   0x20u, 0x00u, 0x0Au,
    0x11u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u,
    0x20u, '2',   '0',   '1',   0x00u, 0x0Au, 0x12u, 0x60u, 0x7Du, 0x60u, 'O',   'C',   'H',   'K',   0x0Du, 0x57u,
    0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '2',   '2',   0x20u, 'S',   'P',   '1',   0x00u,
    0x0Au, 0x13u, 0x60u, 0xA4u, 0x60u, 0x14u, 0x40u, 0x05u, 'N',   'E',   'W',   'S',   0x00u, 0x70u, '_',   'O',
    'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '1',   0x00u,
    0x60u, 0x70u, '_',   'O',   'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',
    '0',   '1',   '5',   0x00u, 0x60u, 0x70u, '_',   'O',   'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu,
    0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '9',   0x00u, 0x60u, 0x70u, '_',   'O',   'S',   'I',   0x0Du, 0x4Cu,
    0x69u, 0x6Eu, 0x75u, 0x78u, 0x00u, 0x60u, 0x14u, 0x0Eu, 'B',   'A',   'D',   'A',   0x00u, 0xA4u, '_',   'O',
    'S',   'I',   0x0Bu, 0x34u, 0x12u, 0x70u, '_',   'O',   'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu,
    0x77u, 0x73u, 0x20u, '2',   '0',   '1',   '5',   0x00u, 'T',   'R',   'U',   '_',
};

/**
 * @brief An `\_OSI` ladder as firmware writes it in `\_SB._INI`: behind CondRefOf (\_OSI), each Windows
 *        string asked sets OSYS to its year, "Linux" to 1000 and "FreeBSD" to 1001; without `\_OSI`,
 *        `\_OS` sets 1999.
 */
static const uint8_t identity_ladder_aml[] = {
    0x08u, 'O',   'S',   'Y',   'S',   0x0Au, 0x00u, 0x10u, 0x44u, 0x16u, 0x5Cu, '_',   'S',   'B',   '_',   0x14u,
    0x4Cu, 0x15u, '_',   'I',   'N',   'I',   0x00u, 0x70u, 0x0Bu, 0xD0u, 0x07u, 'O',   'S',   'Y',   'S',   0xA0u,
    0x44u, 0x12u, 0x5Bu, 0x12u, 0x5Cu, '_',   'O',   'S',   'I',   0x00u, 0xA0u, 0x1Cu, 0x5Cu, '_',   'O',   'S',
    'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '1',   0x00u, 0x70u,
    0x0Bu, 0xD1u, 0x07u, 'O',   'S',   'Y',   'S',   0xA0u, 0x20u, 0x5Cu, '_',   'O',   'S',   'I',   0x0Du, 0x57u,
    0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '1',   0x20u, 'S',   'P',   '1',   0x00u,
    0x70u, 0x0Bu, 0xD2u, 0x07u, 'O',   'S',   'Y',   'S',   0xA0u, 0x1Cu, 0x5Cu, '_',   'O',   'S',   'I',   0x0Du,
    0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '6',   0x00u, 0x70u, 0x0Bu, 0xD6u,
    0x07u, 'O',   'S',   'Y',   'S',   0xA0u, 0x1Cu, 0x5Cu, '_',   'O',   'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu,
    0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '9',   0x00u, 0x70u, 0x0Bu, 0xD9u, 0x07u, 'O',   'S',
    'Y',   'S',   0xA0u, 0x1Cu, 0x5Cu, '_',   'O',   'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u,
    0x73u, 0x20u, '2',   '0',   '1',   '2',   0x00u, 0x70u, 0x0Bu, 0xDCu, 0x07u, 'O',   'S',   'Y',   'S',   0xA0u,
    0x1Cu, 0x5Cu, '_',   'O',   'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',
    '0',   '1',   '3',   0x00u, 0x70u, 0x0Bu, 0xDDu, 0x07u, 'O',   'S',   'Y',   'S',   0xA0u, 0x1Cu, 0x5Cu, '_',
    'O',   'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '1',   '5',
    0x00u, 0x70u, 0x0Bu, 0xDFu, 0x07u, 'O',   'S',   'Y',   'S',   0xA0u, 0x1Cu, 0x5Cu, '_',   'O',   'S',   'I',
    0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '2',   '2',   0x00u, 0x70u, 0x0Bu,
    0xE6u, 0x07u, 'O',   'S',   'Y',   'S',   0xA0u, 0x15u, 0x5Cu, '_',   'O',   'S',   'I',   0x0Du, 0x4Cu, 0x69u,
    0x6Eu, 0x75u, 0x78u, 0x00u, 0x70u, 0x0Bu, 0xE8u, 0x03u, 'O',   'S',   'Y',   'S',   0xA0u, 0x17u, 0x5Cu, '_',
    'O',   'S',   'I',   0x0Du, 0x46u, 0x72u, 0x65u, 0x65u, 'B',   'S',   'D',   0x00u, 0x70u, 0x0Bu, 0xE9u, 0x03u,
    'O',   'S',   'Y',   'S',   0xA1u, 0x27u, 0xA0u, 0x25u, 0x93u, 0x5Cu, '_',   'O',   'S',   '_',   0x0Du, 0x4Du,
    0x69u, 0x63u, 0x72u, 0x6Fu, 0x73u, 0x6Fu, 0x66u, 0x74u, 0x20u, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u,
    0x20u, 'N',   'T',   0x00u, 0x70u, 0x0Bu, 0xCFu, 0x07u, 'O',   'S',   'Y',   'S',
};

/**
 * @brief SystemIO fields over the PIC (0x20), the RTC and its neighbours (0x6E to 0x73, by byte and as
 *        a double word from 0x6E) and the last DMA port (a word from 0x0F); methods read and write them,
 *        and ask `\_OSI` for "Windows 2000" (A200) and "Windows 2001" (A201).
 */
static const uint8_t identity_ports_aml[] = {
    0x5Bu, 0x80u, 'P',   'I',   'C',   '_',   0x01u, 0x0Au, 0x20u, 0x0Au, 0x02u, 0x5Bu, 0x81u, 0x0Bu, 'P',   'I',
    'C',   '_',   0x01u, 'P',   '0',   '2',   '0',   0x08u, 0x5Bu, 0x80u, 'R',   'T',   'C',   '_',   0x01u, 0x0Au,
    0x6Eu, 0x0Au, 0x06u, 0x5Bu, 0x81u, 0x1Fu, 'R',   'T',   'C',   '_',   0x01u, 'P',   '0',   '6',   'E',   0x08u,
    'P',   '0',   '6',   'F',   0x08u, 'P',   '0',   '7',   '0',   0x08u, 'P',   '0',   '7',   '1',   0x08u, 'P',
    '0',   '7',   '2',   0x08u, 0x5Bu, 0x81u, 0x0Bu, 'R',   'T',   'C',   '_',   0x03u, 'D',   '0',   '6',   'E',
    0x20u, 0x5Bu, 0x80u, 'D',   'M',   'A',   '_',   0x01u, 0x0Au, 0x0Fu, 0x0Au, 0x02u, 0x5Bu, 0x81u, 0x0Bu, 'D',
    'M',   'A',   '_',   0x02u, 'W',   '0',   '0',   'F',   0x10u, 0x14u, 0x0Bu, 'R',   'T',   'C',   'R',   0x00u,
    0xA4u, 'P',   '0',   '7',   '0',   0x14u, 0x0Du, 'W',   'R',   'T',   'C',   0x00u, 0x70u, 0x0Au, '3',   'P',
    '0',   '7',   '0',   0x14u, 0x15u, 'N',   'E',   'I',   'G',   0x00u, 0xA4u, 0x7Du, 'P',   '0',   '7',   '2',
    0x79u, 'P',   '0',   '6',   'F',   0x0Au, 0x08u, 0x00u, 0x00u, 0x14u, 0x0Bu, 'D',   'W',   'R',   'D',   0x00u,
    0xA4u, 'D',   '0',   '6',   'E',   0x14u, 0x0Bu, 'D',   'M',   'A',   'W',   0x00u, 0xA4u, 'W',   '0',   '0',
    'F',   0x14u, 0x0Bu, 'P',   'I',   'C',   'R',   0x00u, 0xA4u, 'P',   '0',   '2',   '0',   0x14u, 0x19u, 'A',
    '2',   '0',   '0',   0x00u, 0xA4u, '_',   'O',   'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u,
    0x73u, 0x20u, '2',   '0',   '0',   '0',   0x00u, 0x14u, 0x19u, 'A',   '2',   '0',   '1',   0x00u, 0xA4u, '_',
    'O',   'S',   'I',   0x0Du, 0x57u, 0x69u, 0x6Eu, 0x64u, 0x6Fu, 0x77u, 0x73u, 0x20u, '2',   '0',   '0',   '1',
    0x00u,
};

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
