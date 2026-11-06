// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of a _STA that counts its calls in \CSTA and returns 0x0F, present.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    \CSTA++ __EXPECT__(6084)
    Return (0x0F)
}
