// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Named Integers, Strings, Buffers, a Package and a Device, and methods that store Strings, Buffers, Integers
 *        and Packages to them.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (INT0, 0x1234)
    Name (INT1, 0x00)
    Name (STR0, "ABC")
    Name (STR1, "ABCDE")
    Name (BUF0, Buffer (0x04)
    {
         0x09, 0x09, 0x09, 0x09
    })
    Name (BUF1, Buffer (0x01){})
    Name (PKG0, Package (0x01)
    {
        0x01
    })
    Name (PKG1, Package (0x01)
    {
        0x01
    })
    Device (DEV0) __EXPECT__(3141)
    {
    }

    Name (INT2, 0x00)
    INT0 = "ABCDEFGHIJ"
    INT1 = Buffer (0x02)
        {
             0x01, 0x02
        }
    STR0 = "HELLO WORLD"
    STR1 = 0x41
    BUF0 = "AB"
    BUF1 = 0x0102
    PKG0 = Package (0x02)
        {
            0x02,
            "XY"
        }
    PKG1 = 0x05 __EXPECT__(6058)
    DEV0 = 0x01 __EXPECT__(6058)
    Debug = INT2 = 0x33
}
