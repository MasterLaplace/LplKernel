// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Notify (\CPU0, 0x81) and Notify (\CPU1, 0x81): the C-states of CPU0 and CPU1 changed.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Notify (\CPU0, 0x81) __EXPECT__(6084)
    Notify (\CPU1, 0x81) __EXPECT__(6084)
}
