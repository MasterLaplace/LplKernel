"""The SSDTs a dump's firmware loads at run time, and where `test-acpi` serves them for Load (specification §8.4).

Usage: python3 -I tools/acpi/loaded_tables.py <dump>

Computed from the dump alone, apart from the harness's code. A DSDT or SSDT that holds the bytes of a Load, 5B 20,
references a table in two ways: a String label followed by two integers, an address and a length, as Intel's PmRef
packages do; or a SystemMemory OperationRegion whose address and length are integers and whose name one of its Loads
follows. A reference names an SSDT the dump holds whole of that length: one whose OEM table ID the label names, case
and trailing spaces and NULs apart, else the only one of that length, if not placed yet. Each table is placed at the
first address a reference gives, below 4 GiB, unless that place overlaps the DSDT where a FADT names it, the FACS
where the first FADT names it, or a table placed before. Prints `placed <OEM table ID> 0x<address> <length>` per
table, in the order they were placed.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import real_firmware_census  # noqa: E402

HEADER_BYTES = 36
LIMIT = 1 << 32
WIDTHS = {0x0A: 1, 0x0B: 2, 0x0C: 4, 0x0E: 8}


def header_length(data):
    return int.from_bytes(data[4:8], "little") if len(data) >= 8 else 0


def whole_ssdt(data):
    return len(data) >= HEADER_BYTES and data[:4] == b"SSDT" and header_length(data) == len(data)


def integer(data, offset):
    """The AML integer at offset, Zero, One or a prefixed constant, and the offset past it; None otherwise."""
    if offset >= len(data):
        return None
    prefix = data[offset]
    if prefix in (0x00, 0x01):
        return prefix, offset + 1
    width = WIDTHS.get(prefix)
    if width is None or len(data) - offset - 1 < width:
        return None
    return int.from_bytes(data[offset + 1:offset + 1 + width], "little"), offset + 1 + width


def names(label, identifier):
    """Whether a label names an OEM table ID: the same letters, case and trailing spaces and NULs apart."""
    wanted = identifier.rstrip(b" \0")
    return len(wanted) != 0 and label.rstrip(b" ").upper() == wanted.upper()


def loads(data, name):
    """Whether data holds a Load followed by the bytes of name."""
    return data.find(b"\x5b\x20" + name, HEADER_BYTES) >= 0


def reference(data, offset):
    """The (label or None, address, length) a reference at offset gives, or None."""
    if data[offset] == 0x0D:
        end = data.find(b"\0", offset + 1)
        if end < 0 or end - offset - 1 > 8:
            return None
        label, cursor = data[offset + 1:end], end + 1
    elif (data[offset:offset + 2] == b"\x5b\x80" and offset + 7 < len(data) and
          (0x41 <= data[offset + 2] <= 0x5A or data[offset + 2] == 0x5F) and data[offset + 6] == 0 and
          loads(data, data[offset + 2:offset + 6])):
        label, cursor = None, offset + 7
    else:
        return None
    address = integer(data, cursor)
    if address is None:
        return None
    length = integer(data, address[1])
    if length is None:
        return None
    return label, address[0], length[0]


def named(tables, placed, label, length):
    """The index of the SSDT, not placed yet, a reference of length labelled label names, or None."""
    same = [index for index, data in enumerate(tables) if whole_ssdt(data) and len(data) == length]
    for index in same:
        if index not in placed and label is not None and names(label, tables[index][16:24]):
            return index
    return same[0] if len(same) == 1 and same[0] not in placed else None


def taken(tables):
    """The ranges the harness serves before it places a table for Load: the DSDT where each FADT names it, the
    FACS where the first FADT names it."""
    dsdt = next((data for data in tables if data[:4] == b"DSDT"), None)
    facs = next((data for data in tables if data[:4] == b"FACS"), None)
    ranges, fadts = [], [data for data in tables if data[:4] == b"FACP"]
    for fadt in fadts:
        for offset, width in ((40, 4), (140, 8)):
            address = int.from_bytes(fadt[offset:offset + width].ljust(width, b"\0"), "little")
            if dsdt is not None and address != 0 and all(start != address for start, _ in ranges):
                ranges.append((address, len(dsdt)))
    if facs is not None and fadts:
        facs_32 = int.from_bytes(fadts[0][36:40].ljust(4, b"\0"), "little")
        facs_64 = int.from_bytes(fadts[0][132:140].ljust(8, b"\0"), "little")
        address = facs_32 if facs_32 != 0 else (facs_64 if facs_64 >> 32 == 0 else 0)
        if address != 0 and all(start != address for start, _ in ranges):
            ranges.append((address, len(facs)))
    return ranges


def placed_tables(tables):
    """{index: address} of the tables placed for Load, in the order they were placed."""
    placed, ranges = {}, taken(tables)
    for source_index, source in enumerate(tables):
        if source[:4] not in (b"SSDT", b"DSDT") or len(source) < HEADER_BYTES or not loads(source, b""):
            continue
        for offset in range(HEADER_BYTES, len(source)):
            found = reference(source, offset)
            if found is None:
                continue
            label, address, length = found
            if address == 0 or length < HEADER_BYTES or address + length > LIMIT:
                continue
            index = named(tables, placed, label, length)
            if index is None or index == source_index:
                continue
            if any(address < start + size and start < address + length for start, size in ranges):
                continue
            placed[index] = address
            ranges.append((address, length))
    return placed


def main():
    tables = [bytes(entry["bytes"]) for entry in real_firmware_census.read_acpidump(sys.argv[1])]
    for index, address in placed_tables(tables).items():
        print("placed %s 0x%08X %u" % (tables[index][16:24].rstrip(b" \0").decode("latin-1"), address,
                                       len(tables[index])))


if __name__ == "__main__":
    main()
