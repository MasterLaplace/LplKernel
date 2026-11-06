// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A DSDT that writes One to five bytes or words whose addresses the test writes, each through a
 *        SystemMemory region of its own and a method: BEFO by WBEF, FRST by WFRS, LAST by WLST and AFTE by WAFT,
 *        bytes, and DWRD by WDWD, a DWordAcc unit of 32 bits.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (BEFR, SystemMemory, 0x12345678, One)
    Field (BEFR, ByteAcc, NoLock, Preserve)
    {
        BEFO,   8
    }

    OperationRegion (FRSR, SystemMemory, 0x12345678, One)
    Field (FRSR, ByteAcc, NoLock, Preserve)
    {
        FRST,   8
    }

    OperationRegion (LSTR, SystemMemory, 0x12345678, One)
    Field (LSTR, ByteAcc, NoLock, Preserve)
    {
        LAST,   8
    }

    OperationRegion (AFTR, SystemMemory, 0x12345678, One)
    Field (AFTR, ByteAcc, NoLock, Preserve)
    {
        AFTE,   8
    }

    OperationRegion (DWRR, SystemMemory, 0x12345678, 0x04)
    Field (DWRR, DWordAcc, NoLock, Preserve)
    {
        DWRD,   32
    }

    Method (WBEF, 0, NotSerialized)
    {
        BEFO = One
    }

    Method (WFRS, 0, NotSerialized)
    {
        FRST = One
    }

    Method (WLST, 0, NotSerialized)
    {
        LAST = One
    }

    Method (WAFT, 0, NotSerialized)
    {
        AFTE = One
    }

    Method (WDWD, 0, NotSerialized)
    {
        DWRD = One
    }
}
