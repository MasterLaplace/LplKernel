// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Timer where an operator takes its values: an Add at table level, an Add in M001, the predicate of an If in
 *        M002, the count of a VarPackage in M003, and after a BankField in M004, whose bank value is a Timer. SEEN
 *        counts the methods that get past their Timer, RES1 to RES5 take the results, and AFTR comes last.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (M001, 0, NotSerialized)
    {
        Store (Add (One, Timer), Local0)
        Increment (SEEN)
        Return (Local0)
    }

    Method (M002, 0, NotSerialized)
    {
        If (Timer)
        {
            Timer
        } __EXPECT__(6114)
        Else
        {
            Timer
        } __EXPECT__(6114)

        Return (Zero)
    }

    Method (M003, 0, NotSerialized)
    {
        Store (Package (Timer) { One }, Local0) __EXPECT__(3144)
        Increment (SEEN)
        Return (Zero)
    }

    Method (M004, 0, NotSerialized)
    {
        BankField (REG0, BNK0, Timer, ByteAcc, NoLock, Preserve)
        {
            FLD4,   8
        }

        Timer
        Increment (SEEN) __EXPECT__(6114)
        Return (Zero)
    }

    OperationRegion (REG0, SystemMemory, 0x00040000, 0x10)
    Field (REG0, ByteAcc, NoLock, Preserve)
    {
        BNK0,   8
    }

    Name (RES1, 0x05)
    Name (RES2, 0x05)
    Name (RES3, 0x05)
    Name (RES4, 0x05)
    Name (RES5, 0x05)
    Name (SEEN, Zero)
    Store (Add (One, Timer), RES1)
    Store (Add (0x07, M001 ()), RES2)
    Store (Add (0x07, M002 ()), RES3)
    Store (Add (0x07, M003 ()), RES4)
    Store (Add (0x07, M004 ()), RES5)
    Name (AFTR, One)
}
