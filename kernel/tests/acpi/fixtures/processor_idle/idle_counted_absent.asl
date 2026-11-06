// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of a _STA that counts its calls in \D0RD and returns 0, absent.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    \D0RD++ __EXPECT__(6084)
    Return (Zero)
}
