#!/usr/bin/env python3
"""Writes the drive image the NVM Express tests read and write.

usage: tools/nvme-test-image.py <image> [mebibytes]

The image starts with the signature the tests look for before they write anything, so they never
write to a drive that is not a test image. Every other byte holds a value of its own offset, which
the tests recompute: a block read from the wrong place, or a page of the buffer left unfilled,
does not match.

    qemu-system-i386 ... -drive file=<image>,if=none,id=lplnvme,format=raw \\
                         -device nvme,serial=lplnvme0,drive=lplnvme

The value of the byte at offset o is (o / 512 * 251 + o % 512 * 13 + 7) mod 256, the same as
non_volatile_memory_express_test_byte in kernel/arch/i386/tests/drivers/nvme.c. The image is 16 MiB
unless told otherwise.
"""

import sys

SIGNATURE = b"LPLKERNEL NVME TEST IMAGE"
SIGNATURE_BYTES = 32


def byte_at(offset):
    return ((offset >> 9) * 251 + (offset & 0x1FF) * 13 + 7) & 0xFF


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    mebibytes = int(sys.argv[2]) if len(sys.argv) == 3 else 16
    if mebibytes < 1:
        raise SystemExit("the image needs at least 1 MiB")

    block = bytes(byte_at(offset) for offset in range(512))
    with open(sys.argv[1], "wb") as image:
        for first in range(0, mebibytes << 20, 512):
            data = bytes((value + (first >> 9) * 251) & 0xFF for value in block)
            if first == 0:
                data = SIGNATURE.ljust(SIGNATURE_BYTES, b"\0") + data[SIGNATURE_BYTES:]
            image.write(data)


if __name__ == "__main__":
    main()
