// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief CreateBitField, CreateByteField, CreateWordField, CreateDWordField, CreateQWordField and CreateField over
 *        named Buffers and over a method's argument, read and written.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (BUF0, Buffer (0x08)
    {
         0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
    })
    CreateByteField (BUF0, 0x01, BYT0)
    CreateWordField (BUF0, 0x02, WRD0)
    CreateDWordField (BUF0, 0x04, DWD0)
    CreateQWordField (BUF0, 0x00, QWD0)
    CreateBitField (BUF0, 0x09, BIT0)
    CreateField (BUF0, 0x04, 0x0C, FLD0)
    Name (BUF1, Buffer (0x04){})
    CreateDWordField (BUF1, 0x02, PAST) __EXPECT__(3165)
    Name (BUF2, Buffer (0x02)
    {
         0xFF, 0x00
    })
    CreateField (BUF2, 0x04, 0x0C, FLD2)
    Name (BUF3, Buffer (0x04)
    {
         0x11, 0x22, 0x33, 0x44
    })
    CreateField (BUF3, 0x0C, 0x0C, FLD3)
    Name (R000, 0x07)
    Name (R001, 0x07)
    Name (R002, 0x07)
    Name (R003, 0x07)
    Name (R004, 0x07)
    Name (R005, 0x00)
    Name (R006, 0x00)
    Name (R007, 0x00)
    Name (R008, 0x07)
    Name (R009, 0x00)
    Name (R010, 0x07)
    Name (R011, 0x00)
    Name (R012, 0x00)
    Method (MOSC, 1, NotSerialized)
    {
        CreateDWordField (Arg0, 0x00, CDW1)
        CreateDWordField (Arg0, 0x04, CDW2)
        CDW2 = (CDW1 | 0x10)
        Return (Arg0)
    }

    Method (MBIG, 0, NotSerialized)
    {
        CreateField (BUF1, 0xFFFFFFF0, 0x20, FBIG)
        Return (0x00)
    }

    Method (MEMP, 0, NotSerialized)
    {
        CreateField (BUF1, 0x08, 0x00, FEMP) __EXPECT__(3083)
        FEMP = 0xFF
        Return (FEMP)
    }

    R000 = BYT0
    R001 = WRD0
    R002 = DWD0
    R003 = QWD0
    R004 = BIT0
    CopyObject (FLD0, R005)
    BYT0 = 0xAA
    BIT0 = Zero
    WRD0 = 0x5A5A
    CopyObject (MOSC (Buffer (0x08)
            {
                 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
            }), R006)
    CopyObject (BUF0, R007)
    R008 = MBIG ()
    CopyObject (MEMP (), R009)
    R010 = PAST
    FLD2 = BUF2
    CopyObject (BUF2, R011)
    FLD3 = BUF3
    CopyObject (BUF3, R012)
}
