// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A DSDT whose LDTB runs `LoadTable ("TSTA", "", "", "", "", Zero); OTHR = One; Return (Zero)`, OTHR a byte
 *        of a SystemMemory region whose address the test writes.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (TSTR, SystemMemory, 0x12345678, One)
    Field (TSTR, ByteAcc, NoLock, Preserve)
    {
        OTHR,   8
    }

    Method (LDTB, 0, NotSerialized)
    {
        LoadTable ("TSTA", "", "", "", "", Zero)
        OTHR = One
        Return (Zero)
    }
}
