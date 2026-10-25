"""The directory `test-acpi --machine <dump>` must print for an acpidump text file.

Usage: python3 -I tools/acpi/expected_directory.py <dump>

Computed from the dump alone, apart from the kernel's code, with the reader of
real_firmware_census.py. A dump holds no root pointer and no root table, so the harness writes an
XSDT that lists every table but the root pointer, the root tables, the FACS and the DSDT, in the
dump's order; the DSDT follows when the FADT the directory takes names one, by X_DSDT below 4 GiB or
by its 32-bit field. The harness serves the DSDT at every address a FADT names, and serves each table
with as many bytes as the dump holds, never padded.
A directory records 64 tables and counts the rest unreachable, with every table whose bytes or
header length cannot be a table, or whose header claims more bytes than the dump holds. The FACS is
the 32-bit FIRMWARE_CTRL when it is not 0, else X_FIRMWARE_CTRL when it lies below 4 GiB, else none.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import real_firmware_census  # noqa: E402

CAPACITY = 64
HEADER_BYTES = 36
MAXIMUM_LENGTH = 0x01000000
LEFT_OUT = ("XSDT", "RSDT", "FACS")


def field(data, length, offset, width):
    if length < offset + width:
        return 0
    return int.from_bytes(data[offset:offset + width].ljust(width, b"\0"), "little")


def is_left_out(data):
    return len(data) < 4 or data[:4] == b"RSD " or data[:4].decode("latin-1") in LEFT_OUT


def can_be_mapped(data):
    if len(data) < HEADER_BYTES:
        return False
    length = int.from_bytes(data[4:8], "little")
    return HEADER_BYTES <= length <= min(len(data), MAXIMUM_LENGTH)


def checksum_holds(data):
    length = int.from_bytes(data[4:8], "little")
    return sum(data[:length]) % 256 == 0


def lookup(recorded, signature):
    """The table the directory gives for a signature: the first whose checksum holds, else the first."""
    matching = [data for data in recorded if data[:4] == signature]
    return next((data for data in matching if checksum_holds(data)), matching[0] if matching else None)


def control_port(fadt, length, offset_32, offset_extended):
    """A PM1 control port of the FADT: its 32-bit block when not 0, else its extended block when it lies in the
    I/O space; 0 when neither gives a port."""
    port = field(fadt, length, offset_32, 4)
    if port != 0:
        return port if port <= 0xFFFF else 0
    if length < offset_extended + 12:
        return 0
    space, address = field(fadt, length, offset_extended, 1), field(fadt, length, offset_extended + 4, 8)
    return address if space == 1 and address <= 0xFFFF else 0


def read_directory(path):
    """The tables the directory records for the dump at path, in its order, the count it calls unreachable, the
    FACS, the PM1a and PM1b control ports, and the hardware-reduced flag."""
    tables = [bytes(entry["bytes"]) for entry in real_firmware_census.read_acpidump(path)]
    dsdt = next((data for data in tables if data[:4] == b"DSDT"), None)
    listed = [data for data in tables if data is not dsdt and not is_left_out(data)]

    recorded, unreachable = [], 0
    for data in listed:
        if len(recorded) == CAPACITY or not can_be_mapped(data):
            unreachable += 1
        else:
            recorded.append(data)

    facs, control, reduced = 0, [0, 0], 0
    fadt_recorded = lookup(recorded, b"FACP")
    if fadt_recorded is not None:
        length = int.from_bytes(fadt_recorded[4:8], "little")
        dsdt_32 = field(fadt_recorded, length, 40, 4)
        dsdt_64 = field(fadt_recorded, length, 140, 8)
        if (dsdt_64 != 0 and dsdt_64 >> 32 == 0) or dsdt_32 != 0:
            if dsdt is None or len(recorded) == CAPACITY or not can_be_mapped(dsdt):
                unreachable += 1
            else:
                recorded.append(dsdt)
        facs_32 = field(fadt_recorded, length, 36, 4)
        facs_64 = field(fadt_recorded, length, 132, 8)
        facs = facs_32 if facs_32 != 0 else (facs_64 if facs_64 >> 32 == 0 else 0)
        control = [control_port(fadt_recorded, length, 64, 172), control_port(fadt_recorded, length, 68, 184)]
        reduced = (field(fadt_recorded, length, 112, 4) >> 20) & 1
    return recorded, unreachable, facs, control, reduced


def main():
    recorded, unreachable, facs, control, reduced = read_directory(sys.argv[1])
    print("tables=%u unreachable=%u facs=0x%08X pm1_control=0x%04X,0x%04X hardware_reduced=%u" % (
        len(recorded), unreachable, facs, control[0], control[1], reduced))
    for data in recorded:
        info, _ = real_firmware_census.describe_table({"label": "", "bytes": data, "gaps": 0})
        print("%s length=%u revision=%u checksum=%s" % (
            info["signature"][:4], info["length"], info["revision"], "ok" if info["checksum_ok"] else "bad"))


if __name__ == "__main__":
    main()
