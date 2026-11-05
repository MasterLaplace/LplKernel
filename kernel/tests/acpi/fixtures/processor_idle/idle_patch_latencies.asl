// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (C6LT, Index (C6TM, 2)) and Store (C7LT, Index (C7TM, 2)): Intel's pattern patches C6's and C7's
 *        latencies from its NVS fields.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    C6TM [0x02] = C6LT __EXPECT__(6084)
    C7TM [0x02] = C7LT __EXPECT__(6084)
}
