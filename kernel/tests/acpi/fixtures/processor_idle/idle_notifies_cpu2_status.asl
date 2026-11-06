// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Notify (\CPU2, 0x80): a notification of CPU2 that is not 0x81.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Notify (\CPU2, 0x80) __EXPECT__(6084)
}
