// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Match over a Package of Integers, a String and a Buffer, with pairs of tests, each result stored to a Name.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (PKG0, Package (0x06)
    {
        0x01,
        0x05,
        "X",
        0x09,
        0x05,
        Buffer (0x01)
        {
             0x07
        }
    })
    Name (R000, 0x00)
    Name (R001, 0x00)
    Name (R002, 0x00)
    Name (R003, 0x00)
    Name (R004, 0x00)
    Name (R005, 0x00)
    R000 = Match (PKG0, MEQ, 0x05, MTR, 0x00, 0x00)
    R001 = Match (PKG0, MEQ, 0x05, MTR, 0x00, 0x02)
    R002 = Match (PKG0, MGT, 0x04, MLT, 0x09, 0x00)
    R003 = Match (PKG0, MGE, 0x09, MTR, 0x00, 0x00)
    R004 = Match (PKG0, MEQ, 0x08, MTR, 0x00, 0x00)
    R005 = Match (PKG0, MEQ, 0x07, MTR, 0x00, 0x00)
}
