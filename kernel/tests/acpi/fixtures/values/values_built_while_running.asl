// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Methods that build a String literal, a Buffer of an argument's size longer or shorter than its initialiser,
 *        a VarPackage of an argument's size padded or cut short, and a Package that nests a Package and keeps a
 *        name, each kept by a Name.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (STR0, 0x00)
    Name (BUF0, 0x00)
    Name (BUF1, 0x00)
    Name (VPK0, 0x00)
    Name (VPK1, 0x00)
    Name (PKG0, 0x00)
    Method (MSTR, 0, NotSerialized)
    {
        Return ("hello world")
    }

    Method (MBUF, 1, NotSerialized)
    {
        Return (Buffer (Arg0)
        {
             0x01, 0x02
        })
    }

    Method (MVPK, 1, NotSerialized)
    {
        Return (Package (Arg0)
        {
            0x11,
            Buffer (Arg0)
            {
                 0xAA
            },

            "ab"
        })
    }

    Method (MPKG, 0, NotSerialized)
    {
        Return (Package (0x02)
        {
            0x22,
            Package (0x02)
            {
                "cd",
                STR0,
            }
        })
    }

    CopyObject (MSTR (), STR0)
    CopyObject (MBUF (0x04), BUF0)
    CopyObject (MBUF (0x01), BUF1)
    CopyObject (MVPK (0x05), VPK0)
    CopyObject (MVPK (0x01), VPK1)
    CopyObject (MPKG (), PKG0)
}
