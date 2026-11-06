// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief STR0, the String "XY", and \_SB.MCAT, which returns the Concatenate of its two arguments.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (STR0, "XY")
    Scope (\_SB)
    {
        Method (MCAT, 2, NotSerialized)
        {
            Return (Concatenate (Arg0, Arg1))
        }
    }
}
