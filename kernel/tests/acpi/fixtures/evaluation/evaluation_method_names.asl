// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief M005 declares TMP_, 7, and returns it, called twice into RES0 and RES1; M006 declares LOOP in the body of
 *        an endless While, and the table's code calls it.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES0, Zero)
    Name (RES1, Zero)
    Method (M005, 0, NotSerialized)
    {
        Name (TMP, 0x07)
        Return (TMP)
    }

    RES0 = M005 ()
    RES1 = M005 ()
    Method (M006, 0, NotSerialized)
    {
        While (One)
        {
            Name (LOOP, One)
        }
    }

    M006 ()
}
