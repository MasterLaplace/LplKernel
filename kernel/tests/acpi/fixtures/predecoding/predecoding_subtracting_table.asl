// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief FILL, 12 bytes, then LPB_ at the offsets of LPA_, its Add a Subtract: so -5; as an SSDT.
 */
DefinitionBlock ("", "SSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (FILL, 0x12345678)
    Noop
    Noop
    Method (LPB_, 0, NotSerialized)
    {
        Store (Zero, Local0)
        Store (Zero, Local1)
        While (LLess (Local0, 0x05))
        {
            Store (INCR (Local2), Local2) __EXPECT__(6066) __EXPECT__(6084)
            Subtract (Local1, One, Local1)
            Increment (Local0)
        }

        Return (Local1)
    }
}
