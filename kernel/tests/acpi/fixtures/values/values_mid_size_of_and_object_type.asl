// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Mid of Strings, Buffers and an Integer, SizeOf of a String, a Buffer, a Package and an Integer, ObjectType
 *        of every kind of object and of an uninitialized local, and a Name in a method that holds a Buffer whose size
 *        an argument gives.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (INT0, 0x01)
    Name (STR0, "ABCD")
    Name (BUF0, Buffer (0x06){})
    Name (PKG0, Package (0x03){})
    Device (DEV0)
    {
        Name (_HID, "LPL0000")
    }

    Mutex (MTX0, 0x00)
    OperationRegion (REG0, SystemMemory, 0x1000, 0x10)
    Field (REG0, ByteAcc, NoLock, Preserve)
    {
        FLD0,   8
    }

    Method (MTHD, 0, NotSerialized)
    {
        Return (0x00)
    }

    Method (MLOC, 0, NotSerialized)
    {
        Local0 = Package (0x05){}
        Return (SizeOf (Local0))
    }

    Method (MUNI, 0, NotSerialized)
    {
        Return (ObjectType (Local1))
    }

    Method (MNAM, 1, NotSerialized)
    {
        Name (TMP0, Buffer (Arg0){})
        Return (SizeOf (TMP0))
    }

    Name (M000, 0x00)
    Name (M001, 0x00)
    Name (M002, 0x00)
    Name (M003, 0x00)
    Name (M004, 0x00)
    Name (M005, 0x00)
    Name (Z000, 0x07)
    Name (Z001, 0x00)
    Name (Z002, 0x00)
    Name (Z003, 0x00)
    Name (Z004, 0x00)
    Name (Z005, 0x00)
    Name (Z006, 0x00)
    Name (Z007, 0x00)
    Name (Z008, 0x00)
    Name (Z009, 0x00)
    Name (Z010, 0x00)
    Name (Z011, 0x00)
    Name (Z012, 0x07)
    Name (Z013, 0x07)
    Name (Z014, 0x07)
    Name (Z015, 0x07)
    CopyObject (Mid ("ABCDEF", 0x02, 0x03), M000)
    CopyObject (Mid ("ABC", 0x01, 0x0A), M001)
    CopyObject (Mid (Buffer (0x03)
            {
                 0x01, 0x02, 0x03
            }, 0x05, 0x01), M002)
    CopyObject (Mid ("ABC", 0x00, 0x00), M003)
    CopyObject (Mid (Buffer (0x04)
            {
                 0x01, 0x02, 0x03, 0x04
            }, 0x01, 0x02), M004)
    CopyObject (Mid (0x4142, 0x00, 0x01), M005)
    Z001 = SizeOf (STR0)
    Z002 = SizeOf (BUF0)
    Z003 = SizeOf (PKG0)
    Z004 = MLOC ()
    Z005 = ObjectType (INT0)
    Z006 = ObjectType (STR0)
    Z007 = ObjectType (BUF0)
    Z008 = ObjectType (PKG0)
    Z009 = ObjectType (DEV0)
    Z010 = ObjectType (MTHD)
    Z011 = ObjectType (MTX0)
    Z014 = ObjectType (REG0)
    Z012 = ObjectType (FLD0)
    Z013 = MUNI ()
    Z000 = SizeOf (INT0)
    Z015 = MNAM (0x09)
}
