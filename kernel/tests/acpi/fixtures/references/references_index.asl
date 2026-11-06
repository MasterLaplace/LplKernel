// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Index into Packages, Buffers and Strings: stores and Increment through it, an element that is a name, an
 *        index past the end, a store through an argument that holds an Index, and DerefOf (Index (...)) with and
 *        without a target.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (PKG0, Package (0x03)
    {
        0x01,
        0x02,
        0x03
    })
    Name (BUF0, Buffer (0x03)
    {
         0x01, 0x02, 0x03
    })
    Name (STR0, "ABC")
    Name (NEST, Package (0x02)
    {
        0x00,
        Package (0x02)
        {
            0x01,
            0x02
        }
    })
    Name (NAMD, Package (0x01)
    {
        INT1,
    })
    Name (INT1, 0x55)
    Name (R000, 0x07)
    Name (R001, 0x07)
    Name (R002, 0x07)
    Name (R003, 0x07)
    Name (R004, 0x07)
    Name (R005, 0x07)
    Name (R006, 0x07)
    Name (R007, 0x07)
    Method (MIDX, 0, NotSerialized)
    {
        Index (PKG0, 0x02, Local0)
        Store (0x09, Local0) __EXPECT__(3144)
        Return (DerefOf (Index (PKG0, 0x02)))
    }

    Method (MINC, 0, NotSerialized)
    {
        Increment (Index (BUF0, 0x01))
        Return (DerefOf (Index (BUF0, 0x01)))
    }

    Method (MPUT, 1, NotSerialized)
    {
        Store ("STR", Arg0)
    }

    Method (MOVR, 0, NotSerialized)
    {
        MPUT (Index (PKG0, 0x00))
        Return (ObjectType (DerefOf (Index (PKG0, 0x00))))
    }

    Store (0x05, Index (PKG0, 0x01))
    Store (0x01FF, Index (BUF0, 0x00))
    Store (0x58, Index (STR0, 0x01))
    Store (DerefOf (Index (PKG0, 0x01)), R000)
    Store (DerefOf (Index (BUF0, 0x00)), R001)
    Store (MIDX (), R002)
    Store (MINC (), R003)
    Store (0x77, Index (DerefOf (Index (NEST, 0x01)), 0x00))
    Store (DerefOf (Index (DerefOf (Index (NEST, 0x01)), 0x00)), R004)
    Store (DerefOf (Index (NAMD, 0x00)), R005)
    Store (Index (PKG0, 0x03), R006)
    Store (MOVR (), R007)
}
