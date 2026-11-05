// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief RES0, RES1 and RES2, 5 each, take ShiftLeft (One, 64), Divide (One, Zero) and Mod (One, Zero).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES0, 0x05)
    Name (RES1, 0x05)
    Name (RES2, 0x05)
    Store (ShiftLeft (One, 0x40), RES0)
    Store (Divide (One, Zero, ), RES1)
    Store (Mod (One, Zero), RES2)
}
