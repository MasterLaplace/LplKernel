// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The methods that read `PKG_` and `AFTR` once a table of shortcuts_unbuilt_cases has loaded, before both, so
 *        that a fault that abandons the rest of the table cannot remove them: `MOT_` returns `ObjectType (PKG_)`, then
 *        come `MSZ_` and `SizeOf (PKG_)`, `MEL_` and `DerefOf (Index (PKG_, Zero))`, `MCR_` and `CondRefOf (PKG_)`,
 *        and `MAF_`, which returns `AFTR`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (MOT, 0, NotSerialized)
    {
        Return (ObjectType (PKG)) __EXPECT__(6084)
    }

    Method (MSZ, 0, NotSerialized)
    {
        Return (SizeOf (PKG)) __EXPECT__(6084)
    }

    Method (MEL, 0, NotSerialized)
    {
        Return (DerefOf (PKG [Zero])) __EXPECT__(6084)
    }

    Method (MCR, 0, NotSerialized)
    {
        Return (CondRefOf (PKG))
    }

    Method (MAF, 0, NotSerialized)
    {
        Return (AFTR) __EXPECT__(6084)
    }
}
