// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Return (Add (MF0_, MF1_)): the end of MREG, which reads both units of its field.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Return ((MF0 + MF1)) __EXPECT__(6084)
}
