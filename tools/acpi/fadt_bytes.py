"""The bytes of the FADT `test-acpi --machine <dump>` normalises, for ACPICA's FADT to be built from the same table.

Usage: python3 -I tools/acpi/fadt_bytes.py <dump>

The FADT is the one the directory gives, the first whose checksum holds, else the first, cut to its header's length;
nothing is written when the dump has none.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import expected_directory  # noqa: E402


def main():
    recorded = expected_directory.read_directory(sys.argv[1])[0]
    table = expected_directory.lookup(recorded, b"FACP")
    if table is not None:
        sys.stdout.buffer.write(table[:int.from_bytes(table[4:8], "little")])


if __name__ == "__main__":
    main()
