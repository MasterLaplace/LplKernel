// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief RES0 takes Add (Ones, One), RES1 Ones, and RES2 LEqual (Ones, 0xFFFFFFFF): each depends on the width of
 *        the integers.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES0, Zero)
    Name (RES1, Zero)
    Name (RES2, Zero)
    Store (Add (Ones, One), RES0)
    Store (Ones, RES1)
    Store (LEqual (Ones, 0xFFFFFFFF), RES2)
}
