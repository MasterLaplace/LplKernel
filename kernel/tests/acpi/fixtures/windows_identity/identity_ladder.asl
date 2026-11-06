// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief An `\_OSI` ladder as firmware writes it in `\_SB._INI`: behind CondRefOf (\_OSI), each Windows
 *        string asked sets OSYS to its year, "Linux" to 1000 and "FreeBSD" to 1001; without `\_OSI`,
 *        `\_OS` sets 1999.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (OSYS, 0x00)
    Scope (\_SB)
    {
        Method (_INI, 0, NotSerialized)
        {
            OSYS = 0x07D0
            If (CondRefOf (\_OSI))
            {
                If (\_OSI ("Windows 2001"))
                {
                    OSYS = 0x07D1
                }

                If (\_OSI ("Windows 2001 SP1"))
                {
                    OSYS = 0x07D2
                }

                If (\_OSI ("Windows 2006"))
                {
                    OSYS = 0x07D6
                }

                If (\_OSI ("Windows 2009"))
                {
                    OSYS = 0x07D9
                }

                If (\_OSI ("Windows 2012"))
                {
                    OSYS = 0x07DC
                }

                If (\_OSI ("Windows 2013"))
                {
                    OSYS = 0x07DD
                }

                If (\_OSI ("Windows 2015"))
                {
                    OSYS = 0x07DF
                }

                If (\_OSI ("Windows 2022"))
                {
                    OSYS = 0x07E6
                }

                If (\_OSI ("Linux"))
                {
                    OSYS = 0x03E8
                }

                If (\_OSI ("FreeBSD"))
                {
                    OSYS = 0x03E9
                }
            }
            ElseIf ((\_OS == "Microsoft Windows NT"))
            {
                OSYS = 0x07CF
            }
        }
    }
}
