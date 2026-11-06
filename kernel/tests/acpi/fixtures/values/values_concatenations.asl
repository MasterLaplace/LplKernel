// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Concatenate of two Integers, of a String with an Integer and with a Buffer, and of a Buffer with a String
 *        and with an Integer, each stored to a Name.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (R000, 0x00)
    Name (R001, 0x00)
    Name (R002, 0x00)
    Name (R003, 0x00)
    Name (R004, 0x00)
    Name (R005, 0x00)
    Name (R006, 0x00)
    CopyObject (Concatenate (0x0102, 0x0304), R000)
    CopyObject (Concatenate ("AB", "CD"), R001)
    CopyObject (Concatenate ("AB", 0x1F), R002)
    CopyObject (Concatenate (Buffer (0x01)
            {
                 0x01
            }, "AB"), R003)
    CopyObject (Concatenate (Buffer (0x01)
            {
                 0x01
            }, 0x0102), R004)
    CopyObject (Concatenate ("AB", Buffer (0x02)
            {
                 0x01, 0xAB
            }), R005)
    Concatenate ("X", "Y", R006)
}
