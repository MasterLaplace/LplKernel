// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief WRTE makes ALS2 refer to an element of PKG0; CopyObject ("ab", ALS2) replaces ALS2, and R000 reads the
 *        element.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (PKG0, Package (0x03)
    {
        One,
        0x02,
        0x03
    })
    Name (ALS2, Zero)
    Name (R000, 0x07)
    Method (WRTE, 2, NotSerialized)
    {
        Arg1 = Arg0
    }

    WRTE (PKG0 [One], RefOf (ALS2))
    CopyObject ("ab", ALS2)
    R000 = DerefOf (PKG0 [One])
}
