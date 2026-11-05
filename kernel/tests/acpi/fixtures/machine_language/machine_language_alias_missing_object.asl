// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Alias (ZOO_, BAR_), of an object that does not exist, then Name (AFTR, 7).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Alias (ZOO, BAR) __EXPECT__(6084) __EXPECT__(6085)
    Name (AFTR, 0x07)
}
