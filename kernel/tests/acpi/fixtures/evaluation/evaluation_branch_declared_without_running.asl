// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief If FLD0, a field of an EmbeddedControl region, stores 0x2A to RES_, 5 before, and declares NEW_.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (REG0, EmbeddedControl, Zero, 0x10)
    Field (REG0, ByteAcc, NoLock, Preserve)
    {
        FLD0,   8
    }

    Name (RES, 0x05)
    If (FLD0)
    {
        RES = 0x2A
        Name (NEW, One)
    }
}
