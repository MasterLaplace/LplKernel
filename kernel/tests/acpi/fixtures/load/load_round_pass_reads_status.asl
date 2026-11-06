// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief If (\FLAG) { Return (Zero) } Return (0x0F).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    If (\FLAG) __EXPECT__(6084)
    {
        Return (Zero)
    }

    Return (0x0F)
}
