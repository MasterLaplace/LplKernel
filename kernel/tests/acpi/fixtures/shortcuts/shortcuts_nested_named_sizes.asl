// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Sizes a name gives inside kept packages, read when a method reads them: `Name (CNT_, 4)`,
 *        `Name (PNS_, Package (1) {Buffer (CNT_) {}})`, `Name (PLT_, Package (1) {Buffer (LATE) {}})` before
 *        `Name (LATE, 2)`, `Name (BIG_, 0x1001)` and `Name (PVC_, Package (1) {VarPackage (BIG_) {}})`, one
 *        element past the 4,096 of §6.1, then table-level code that stores 8 to CNT_; the method MNS_ returns the
 *        SizeOf of PNS_'s Buffer, and MLT_ that of PLT_'s.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (CNT, 0x04)
    Name (PNS, Package (0x01)
    {
        Buffer (CNT){}
    })
    Name (PLT, Package (0x01)
    {
        Buffer (LATE){} __EXPECT__(6142)
    })
    Name (LATE, 0x02)
    Name (BIG, 0x1001)
    Name (PVC, Package (0x01)
    {
        Package (BIG) {}
    })
    CNT = 0x08
    Method (MNS_, 0, NotSerialized)
    {
        Return (SizeOf (DerefOf (PNS [Zero])))
    }

    Method (MLT_, 0, NotSerialized)
    {
        Return (SizeOf (DerefOf (PLT [Zero])))
    }
}
