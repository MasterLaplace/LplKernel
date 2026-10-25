"""The census of aml_census.py against hand-made tables whose method calls follow from the specification alone.

Usage: python3 -I tools/acpi/check_census.py

Each case is one or two definition blocks, and its count of method calls is derived from a rule of
docs/acpi/specification.md, never read from the census or the kernel's decoder. A rule no recorded machine
exercises can still be wrong, and only a case written for it can show it. Exits 1 when a case disagrees.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import aml_census  # noqa: E402

LOCAL0 = b"\x60"
ZERO = b"\x00"
ONE = b"\x01"
METHOD_OBJECT = 8
DEVICE_OBJECT = 6


def package(lead, body):
    """A construct of one PkgLength byte, which counts itself."""
    assert len(body) + 1 < 64
    return lead + bytes([len(body) + 1]) + body


def path(*segments, root=False):
    """A NameString of 4-character segments."""
    prefix = b"\\" if root else b""
    if len(segments) == 1:
        return prefix + segments[0]
    if len(segments) == 2:
        return prefix + b"\x2E" + segments[0] + segments[1]
    return prefix + b"\x2F" + bytes([len(segments)]) + b"".join(segments)


def method(name, arguments, body=b""):
    return package(b"\x14", name + bytes([arguments]) + body)


def scope(name, body):
    return package(b"\x10", name + body)


def device(name, body=b""):
    return package(b"\x5B\x82", name + body)


def external(name, object_type, arguments=0):
    return b"\x15" + name + bytes([object_type, arguments])


def block(signature, aml):
    """A definition block of the given signature holding the AML."""
    length = 36 + len(aml)
    return (signature + length.to_bytes(4, "little") + bytes([2, 0]) + b"TEST  " + b"CENSUS  "
            + (1).to_bytes(4, "little") + b"INTL" + (1).to_bytes(4, "little") + aml)


CASES = [
    ("a method in a SuperName is called (§4.2): SizeOf (MTHD)", 1, [block(b"SSDT",
        method(b"MTHD", 0) + method(b"USE_", 0, b"\x70\x87MTHD" + LOCAL0))]),
    ("a method in a SimpleName is never called (§4.2): CondRefOf (MTHD, Local0)", 0, [block(b"SSDT",
        method(b"MTHD", 0) + method(b"USE_", 0, b"\x5B\x12MTHD" + LOCAL0))]),
    ("Load takes a SuperName (§4.2), where a method is called", 1, [block(b"SSDT",
        method(b"MTHD", 0) + method(b"USE_", 0, b"\x5B\x20MTHD" + LOCAL0))]),
    ("the first declaration stays (§5.1): an Alias over a Name is skipped", 0, [block(b"SSDT",
        b"\x08ALI_" + ZERO + method(b"SRC_", 1) + b"\x06SRC_ALI_" + method(b"USE_", 0, b"ALI_" + ONE))]),
    ("declarations load in one pass (§3.6): an Alias of a method declared after it creates nothing", 0,
     [block(b"SSDT", b"\x06SRC_ALI_" + method(b"SRC_", 1) + method(b"USE_", 0, b"ALI_" + ONE))]),
    ("what a method body declares ends with the body (§3.5, §5.1): a nested root method", 0, [block(b"SSDT",
        method(b"MTHA", 0, method(path(b"INNR", root=True), 0)) + method(b"MTHB", 0, b"INNR"))]),
    ("an External a body's Name replaces comes back after the body (§5.1)", 1, [block(b"SSDT",
        external(path(b"FOO_", root=True), METHOD_OBJECT, 1)
        + method(b"MTHA", 0, b"\x08" + path(b"FOO_", root=True) + ZERO) + method(b"MTHB", 0, b"FOO_" + ONE))]),
    ("an External whose parent exists is placed where it stands (§3.6): a Scope opens it", 1, [block(b"SSDT",
        external(path(b"_SB_", b"DEV_", root=True), DEVICE_OBJECT)
        + scope(path(b"_SB_", b"DEV_", root=True), method(b"MINE", 0))
        + method(b"USE_", 0, path(b"_SB_", b"DEV_", b"MINE", root=True)))]),
    ("an External whose parent is missing is placed last, its missing segments as Externals (§5.1)", 1,
     [block(b"SSDT", external(path(b"_SB_", b"XYZ_", b"EXT_", root=True), METHOD_OBJECT, 1)
                     + method(b"USE_", 0, path(b"_SB_", b"XYZ_", b"EXT_", root=True) + ONE))]),
    ("the DSDT loads first (§3.8 step 1), so an SSDT given before it opens the DSDT's device", 1,
     [block(b"SSDT", scope(path(b"DEV0", root=True), method(b"MSSD", 0))),
      block(b"DSDT", device(b"DEV0") + method(b"USE_", 0, path(b"DEV0", b"MSSD", root=True)))]),
    ("Scope (\\) opens the root, wherever it stands (§5.1)", 1, [block(b"SSDT",
        device(b"DEV0", scope(b"\\\x00", method(b"ROOT", 0))) + method(b"USE_", 0, b"ROOT"))]),
]


def main():
    disagreements = 0
    for name, expected, blocks in CASES:
        labelled = [("case:%s%u" % (data[:4].decode(), index + 1), data) for index, data in enumerate(blocks)]
        census = aml_census.census_of_machine(name, labelled, 5)
        calls = census["methods"]["calls_resolved"]
        agrees = calls == expected and census["parse_errors"] == 0
        disagreements += 0 if agrees else 1
        print("%s %s: %u calls, %u expected" % ("ok" if agrees else "not ok", name, calls, expected))
    print("# census: %u of %u cases agree" % (len(CASES) - disagreements, len(CASES)))
    return 1 if disagreements else 0


if __name__ == "__main__":
    sys.exit(main())
