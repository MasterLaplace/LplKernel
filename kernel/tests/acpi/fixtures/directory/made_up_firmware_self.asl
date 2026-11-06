// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A DSDT whose method rewrites its own bytes: the field BND_ over the bound of SELF's While and the
 *        field OTHR over a byte of TSTA, which the directory lists and the load does not read, each a byte of
 *        a SystemMemory region whose address the test writes, at MADE_UP_FIRMWARE_SELF_ADDRESS and
 *        MADE_UP_FIRMWARE_OTHER_ADDRESS; then SELF: `OTHR = One; Local0 = 0; While (Local0 < 0x10)
 *        { Local0++; If (Local0 == 2) { BND_ = 4 } }; Return (Local0)`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (TBLB, SystemMemory, 0x12345678, 0x01)
    Field (TBLB, ByteAcc, NoLock, Preserve)
    {
        BND,    8
    }

    OperationRegion (TSTR, SystemMemory, 0x12345678, 0x01)
    Field (TSTR, ByteAcc, NoLock, Preserve)
    {
        OTHR,   8
    }

    Method (SELF, 0, NotSerialized)
    {
        OTHR = One
        Local0 = Zero
        While ((Local0 < 0x10))
        {
            Local0++
            If ((Local0 == 0x02))
            {
                BND = 0x04
            }
        }

        Return (Local0)
    }
}
