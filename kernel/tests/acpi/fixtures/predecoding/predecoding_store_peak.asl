// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A Store first run from a record, as PEAK: `Local0++` three times, then `While (Local0) { Local0--;
 *        If (Local1) { Local2 = Local0 }; Local1++ }; Return (Local2)`; nothing before its second turn holds
 *        2 values, and the Store, its If false at the first turn, first runs at the second, from its record.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (PEAK, 0, NotSerialized)
    {
        Local0++ __EXPECT__(6066)
        Local0++ __EXPECT__(6066)
        Local0++ __EXPECT__(6066)
        While (Local0) __EXPECT__(6066)
        {
            Local0-- __EXPECT__(6066)
            If (Local1) __EXPECT__(6066)
            {
                Local2 = Local0 __EXPECT__(6066)
            }

            Local1++ __EXPECT__(6066)
        }

        Return (Local2)
    }
}
