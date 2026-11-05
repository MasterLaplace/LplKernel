// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief \BSTA++ If (\FLAG) { Return (0x0F) } Return (Zero).
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
