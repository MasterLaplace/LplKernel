// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief R00_ to R13_ take And, Or, Xor, Not with and without a target, LAnd, LOr, LNot, Decrement, ShiftRight,
 *        Divide's remainder and quotient, Mod, and a Subtract that wraps, of constants.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (R00, Zero)
    Name (R01, Zero)
    Name (R02, Zero)
    Name (R03, Zero)
    Name (R04, Zero)
    Name (R05, Zero)
    Name (R06, Zero)
    Name (R07, Zero)
    Name (R08, 0x05)
    Name (R09, Zero)
    Name (R10, Zero)
    Name (R11, Zero)
    Name (R12, Zero)
    Name (R13, Zero)
    Store (And (0x0F0F, 0xFF), R00)
    Store (Or (0x0F00, 0xF0), R01)
    Store (XOr (0x0FF0, 0xFF), R02)
    Not (Zero, R03)
    Store (Not (0xF0F0), R04)
    Store (LAnd (One, Zero), R05)
    Store (LOr (Zero, One), R06)
    Store (LNot (0x05), R07)
    Decrement (R08)
    Store (ShiftRight (0x80, 0x03), R09)
    Divide (0x11, 0x05, R10, R11)
    Store (Mod (0x11, 0x05), R12)
    Store (Subtract (0x05, 0x07), R13)
}
