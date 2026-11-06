// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of a _STA that counts its calls in \BSTA and returns 0x0F once \FLAG is set, 0 before.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    \BSTA++ __EXPECT__(6084)
    If (\FLAG) __EXPECT__(6084)
    {
        Return (0x0F)
    }

    Return (Zero)
}
