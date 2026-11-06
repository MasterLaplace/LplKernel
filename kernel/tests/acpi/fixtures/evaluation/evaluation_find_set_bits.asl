// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief R00_ to R12_ take FindSetLeftBit and FindSetRightBit, with a target and without, of 0x10, 0x0300, Zero,
 *        Ones, 0x80000000, and One shifted left by 63, which a table of 32-bit integers makes Zero.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (R00, 0x0F)
    Name (R01, 0x0F)
    Name (R02, 0x0F)
    Name (R03, 0x0F)
    Name (R04, 0x0F)
    Name (R05, 0x0F)
    Name (R06, 0x0F)
    Name (R07, 0x0F)
    Name (R08, 0x0F)
    Name (R09, 0x0F)
    Name (R10, 0x0F)
    Name (R11, 0x0F)
    Name (R12, 0x0F)
    R00 = FindSetLeftBit (0x10)
    R01 = FindSetRightBit (0x10)
    FindSetLeftBit (0x0300, R02)
    R03 = FindSetRightBit (0x0300, R04)
    R05 = FindSetLeftBit (Zero)
    R06 = FindSetRightBit (Zero)
    R07 = FindSetLeftBit (Ones)
    R08 = FindSetRightBit (Ones)
    R09 = FindSetLeftBit (0x80000000)
    R10 = FindSetRightBit (0x80000000)
    R11 = FindSetLeftBit ((One << 0x3F))
    R12 = FindSetRightBit ((One << 0x3F))
}
