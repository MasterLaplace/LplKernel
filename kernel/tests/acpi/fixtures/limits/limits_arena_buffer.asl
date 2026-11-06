// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief RES_ holds the size of the Buffer of 32 KiB MBIG builds in a local, whose pages go back when MBIG returns.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (RES, 5)
    Method (MBIG)
    {
        Local0 = Buffer (0x8000) {}
        Return (SizeOf (Local0))
    }
    RES = MBIG ()
}
