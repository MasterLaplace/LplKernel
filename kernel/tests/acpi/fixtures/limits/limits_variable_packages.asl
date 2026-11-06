// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief FULL holds the size of a Package of 4,096 elements, the most a VarPackage holds (§6.1); OVER, the size of
 *        one of 4,097.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (FULL, 5)
    Name (OVER, 5)
    Method (MFUL)
    {
        Local0 = Package (0x1000) {}
        Return (SizeOf (Local0))
    }
    Method (MOVR)
    {
        Local0 = Package (0x1001) {}
        Return (SizeOf (Local0))
    }
    FULL = MFUL ()
    OVER = MOVR ()
}
