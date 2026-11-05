// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Stores that would close a cycle: a package element of Local0 given RefOf (Local0), packages that refer to
 *        each other through their elements, PKG0 to PKG2, and results kept in R000 and R001.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (PKG0, Package (0x02)
    {
        0x00,
        0x00
    })
    Name (PKG1, Package (0x02)
    {
        0x00,
        0x00
    })
    Name (PKG2, Package (0x02)
    {
        0x00,
        0x00
    })
    Name (R000, 0x07)
    Name (R001, 0x07)
    Method (MLOC, 0, NotSerialized)
    {
        Local0 = Package (0x01)
            {
                0x00
            }
        Local0 [0x00] = RefOf (Local0)
        Return (0x01)
    }

    Method (MSLF, 0, NotSerialized)
    {
        Local0 = 0x01
        Local0 = RefOf (Local0) __EXPECT__(3144)
        Return (0x01)
    }

    PKG0 [0x00] = RefOf (PKG0)
    PKG1 [0x00] = RefOf (PKG0)
    PKG0 [0x01] = RefOf (PKG1)
    PKG2 [0x00] = RefOf (PKG1)
    PKG2 [0x01] = RefOf (PKG1)
    R000 = MLOC ()
    R001 = MSLF ()
}
