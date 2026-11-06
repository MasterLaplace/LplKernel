// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Names whose kept data objects (O20) methods reach, as iasl compiles them: `Name (BUF0, Buffer (4) {1, 2})`,
 *        `Name (PKG0, Package (3) {1, 2, 3})`, `Name (PKG1, Package (2) {0x11, 0x22})`, `Name (PKGA, Package (1)
 *        {PKG1})`, `Name (PKG2, Package (1) {0x33})`, `Name (PKG3, Package (2) {4, 5})`, `Name (BUF1, Buffer (2) {6})`;
 *        then `M0__` stores 0x1234 to BUF0 and returns its byte 1, `M1__` stores RefOf (PKG0) to Local0 and returns
 *        element 2 of DerefOf (Local0), `M2__` returns element 1 of what element 0 of PKGA names, `M3__` returns
 *        element 0 of DerefOf ("\\PKG2"), `M4__` stores 7 to element 0 of PKG3 and returns it, `M5__` returns element
 *        0 of PKG3, and `M6__` returns SizeOf (BUF1).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (BUF0, Buffer (0x04)
    {
         0x01, 0x02
    })
    Name (PKG0, Package (0x03)
    {
        One,
        0x02,
        0x03
    })
    Name (PKG1, Package (0x02)
    {
        0x11,
        0x22
    })
    Name (PKGA, Package (0x01)
    {
        PKG1,
    })
    Name (PKG2, Package (0x01)
    {
        0x33
    })
    Name (PKG3, Package (0x02)
    {
        0x04,
        0x05
    })
    Name (BUF1, Buffer (0x02)
    {
         0x06
    })
    Method (M0, 0, NotSerialized)
    {
        BUF0 = 0x1234
        Return (DerefOf (BUF0 [One]))
    }

    Method (M1, 0, NotSerialized)
    {
        Local0 = RefOf (PKG0)
        Local1 = DerefOf (Local0)
        Return (DerefOf (Local1 [0x02]))
    }

    Method (M2, 0, NotSerialized)
    {
        Local0 = DerefOf (PKGA [Zero])
        Return (DerefOf (Local0 [One]))
    }

    Method (M3, 0, NotSerialized)
    {
        Local1 = "\\PKG2"
        Local0 = DerefOf (Local1)
        Return (DerefOf (Local0 [Zero]))
    }

    Method (M4, 0, NotSerialized)
    {
        PKG3 [Zero] = 0x07
        Return (DerefOf (PKG3 [Zero]))
    }

    Method (M5, 0, NotSerialized)
    {
        Return (DerefOf (PKG3 [Zero]))
    }

    Method (M6, 0, NotSerialized)
    {
        Return (SizeOf (BUF1))
    }
}
