// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Processor (CPU0, 1, 0x810, 6), Mutex (MUT_, 5) and PowerResource (PWR_, 1, 0x0203).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Processor (CPU0, 0x01, 0x00000810, 0x06){} __EXPECT__(3168)
    Mutex (MUT, 0x05)
    PowerResource (PWR, 0x01, 0x0203){}
}
