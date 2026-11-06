// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief FLD0, a field of an EmbeddedControl region, is the predicate of an If that declares EEEE, its Else FFFF;
 *        then RES_, 5, takes FLD0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (REG0, EmbeddedControl, Zero, 0x10)
    Field (REG0, ByteAcc, NoLock, Preserve)
    {
        FLD0,   8
    }

    If (FLD0)
    {
        Name (EEEE, One)
    }
    Else
    {
        Name (FFFF, One)
    }

    Name (RES, 0x05)
    RES = FLD0
}
