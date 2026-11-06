// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Methods that acquire `\_GL`: TAKE acquires then releases it and returns what Acquire gave,
 *        HOLD acquires it and keeps it, NEST acquires it twice and releases it twice.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (TAKE, 0, NotSerialized)
    {
        Local0 = Acquire (\_GL, 0xFFFF)
        Release (\_GL)
        Return (Local0)
    }

    Method (HOLD, 0, NotSerialized)
    {
        Return (Acquire (\_GL, 0xFFFF))
    }

    Method (NEST, 0, NotSerialized)
    {
        Acquire (\_GL, 0xFFFF)
        Acquire (\_GL, 0xFFFF)
        Release (\_GL)
        Release (\_GL)
    }
}
