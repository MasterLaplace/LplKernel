// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MSET stores RefOf (TGT0) to ALS0 through its arguments, making ALS0 an alias, which the table's code reads,
 *        writes and asks ObjectType of; MPUT stores an Integer through a reference to its argument, given STR0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (TGT0, 0x1234)
    Name (ALS0, 0x00)
    Name (STR0, "ABCDEFGH")
    Name (R000, 0x07)
    Name (R001, 0x07)
    Name (R002, 0x07)
    Name (R003, 0x07)
    Method (MSET, 2, NotSerialized)
    {
        Arg1 = Arg0
    }

    Method (MPUT, 1, NotSerialized)
    {
        Local0 = RefOf (Arg0)
        Local0 = 0x44434241 __EXPECT__(3144)
    }

    MSET (RefOf (TGT0), RefOf (ALS0))
    R000 = ALS0
    ALS0 = 0x05
    R001 = TGT0
    R002 = ObjectType (ALS0)
    MPUT (STR0)
    R003 = SizeOf (STR0)
}
