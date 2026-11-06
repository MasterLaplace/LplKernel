// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Mutex (MTXA, 1), whose SyncLevel keeps a _REG of level 10 from running.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Mutex (MTXA, 0x01)
}
