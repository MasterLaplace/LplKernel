// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A DSDT that keeps a Package in its table and rewrites it: `Name (PKG0, Package () {0x11, 0x22})`, kept unbuilt
 *        (O20), BYT0, the byte of a SystemMemory region over that Package's first element, whose address the test
 *        writes at MADE_UP_FIRMWARE_KEPT_ADDRESS, then KEPT: `BYT0 = 0x99; Return (DerefOf (PKG0 [0]))`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (PKG0, Package (0x02)
    {
        0x11,
        0x22
    })
    OperationRegion (TREG, SystemMemory, 0x12345678, 0x01)
    Field (TREG, ByteAcc, NoLock, Preserve)
    {
        BYT0,   8
    }

    Method (KEPT, 0, NotSerialized)
    {
        BYT0 = 0x99
        Return (DerefOf (PKG0 [Zero]))
    }
}
