// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Name (_STA, Buffer (0) {}): a status that reads as 0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (_STA, Buffer (Zero){}) __EXPECT__(6105)
}
