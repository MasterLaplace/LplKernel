// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Comparisons of Strings and of Buffers, mixed comparisons of a String and a Buffer, and LAnd and LOr of a
 *        Buffer, each stored to a Name.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (R000, 0x07)
    Name (R001, 0x07)
    Name (R002, 0x07)
    Name (R003, 0x07)
    Name (R004, 0x07)
    Name (R005, 0x07)
    Name (R006, 0x07)
    Name (R007, 0x07)
    Name (R008, 0x07)
    Name (R009, 0x07)
    Name (R010, 0x07)
    Name (R011, 0x07)
    Name (R012, 0x07)
    Name (R013, 0x07)
    R000 = ("ABC" == "ABC")
    R001 = ("ABC" == "ABD")
    R002 = ("ABC" < "ABD")
    R003 = ("ABCD" > "ABC")
    R004 = (Buffer (0x02)
            {
                 0x01, 0x02
            } == Buffer (0x02)
            {
                 0x01, 0x02
            })
    R005 = (Buffer (0x02)
            {
                 0x01, 0x02
            } < Buffer (0x02)
            {
                 0x01, 0x03
            })
    R006 = (0x1F == "1F")
    R007 = ("1F" == 0x1F)
    R008 = ("000000000000001F" == 0x1F)
    R009 = (Buffer (0x05)
            {
                 0x00, 0x00, 0x00, 0x00, 0x01
            } && 0x01)
    R010 = (Buffer (0x05)
            {
                 0x00, 0x00, 0x00, 0x00, 0x01
            } || 0x00)
    R011 = !"0"
    R012 = ("AB" < "ABC")
    R013 = ("AB" == "ABC")
}
