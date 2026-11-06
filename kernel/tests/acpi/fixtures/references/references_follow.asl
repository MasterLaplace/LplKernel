// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Names, locals and arguments that hold references: chains of RefOf followed by DerefOf, a path String read
 *        through DerefOf, stores through locals and arguments, CondRefOf of a name, of \_OSI, of a missing name and of
 *        an External, references to a method's and to the table's locals, and a package element that names the name
 *        referring to it.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    External (UNDF, IntObj)

    Name (INT0, 0x1234)
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
    Name (R014, 0x07)
    Name (R015, 0x07)
    Name (R016, 0x07)
    Name (R017, 0x07)
    Name (R018, 0x07)
    Name (R019, 0x07)
    Name (R020, 0x07)
    Name (R021, 0x07)
    Device (DEVR) __EXPECT__(3141)
    {
    }

    Name (PKGD, Package (0x01)
    {
        DEVR,
    })
    Name (PKGL, Package (0x01)
    {
        ALSL,
    })
    Name (ALSL, 0x00)
    Method (MDRF, 0, NotSerialized)
    {
        Local0 = RefOf (INT0)
        Return (DerefOf (Local0))
    }

    Method (MLOC, 0, NotSerialized)
    {
        Local1 = 0x05
        Local0 = RefOf (Local1)
        Local0 = 0x09 __EXPECT__(3144)
        Return (Local1)
    }

    Method (MSET, 1, NotSerialized)
    {
        Arg0 = 0x07
    }

    Method (MARG, 0, NotSerialized)
    {
        Local0 = 0x01
        MSET (RefOf (Local0))
        Return (Local0)
    }

    Method (MCON, 0, NotSerialized)
    {
        If (CondRefOf (INT0, Local0))
        {
            Return (DerefOf (Local0))
        }

        Return (0x00)
    }

    Method (MRET, 0, NotSerialized)
    {
        Local0 = 0x42
        Return (RefOf (Local0)) __EXPECT__(3144)
    }

    Method (MCHN, 0, NotSerialized)
    {
        Local0 = 0x10
        Local1 = RefOf (Local0) __EXPECT__(3144)
        Local2 = RefOf (Local1) __EXPECT__(3144)
        Local3 = RefOf (Local2) __EXPECT__(3144)
        Local3++
        Return (DerefOf (Local3))
    }

    Method (MSTR, 0, NotSerialized)
    {
        Local0 = "\\INT0"
        Return (DerefOf (Local0))
    }

    Method (MKIN, 0, NotSerialized)
    {
        Local1 = "abc"
        Local0 = RefOf (Local1)
        Local0 = 0x41 __EXPECT__(3144)
        Return (ObjectType (Local1))
    }

    Method (MKRF, 1, NotSerialized)
    {
        Local0 = 0x99
        Arg0 [0x00] = RefOf (Local0) __EXPECT__(3144)
    }

    Method (MRDF, 1, NotSerialized)
    {
        Local0 = 0x5A __EXPECT__(3144)
        Return (DerefOf (DerefOf (Arg0 [0x00])))
    }

    Method (MDNG, 0, NotSerialized)
    {
        Local0 = Package (0x01)
            {
                0x00
            }
        MKRF (Local0)
        Return (MRDF (Local0))
    }

    Method (MTMP, 0, Serialized)
    {
        Name (TMPN, 0x11)
        Return (RefOf (TMPN))
    }

    Method (MKTP, 1, Serialized)
    {
        Name (TMPN, 0x33)
        Arg0 [0x00] = RefOf (TMPN)
    }

    Method (MRTP, 1, Serialized)
    {
        Name (OTHR, 0x22)
        Return (DerefOf (DerefOf (Arg0 [0x00])))
    }

    Method (MDTP, 0, NotSerialized)
    {
        Local0 = Package (0x01)
            {
                0x00
            }
        MKTP (Local0)
        Return (MRTP (Local0))
    }

    Method (MDNC, 0, NotSerialized)
    {
        Local0 = Package (0x01)
            {
                0x00
            }
        MKRF (Local0)
        Return (DerefOf (DerefOf (Local0 [0x00])))
    }

    Method (MCR2, 0, NotSerialized)
    {
        Local1 = 0x00
        If (CondRefOf (\MDRF, Local0))
        {
            Local1++
        }

        If (CondRefOf (\INT0, Local0))
        {
            Local1++
        }

        If (CondRefOf (\R000, Local0))
        {
            Local1++
        }

        Return ((Local1 + DerefOf (Local0)))
    }

    Method (MCRB, 1, NotSerialized)
    {
        If (CondRefOf (\_OSI, Local0))
        {
            Noop
        }

        Local0 = Arg0
        CreateDWordField (Local0, 0x00, CDW1)
        CDW1 |= 0x10
        Return (DerefOf (Local0 [0x00]))
    }

    Method (MSE2, 1, NotSerialized)
    {
        Local0 = 0x33
        Arg0 = RefOf (Local0) __EXPECT__(3144)
    }

    Method (MDED, 0, NotSerialized)
    {
        Local1 = 0x00
        MSE2 (RefOf (Local1))
        Local1 = 0x44
        Return (Local1)
    }

    Method (MELD, 0, NotSerialized)
    {
        Local0 = PKGD [0x00]
        Local0 = 0x05
        Return (Local0)
    }

    Method (MELR, 0, NotSerialized)
    {
        Local1 = 0x05
        Local0 = Package (0x01)
            {
                0x00
            }
        Local0 [0x00] = RefOf (Local1)
        Local0 [0x00] = 0x09
        Return (Local1)
    }

    R000 = MDRF ()
    R001 = MLOC ()
    R002 = MARG ()
    R003 = MCON ()
    R004 = CondRefOf (\UNDF)
    R005 = CondRefOf (\_OSI)
    R006 = CondRefOf (\NOPE)
    R007 = MRET ()
    R008 = MCHN ()
    R009 = MSTR ()
    Local0 = 0x77
    Local1 = RefOf (Local0)
    Local2 = RefOf (Local1)
    R010 = DerefOf (Local2)
    R011 = MKIN ()
    R012 = MDNG ()
    R013 = MTMP ()
    R014 = MDTP ()
    R015 = MDNC ()
    R016 = MELR ()
    R018 = MCR2 ()
    R019 = MCRB (Buffer (0x04)
            {
                 0x01, 0x00, 0x00, 0x00
            })
    R020 = MDED ()
    R021 = MELD ()
    CopyObject (PKGL [0x00], ALSL)
    R017 = DerefOf (PKGL [0x00])
}
