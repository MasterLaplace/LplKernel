// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Statements the declaration pass's decoder takes (O19), as iasl compiles them: `Name (N0__, One)`,
 *        `Name (N1__, Package (2) {1, "a"})`, `Method (M0__, 0) {Return (One)}`,
 *        `Method (M1__, 7, Serialized, 15) {Return (Arg6)}`, `OperationRegion (R0__, SystemMemory, 0x100000, 0x20)`,
 *        `Field (R0__, ByteAcc, NoLock, Preserve) {F0__, 8, , 4, F1__, 4, Offset (4), F2__, 32}`,
 *        `Field (R0__, AnyAcc, Lock, WriteAsZeros) {AccessAs (DWordAcc), F3__, 32}`, `Device (D0__)` holding
 *        `Name (_HID, "LPL0001")`, `Method (_STA) {Return (0x0F)}`, `Device (D1__) {Name (_ADR, Zero)}`,
 *        `ThermalZone (TZ1_) {}` and `Scope (^D0__) {Name (S2__, 6)}`, then `Scope (D0__) {Name (S0__, 2)}`,
 *        `Scope (\_SB) {Device (D2__) {}}`, `Scope (\D0__.D1__) {Name (S1__, 3)}`,
 *        `ThermalZone (TZ0_) {Method (_TMP) {Return (3000)}}`, `Device (\_SB.D3__) {}`, `Name (\_SB.N2__, 4)`,
 *        `Method (\_SB.M2__) {}` and `Scope (\) {Name (RT__, 5)}`: 26 of them through the decoder, the Name and the
 *        Method of a path through the walk.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (N0, One)
    Name (N1, Package (0x02)
    {
        One,
        "a"
    })
    Method (M0, 0, NotSerialized)
    {
        Return (One)
    }

    Method (M1, 7, Serialized, 15)
    {
        Return (Arg6)
    }

    OperationRegion (R0, SystemMemory, 0x00100000, 0x20)
    Field (R0, ByteAcc, NoLock, Preserve)
    {
        F0,     8,
            ,   4,
        F1,     4,
        Offset (0x04),
        F2,     32
    }

    Field (R0, AnyAcc, Lock, WriteAsZeros)
    {
        AccessAs (DWordAcc, 0x00),
        F3,     32
    }

    Device (D0)
    {
        Name (_HID, "LPL0001")
        Method (_STA, 0, NotSerialized)
        {
            Return (0x0F)
        }

        Device (D1)
        {
            Name (_ADR, Zero)
        }

        ThermalZone (TZ1)
        {
        }

        Scope (^D0)
        {
            Name (S2, 0x06)
        }
    }

    Scope (D0)
    {
        Name (S0, 0x02)
    }

    Scope (_SB)
    {
        Device (D2) __EXPECT__(3141)
        {
        }
    }

    Scope (D0.D1)
    {
        Name (S1, 0x03)
    }

    ThermalZone (TZ0)
    {
        Method (_TMP, 0, NotSerialized)
        {
            Return (0x0BB8)
        }
    }

    Device (_SB.D3) __EXPECT__(3141)
    {
    }

    Name (_SB.N2, 0x04)
    Method (_SB.M2, 0, NotSerialized)
    {
    }

    Scope (\)
    {
        Name (RT, 0x05)
    }
}
