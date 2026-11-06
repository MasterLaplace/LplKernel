// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief If (ECF_) { OperationRegion (SKPR, SystemMemory, Add (BASE, Zero), One) and its field SKF_ }: the branch
 *        the load declares without taking.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    If (ECF_) __EXPECT__(6084)
    {
        OperationRegion (SKPR, SystemMemory, (BASE + Zero), One) __EXPECT__(6084)
        Field (SKPR, ByteAcc, NoLock, Preserve)
        {
            SKF_, 8
        }
    }
}
