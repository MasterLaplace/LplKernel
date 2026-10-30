"""Census of the ACPI Machine Language found in DSDT and SSDT tables.

Walks the AML byte stream of each definition block by the grammar of the
ACPI specification, chapter 20 (ACPI 6.5 numbering): the table header
(20.2.1), name strings (20.2.2), data objects (20.2.3), PkgLength (20.2.4),
and every term object (20.2.5).  It counts which opcodes and which named
objects appear, resolves names with the rules of section 5.3 so that a
method invocation consumes the right number of arguments, and reports the
facts an interpreter has to plan for: operation region spaces, field
access widths, method shapes, the strings passed to \\_OSI, the UUIDs
checked by _OSC, the resource descriptors found in buffers, and the static
call depth.

Inputs, any mix of:
  * a raw table file (it starts with the DSDT or SSDT signature);
  * an acpidump text dump ("DSDT @ 0x..." followed by hexadecimal lines),
    such as the dumps of the linuxhw/ACPI corpus: every DSDT and SSDT in
    the dump is loaded into one namespace, as one machine.

Each positional file is one machine; a directory adds each file in it.  ``--machine NAME=FILE[,FILE...]``
loads several raw tables into one namespace, the way an OS loads a DSDT
and its SSDTs.  The JSON census goes to standard output.

The tool reads untrusted bytes: every read is bounds checked, a malformed
term list is skipped to the end of its enclosing package and reported, and
nothing in the input is ever executed.

Usage:
  python3 -I tools/acpi/aml_census.py [--machine NAME=FILE[,FILE...]]... [FILE...]
                           [--per-machine] [--top N]
"""

import argparse
import collections
import json
import os
import re
import sys

TABLE_HEADER_SIZE = 36
MAXIMUM_TERM_DEPTH = 256

ONE_BYTE_OPCODES = {
    0x00: "Zero", 0x01: "One", 0x06: "Alias", 0x08: "Name",
    0x0A: "BytePrefix", 0x0B: "WordPrefix", 0x0C: "DWordPrefix",
    0x0D: "StringPrefix", 0x0E: "QWordPrefix", 0x10: "Scope",
    0x11: "Buffer", 0x12: "Package", 0x13: "VarPackage", 0x14: "Method",
    0x15: "External",
    0x60: "Local0", 0x61: "Local1", 0x62: "Local2", 0x63: "Local3",
    0x64: "Local4", 0x65: "Local5", 0x66: "Local6", 0x67: "Local7",
    0x68: "Arg0", 0x69: "Arg1", 0x6A: "Arg2", 0x6B: "Arg3", 0x6C: "Arg4",
    0x6D: "Arg5", 0x6E: "Arg6",
    0x70: "Store", 0x71: "RefOf", 0x72: "Add", 0x73: "Concat",
    0x74: "Subtract", 0x75: "Increment", 0x76: "Decrement",
    0x77: "Multiply", 0x78: "Divide", 0x79: "ShiftLeft", 0x7A: "ShiftRight",
    0x7B: "And", 0x7C: "Nand", 0x7D: "Or", 0x7E: "Nor", 0x7F: "Xor",
    0x80: "Not", 0x81: "FindSetLeftBit", 0x82: "FindSetRightBit",
    0x83: "DerefOf", 0x84: "ConcatRes", 0x85: "Mod", 0x86: "Notify",
    0x87: "SizeOf", 0x88: "Index", 0x89: "Match",
    0x8A: "CreateDWordField", 0x8B: "CreateWordField",
    0x8C: "CreateByteField", 0x8D: "CreateBitField", 0x8E: "ObjectType",
    0x8F: "CreateQWordField", 0x90: "LAnd", 0x91: "LOr", 0x92: "LNot",
    0x93: "LEqual", 0x94: "LGreater", 0x95: "LLess", 0x96: "ToBuffer",
    0x97: "ToDecimalString", 0x98: "ToHexString", 0x99: "ToInteger",
    0x9C: "ToString", 0x9D: "CopyObject", 0x9E: "Mid", 0x9F: "Continue",
    0xA0: "If", 0xA1: "Else", 0xA2: "While", 0xA3: "Noop", 0xA4: "Return",
    0xA5: "Break", 0xCC: "BreakPoint", 0xFF: "Ones",
}

EXTENDED_OPCODES = {
    0x01: "Mutex", 0x02: "Event", 0x12: "CondRefOf", 0x13: "CreateField",
    0x1F: "LoadTable", 0x20: "Load", 0x21: "Stall", 0x22: "Sleep",
    0x23: "Acquire", 0x24: "Signal", 0x25: "Wait", 0x26: "Reset",
    0x27: "Release", 0x28: "FromBCD", 0x29: "ToBCD", 0x2A: "Unload",
    0x30: "Revision", 0x31: "Debug", 0x32: "Fatal", 0x33: "Timer",
    0x80: "OperationRegion", 0x81: "Field", 0x82: "Device",
    0x83: "Processor", 0x84: "PowerResource", 0x85: "ThermalZone",
    0x86: "IndexField", 0x87: "BankField", 0x88: "DataRegion",
}

SPECIFICATION_STATUS = {
    "Unload": "reserved since ACPI 6.5 (20.3: 0x5B 0x2A Reserved)",
    "Processor": "permanently reserved, deprecated in ACPI 6.4 (20.3: 0x5B 0x83)",
}

OPERAND_SHAPES = {
    "Store": "TS", "RefOf": "M", "Add": "TTR", "Concat": "TTR",
    "Subtract": "TTR", "Increment": "S", "Decrement": "S", "Multiply": "TTR",
    "Divide": "TTRR", "ShiftLeft": "TTR", "ShiftRight": "TTR", "And": "TTR",
    "Nand": "TTR", "Or": "TTR", "Nor": "TTR", "Xor": "TTR", "Not": "TR",
    "FindSetLeftBit": "TR", "FindSetRightBit": "TR", "DerefOf": "T",
    "ConcatRes": "TTR", "Mod": "TTR", "Notify": "ST", "SizeOf": "S",
    "Index": "TTR", "Match": "TBTBTT", "ObjectType": "M", "LAnd": "TT",
    "LOr": "TT", "LNot": "T", "LEqual": "TT", "LGreater": "TT",
    "LLess": "TT", "ToBuffer": "TR", "ToDecimalString": "TR",
    "ToHexString": "TR", "ToInteger": "TR", "ToString": "TTR",
    "CopyObject": "TM", "Mid": "TTTR", "Return": "T",
    "CondRefOf": "MR", "LoadTable": "TTTTTT", "Load": "SR", "Stall": "T",
    "Sleep": "T", "Acquire": "SW", "Signal": "S", "Wait": "ST", "Reset": "S",
    "Release": "S", "FromBCD": "TR", "ToBCD": "TR", "Unload": "S",
    "Fatal": "BDT",
}

NO_OPERAND_OPCODES = {
    "Zero", "One", "Ones", "Revision", "Debug", "Timer", "Noop", "Break",
    "Continue", "BreakPoint",
}

REGION_SPACES = {
    0x00: "SystemMemory", 0x01: "SystemIO", 0x02: "PCI_Config",
    0x03: "EmbeddedControl", 0x04: "SMBus", 0x05: "SystemCMOS",
    0x06: "PciBarTarget", 0x07: "IPMI", 0x08: "GeneralPurposeIO",
    0x09: "GenericSerialBus", 0x0A: "PCC", 0x0B: "PlatformRtMechanism",
    0x7F: "FFixedHW",
}

ACCESS_TYPES = {0: "AnyAcc", 1: "ByteAcc", 2: "WordAcc", 3: "DWordAcc",
                4: "QWordAcc", 5: "BufferAcc"}
UPDATE_RULES = {0: "Preserve", 1: "WriteAsOnes", 2: "WriteAsZeros"}
ACCESS_WIDTH_BITS = {0: 8, 1: 8, 2: 16, 3: 32, 4: 64, 5: 8}

PREDEFINED_ROOT = {
    ("_GPE",): "Scope", ("_PR_",): "Scope", ("_SB_",): "Device",
    ("_SI_",): "Scope", ("_TZ_",): "Device", ("_REV",): "Integer",
    ("_OS_",): "String", ("_GL_",): "Mutex", ("_OSI",): "Method",
}

INTERESTING_NAMES = [
    "_PRT", "_S0_", "_S1_", "_S2_", "_S3_", "_S4_", "_S5_", "_PTS", "_WAK",
    "_TTS", "_CST", "_CSD", "_LPI", "_RDI", "_PSS", "_PCT", "_PPC", "_PSD",
    "_PDL", "_CPC", "_TSS", "_PTC", "_TPC", "_HID", "_CID", "_UID", "_ADR",
    "_STA", "_CRS", "_PRS", "_SRS", "_DIS", "_OSC", "_PDC", "_DSM", "_DSD",
    "_INI", "_REG", "_PIC", "_MAT", "_PXM", "_SEG", "_BBN", "_CCA", "_DEP",
    "_PRW", "_PS0", "_PS3", "_PR0", "_PR3", "_ON_", "_OFF", "_EJ0", "_RMV",
    "_TMP", "_CRT", "_HOT", "_PSV", "_PSL", "_TC1", "_TC2", "_TSP", "_AC0",
    "_AL0", "_TZP", "_SCP", "_TZD", "_AEI", "_EVT", "_GLK", "_SUN", "_STR",
]

SMALL_RESOURCES = {
    0x4: "IRQ", 0x5: "DMA", 0x6: "StartDependentFn", 0x7: "EndDependentFn",
    0x8: "IO", 0x9: "FixedIO", 0xA: "FixedDMA", 0xE: "VendorShort",
    0xF: "EndTag",
}
LARGE_RESOURCES = {
    0x01: "Memory24", 0x02: "Register", 0x04: "VendorLong", 0x05: "Memory32",
    0x06: "Memory32Fixed", 0x07: "DWordAddress", 0x08: "WordAddress",
    0x09: "Interrupt", 0x0A: "QWordAddress", 0x0B: "ExtendedAddress",
    0x0C: "GpioConnection", 0x0D: "PinFunction",
    0x0E: "SerialBusConnection", 0x0F: "PinConfig", 0x10: "PinGroup",
    0x11: "PinGroupFunction", 0x12: "PinGroupConfig", 0x13: "ClockInput",
}
ADDRESS_RESOURCE_TYPES = {0: "Memory", 1: "IO", 2: "BusNumber"}
SERIAL_BUS_TYPES = {1: "I2C", 2: "SPI", 3: "UART", 4: "CSI2"}


class AmlError(Exception):
    """A malformed construct found while walking the byte stream."""


class NameString:
    """A decoded NameString: a root flag, a count of parent prefixes and segments."""

    __slots__ = ("root", "parents", "segments")

    def __init__(self, root, parents, segments):
        self.root = root
        self.parents = parents
        self.segments = segments

    def form(self):
        """Return the encoding form used to count name references."""
        if not self.segments:
            return "null"
        if self.root:
            return "absolute"
        if self.parents:
            return "parent-prefixed"
        if len(self.segments) == 1:
            return "single-segment"
        return "multi-segment-relative"

    def text(self):
        """Return the ASL spelling of the path."""
        prefix = "\\" if self.root else "^" * self.parents
        return prefix + ".".join(self.segments)


class Namespace:
    """The namespace of one machine: absolute paths mapped to an object kind."""

    def __init__(self):
        self.kinds = dict(PREDEFINED_ROOT)
        self.declarers = {}
        self.duplicated = set()
        self.argument_counts = {("_OSI",): 1}
        self.created = []
        self.unplaced_externals = []

    def creation_path(self, scope, name):
        """Return where a declaration of name in scope creates its object (5.3)."""
        if name.root:
            return tuple(name.segments)
        if name.parents:
            if name.parents > len(scope):
                return None
            return tuple(scope[: len(scope) - name.parents]) + tuple(name.segments)
        return tuple(scope) + tuple(name.segments)

    def resolve(self, scope, name):
        """Return the path name refers to from scope, applying the search rules of 5.3."""
        if not name.segments:
            return None
        if name.root or name.parents or len(name.segments) > 1:
            path = self.creation_path(scope, name)
            return path if path in self.kinds else None
        for depth in range(len(scope), -1, -1):
            path = tuple(scope[:depth]) + (name.segments[0],)
            if path in self.kinds:
                return path
        return None

    def declare(self, path, kind, argument_count=None):
        """Record a declaration: a real definition replaces an External one, and otherwise the first
        definition of a path stays and a duplicate is skipped (docs/acpi/specification.md §5.1)."""
        if path is None:
            return
        existing = self.kinds.get(path)
        if existing is not None and (existing != "External" or kind == "External"):
            return
        self.created.append((path, existing, self.argument_counts.get(path)))
        self.kinds[path] = kind
        self.argument_counts.pop(path, None)
        if argument_count is not None:
            self.argument_counts[path] = argument_count

    def declare_external(self, path, argument_count):
        """Record an External: placed now when its parent exists, otherwise once every table is
        declared (docs/acpi/specification.md §5.1)."""
        if path is None:
            return
        if path[:-1] in self.kinds or not path[:-1]:
            self.declare(path, "External", argument_count)
        else:
            self.unplaced_externals.append((path, argument_count))

    def place_externals(self):
        """Place the Externals whose parent was missing, the missing segments created as Externals."""
        for path, argument_count in self.unplaced_externals:
            for depth in range(1, len(path)):
                self.declare(path[:depth], "External")
            self.declare(path, "External", argument_count)
        self.unplaced_externals = []

    def forget_since(self, mark):
        """Undo the declarations made since the mark, the last first: what a method body declares ends with
        the body, and an External it replaced comes back (docs/acpi/specification.md §3.5, §5.1)."""
        for path, kind, argument_count in reversed(self.created[mark:]):
            self.argument_counts.pop(path, None)
            if kind is None:
                del self.kinds[path]
                continue
            self.kinds[path] = kind
            if argument_count is not None:
                self.argument_counts[path] = argument_count
        del self.created[mark:]

    def is_method(self, path):
        """Tell whether path names a control method (declared or External MethodObj)."""
        return path in self.argument_counts


class Census:
    """Counters filled while walking the definition blocks of one machine."""

    def __init__(self):
        self.opcodes_block = collections.Counter()
        self.opcodes_method = collections.Counter()
        self.opcodes_by_table = collections.defaultdict(collections.Counter)
        self.named_objects = collections.Counter()
        self.predefined = collections.defaultdict(collections.Counter)
        self.region_spaces = collections.Counter()
        self.regions_dynamic_operands = 0
        self.regions_in_methods = 0
        self.field_access = collections.Counter()
        self.field_lock = 0
        self.field_update = collections.Counter()
        self.field_elements = collections.Counter()
        self.field_units = 0
        self.field_unit_bits_max = 0
        self.field_units_wider_than_64 = 0
        self.field_units_unaligned = 0
        self.field_units_by_space = collections.Counter()
        self.methods = 0
        self.method_arguments = collections.Counter()
        self.methods_serialized = 0
        self.methods_sync_level = 0
        self.method_bytes_total = 0
        self.method_bytes_max = 0
        self.methods_without_return = 0
        self.methods_ending_without_return = 0
        self.term_depth_max = 0
        self.while_depth_max = 0
        self.osi_strings = collections.Counter()
        self.osc_uuids = collections.Counter()
        self.notify_values = collections.Counter()
        self.sleep_max_ms = 0
        self.stall_max_us = 0
        self.stalls_over_100_us = 0
        self.acquire_timeouts = collections.Counter()
        self.packages = 0
        self.package_elements_max = 0
        self.buffers = 0
        self.buffer_bytes_max = 0
        self.strings = 0
        self.string_bytes_max = 0
        self.resource_templates = 0
        self.resource_descriptors = collections.Counter()
        self.name_reference_forms = collections.Counter()
        self.name_references_unresolved = collections.Counter()
        self.method_calls = 0
        self.named_objects_in_methods = collections.Counter()
        self.predefined_root_references = collections.Counter()
        self.call_edges = collections.defaultdict(set)
        self.pkglength_bytes = collections.Counter()
        self.errors = []


def checksum_ok(table):
    """Return True when the bytes of a table sum to zero modulo 256."""
    return sum(table) % 256 == 0


def table_header(table):
    """Decode the 36-byte header of a definition block (20.2.1, 5.2.6)."""
    if len(table) < TABLE_HEADER_SIZE:
        raise AmlError("table shorter than its header")

    def text(start, end):
        return table[start:end].split(b"\0")[0].decode("ascii", "replace")

    return {
        "signature": text(0, 4),
        "length": int.from_bytes(table[4:8], "little"),
        "revision": table[8],
        "checksum_ok": checksum_ok(table),
        "oem_id": text(10, 16),
        "oem_table_id": text(16, 24),
        "oem_revision": int.from_bytes(table[24:28], "little"),
        "creator_id": text(28, 32),
        "creator_revision": int.from_bytes(table[32:36], "little"),
    }


def uuid_from_buffer(data):
    """Format 16 bytes laid out by the ASL ToUUID macro (19.6.142) as text."""
    first = data[3::-1].hex()
    second = data[5:3:-1].hex()
    third = data[7:5:-1].hex()
    return f"{first}-{second}-{third}-{data[8:10].hex()}-{data[10:16].hex()}".upper()


def resource_descriptors(data):
    """Return the descriptor names of a resource template (6.4), or None if data is not one."""
    names = []
    offset = 0
    while offset < len(data):
        tag = data[offset]
        if tag & 0x80:
            if offset + 3 > len(data):
                return None
            kind = LARGE_RESOURCES.get(tag & 0x7F)
            length = int.from_bytes(data[offset + 1:offset + 3], "little")
            body = data[offset + 3:offset + 3 + length]
            if kind is None or len(body) != length:
                return None
            if kind in ("DWordAddress", "WordAddress", "QWordAddress", "ExtendedAddress") and body:
                kind += "/" + ADDRESS_RESOURCE_TYPES.get(body[0], "Vendor")
            if kind == "SerialBusConnection" and len(body) >= 3:
                kind += "/" + SERIAL_BUS_TYPES.get(body[2], "Vendor")
            names.append(kind)
            offset += 3 + length
            continue
        kind = SMALL_RESOURCES.get((tag >> 3) & 0x0F)
        length = tag & 0x07
        if kind is None or offset + 1 + length > len(data):
            return None
        names.append(kind)
        offset += 1 + length
        if kind == "EndTag":
            if any(data[offset:]):
                return None
            return names
    return None


class Walker:
    """Walks the AML of one definition block by the grammar of chapter 20."""

    def __init__(self, data, start, namespace, census, label, counting):
        self.data = data
        self.start = start
        self.namespace = namespace
        self.census = census
        self.label = label
        self.counting = counting
        self.method_stack = []
        self.while_depth = 0

    def byte(self, offset):
        """Return the byte at offset, raising AmlError past the end."""
        if offset >= len(self.data):
            raise AmlError(f"read past the end at 0x{offset:x}")
        return self.data[offset]

    def integer(self, offset, width):
        """Return a little-endian unsigned integer of width bytes."""
        if offset + width > len(self.data):
            raise AmlError(f"integer of {width} bytes past the end at 0x{offset:x}")
        return int.from_bytes(self.data[offset:offset + width], "little")

    def package_length(self, offset, limit):
        """Decode a PkgLength (20.2.4); return (end of the package, offset after the encoding)."""
        lead = self.byte(offset)
        follow = lead >> 6
        if follow == 0:
            length = lead & 0x3F
        else:
            if lead & 0x30:
                raise AmlError(f"PkgLength lead byte 0x{lead:02x} has bits 4-5 set at 0x{offset:x}")
            length = lead & 0x0F
            for index in range(follow):
                length |= self.byte(offset + 1 + index) << (4 + 8 * index)
        if self.counting:
            self.census.pkglength_bytes[follow + 1] += 1
        end = offset + length
        if end > limit or length < follow + 1:
            raise AmlError(f"PkgLength 0x{length:x} at 0x{offset:x} leaves its enclosing block")
        return end, offset + follow + 1

    def name_segment(self, offset):
        """Decode one NameSeg (20.2.2)."""
        if offset + 4 > len(self.data):
            raise AmlError(f"NameSeg past the end at 0x{offset:x}")
        raw = self.data[offset:offset + 4]
        if not (0x41 <= raw[0] <= 0x5A or raw[0] == 0x5F):
            raise AmlError(f"bad NameSeg lead byte 0x{raw[0]:02x} at 0x{offset:x}")
        for value in raw[1:]:
            if not (0x41 <= value <= 0x5A or 0x30 <= value <= 0x39 or value == 0x5F):
                raise AmlError(f"bad NameSeg byte 0x{value:02x} at 0x{offset:x}")
        return raw.decode("ascii")

    def name_string(self, offset):
        """Decode a NameString (20.2.2); return (NameString, offset after it)."""
        root = False
        parents = 0
        if self.byte(offset) == 0x5C:
            root = True
            offset += 1
        else:
            while self.byte(offset) == 0x5E:
                parents += 1
                offset += 1
        lead = self.byte(offset)
        if lead == 0x00:
            return NameString(root, parents, ()), offset + 1
        if lead == 0x2E:
            count = 2
            offset += 1
        elif lead == 0x2F:
            count = self.byte(offset + 1)
            offset += 2
            if count == 0:
                raise AmlError(f"MultiNamePath with zero segments at 0x{offset:x}")
        else:
            count = 1
        segments = []
        for _ in range(count):
            segments.append(self.name_segment(offset))
            offset += 4
        return NameString(root, parents, tuple(segments)), offset

    @staticmethod
    def is_name_lead(value):
        """Tell whether a byte starts a NameString."""
        return value in (0x5C, 0x5E, 0x2E, 0x2F) or 0x41 <= value <= 0x5A or value == 0x5F

    def opcode(self, offset):
        """Return (mnemonic, offset after the opcode) or (None, offset) for a name."""
        lead = self.byte(offset)
        if lead == 0x5B:
            second = self.byte(offset + 1)
            name = EXTENDED_OPCODES.get(second)
            if name is None:
                raise AmlError(f"unknown extended opcode 0x5B 0x{second:02x} at 0x{offset:x}")
            return name, offset + 2
        if self.is_name_lead(lead):
            return None, offset
        name = ONE_BYTE_OPCODES.get(lead)
        if name is None:
            raise AmlError(f"unknown opcode 0x{lead:02x} at 0x{offset:x}")
        return name, offset + 1

    def count_opcode(self, name):
        """Count one opcode, split between definition block level and method bodies."""
        if not self.counting:
            return
        self.census.opcodes_by_table[self.label][name] += 1
        if self.method_stack:
            self.census.opcodes_method[name] += 1
        else:
            self.census.opcodes_block[name] += 1

    def record_error(self, offset, error):
        """Record a parse error once, during the counting pass."""
        if self.counting:
            self.census.errors.append(f"{self.label}+0x{offset:x}: {error}")

    def declare(self, scope, name, kind, argument_count=None):
        """Declare a named object and count it: the declaration pass declares what lies outside method
        bodies, and the counting pass what a method body declares, which ends with the body."""
        path = self.namespace.creation_path(scope, name)
        if self.method_stack or not self.counting:
            self.namespace.declare(path, kind, argument_count)
        if self.counting and path:
            self.census.named_objects[kind] += 1
            if self.method_stack:
                self.census.named_objects_in_methods[kind] += 1
            if path[-1].startswith("_"):
                self.census.predefined[path[-1]][kind] += 1
        return path

    def declare_scope(self, scope, name, kind, start, argument_count=None):
        """Declare a Device, ThermalZone, Processor, PowerResource or Method and tell whether its body is read: the
        declaration pass skips the body of a duplicate, and the counting pass, outside method bodies, the body of
        a construct naming a path a duplicate was refused for, unless it declared that path
        (docs/acpi/specification.md §5.1)."""
        path = self.namespace.creation_path(scope, name)
        existing = self.namespace.kinds.get(path) if path else None
        self.declare(scope, name, kind, argument_count)
        if self.method_stack or path is None:
            return path, True
        if not self.counting:
            if existing is not None and existing != "External":
                self.namespace.duplicated.add(path)
                return path, False
            self.namespace.declarers[path] = (self.label, start)
            return path, True
        return path, path not in self.namespace.duplicated or self.namespace.declarers.get(path) == (self.label, start)

    def count_for_disassembly(self, offset, end, scope, depth, method):
        """Walk a body no pass reads, a duplicate's, for the per-table opcode counts alone, which the iASL
        comparison reads: iASL disassembles every body."""
        scratch = Census()
        walker = Walker(self.data, self.start, self.namespace, scratch, self.label, True)
        walker.method_stack = self.method_stack + ([scope] if method else [])
        mark = len(self.namespace.created)
        if method:
            walker.last_statement(offset, end, scope, depth)
        else:
            walker.term_list(offset, end, scope, depth)
        self.namespace.forget_since(mark)
        self.census.opcodes_by_table[self.label].update(scratch.opcodes_by_table[self.label])

    def reference(self, scope, name):
        """Count a name reference and return the path it resolves to."""
        path = self.namespace.resolve(scope, name)
        if self.counting and name.segments:
            self.census.name_reference_forms[name.form()] += 1
            if path is None:
                self.census.name_references_unresolved[name.text()] += 1
            elif len(path) == 1 and path in PREDEFINED_ROOT:
                self.census.predefined_root_references[path[0]] += 1
        return path

    def term_list(self, offset, end, scope, depth):
        """Walk a TermList up to end; a malformed term skips the rest of the list."""
        while offset < end:
            try:
                offset = self.term(offset, end, scope, depth, statement=True)
            except (AmlError, RecursionError) as error:
                self.record_error(offset, error)
                return end
        return end

    def term(self, offset, end, scope, depth, statement=False):
        """Walk one term object or term argument and return the offset after it."""
        if depth > MAXIMUM_TERM_DEPTH:
            raise AmlError("term nesting deeper than the walker allows")
        if self.counting and depth > self.census.term_depth_max:
            self.census.term_depth_max = depth
        name, after = self.opcode(offset)
        if name is None:
            return self.name_term(offset, end, scope, depth)
        self.count_opcode(name)
        handler = getattr(self, "op_" + name, None)
        if handler is not None:
            return handler(after, end, scope, depth)
        if name in NO_OPERAND_OPCODES or name.startswith(("Local", "Arg")):
            return after
        if name in OPERAND_SHAPES:
            return self.operands(OPERAND_SHAPES[name], name, after, end, scope, depth)
        raise AmlError(f"no grammar for {name} at 0x{offset:x}")

    def name_term(self, offset, end, scope, depth):
        """Walk a NameString in a TermArg, SuperName or Target position: a reference or a
        MethodInvocation (docs/acpi/specification.md §4.2)."""
        name, after = self.name_string(offset)
        path = self.reference(scope, name)
        if path is not None and self.namespace.is_method(path):
            if self.counting:
                self.census.method_calls += 1
                if self.method_stack:
                    self.census.call_edges[self.method_stack[-1]].add(path)
            arguments = self.namespace.argument_counts[path]
            first_argument = after
            for _ in range(arguments):
                after = self.term(after, end, scope, depth + 1)
            if self.counting and path == ("_OSI",) and arguments == 1:
                self.record_osi_argument(first_argument)
            return after
        return after

    def record_osi_argument(self, offset):
        """Record the string literal passed to \\_OSI, if the argument is one."""
        if self.byte(offset) != 0x0D:
            return
        stop = self.data.find(b"\0", offset + 1)
        if stop < 0:
            return
        self.census.osi_strings[self.data[offset + 1:stop].decode("ascii", "replace")] += 1

    def super_name(self, offset, end, scope, depth, may_call):
        """Walk a SuperName or Target, where a method is called, or a SimpleName, where it never is
        (docs/acpi/specification.md §4.2)."""
        value = self.byte(offset)
        if value == 0x00:
            return offset + 1
        if self.is_name_lead(value):
            if may_call:
                return self.name_term(offset, end, scope, depth + 1)
            name, after = self.name_string(offset)
            self.reference(scope, name)
            return after
        return self.term(offset, end, scope, depth + 1)

    def operands(self, shape, opcode_name, offset, end, scope, depth):
        """Walk the fixed operands of an opcode described by a shape string."""
        for kind in shape:
            if kind == "T":
                start = offset
                offset = self.term(offset, end, scope, depth + 1)
                if self.counting:
                    self.observe_constant_operand(opcode_name, start)
            elif kind in "SRM":
                offset = self.super_name(offset, end, scope, depth, kind != "M")
            elif kind == "N":
                name, offset = self.name_string(offset)
                self.reference(scope, name)
            elif kind == "B":
                self.byte(offset)
                offset += 1
            elif kind == "W":
                timeout = self.integer(offset, 2)
                if self.counting and opcode_name == "Acquire":
                    self.census.acquire_timeouts["0xFFFF (forever)" if timeout >= 0xFFFF else "finite"] += 1
                offset += 2
            elif kind == "D":
                self.integer(offset, 4)
                offset += 4
        return offset

    def constant_at(self, offset):
        """Return the value of an integer constant term at offset, or None."""
        lead = self.byte(offset)
        widths = {0x0A: 1, 0x0B: 2, 0x0C: 4, 0x0E: 8}
        if lead in widths:
            return self.integer(offset + 1, widths[lead])
        if lead == 0x00:
            return 0
        if lead == 0x01:
            return 1
        return None

    def observe_constant_operand(self, opcode_name, offset):
        """Record the constant operands of Sleep, Stall and Notify."""
        if opcode_name not in ("Sleep", "Stall", "Notify"):
            return
        value = self.constant_at(offset)
        if value is None:
            return
        if opcode_name == "Sleep":
            self.census.sleep_max_ms = max(self.census.sleep_max_ms, value)
        elif opcode_name == "Stall":
            self.census.stall_max_us = max(self.census.stall_max_us, value)
            if value > 100:
                self.census.stalls_over_100_us += 1
        else:
            self.census.notify_values[f"0x{value:02X}"] += 1

    def op_BytePrefix(self, offset, end, scope, depth):
        self.integer(offset, 1)
        return offset + 1

    def op_WordPrefix(self, offset, end, scope, depth):
        self.integer(offset, 2)
        return offset + 2

    def op_DWordPrefix(self, offset, end, scope, depth):
        self.integer(offset, 4)
        return offset + 4

    def op_QWordPrefix(self, offset, end, scope, depth):
        self.integer(offset, 8)
        return offset + 8

    def op_StringPrefix(self, offset, end, scope, depth):
        stop = self.data.find(b"\0", offset, end)
        if stop < 0:
            raise AmlError(f"unterminated String at 0x{offset:x}")
        if self.counting:
            self.census.strings += 1
            self.census.string_bytes_max = max(self.census.string_bytes_max, stop - offset)
        return stop + 1

    def op_Alias(self, offset, end, scope, depth):
        source, offset = self.name_string(offset)
        alias, offset = self.name_string(offset)
        target = self.namespace.resolve(scope, source)
        if target is None and (not self.counting or self.method_stack):
            return offset
        mark = len(self.namespace.created)
        path = self.declare(scope, alias, "Alias")
        if path is not None and len(self.namespace.created) > mark:
            self.namespace.kinds[path] = self.namespace.kinds.get(target, "Alias")
            if self.namespace.is_method(target):
                self.namespace.argument_counts[path] = self.namespace.argument_counts[target]
        return offset

    def op_Name(self, offset, end, scope, depth):
        name, offset = self.name_string(offset)
        self.declare(scope, name, "Name")
        return self.data_object(offset, end, scope, depth + 1)

    def data_object(self, offset, end, scope, depth):
        """Walk a DataRefObject or a package element: names are references, not calls."""
        value = self.byte(offset)
        if self.is_name_lead(value):
            name, after = self.name_string(offset)
            self.reference(scope, name)
            return after
        return self.term(offset, end, scope, depth)

    def op_External(self, offset, end, scope, depth):
        name, offset = self.name_string(offset)
        object_type = self.byte(offset)
        argument_count = self.byte(offset + 1) & 0x07
        if not self.counting or self.method_stack:
            self.namespace.declare_external(self.namespace.creation_path(scope, name),
                                            argument_count if object_type == 8 else None)
        if self.counting:
            self.census.named_objects["External"] += 1
        return offset + 2

    def scoped_body(self, kind, offset, end, scope, depth, prefix_bytes=0):
        """Walk Scope, Device, PowerResource, Processor and ThermalZone."""
        start = offset
        block_end, offset = self.package_length(offset, end)
        name, offset = self.name_string(offset)
        reads = True
        if kind == "Scope":
            path = self.namespace.resolve(scope, name) if name.segments else self.namespace.creation_path(scope, name)
            if path is None and not self.counting:
                return block_end
            path = scope if path is None else path
            if self.counting:
                self.census.named_objects["Scope"] += 1
        else:
            path, reads = self.declare_scope(scope, name, kind, start)
        if prefix_bytes:
            self.integer(offset, prefix_bytes)
            offset += prefix_bytes
        if path is None:
            raise AmlError(f"{kind} {name.text()} climbs above the root")
        if not reads:
            if self.counting:
                self.count_for_disassembly(offset, block_end, path, depth + 1, False)
            return block_end
        return self.term_list(offset, block_end, path, depth + 1)

    def op_Scope(self, offset, end, scope, depth):
        return self.scoped_body("Scope", offset, end, scope, depth)

    def op_Device(self, offset, end, scope, depth):
        return self.scoped_body("Device", offset, end, scope, depth)

    def op_ThermalZone(self, offset, end, scope, depth):
        return self.scoped_body("ThermalZone", offset, end, scope, depth)

    def op_PowerResource(self, offset, end, scope, depth):
        return self.scoped_body("PowerResource", offset, end, scope, depth, prefix_bytes=3)

    def op_Processor(self, offset, end, scope, depth):
        return self.scoped_body("Processor", offset, end, scope, depth, prefix_bytes=6)

    def op_Method(self, offset, end, scope, depth):
        start = offset
        block_end, offset = self.package_length(offset, end)
        name, offset = self.name_string(offset)
        flags = self.byte(offset)
        offset += 1
        path, reads = self.declare_scope(scope, name, "Method", start, flags & 0x07)
        if path is None:
            raise AmlError(f"Method {name.text()} climbs above the root")
        if not self.counting:
            return block_end
        if not reads:
            self.count_for_disassembly(offset, block_end, path, depth + 1, True)
            return block_end
        census = self.census
        census.methods += 1
        census.method_arguments[flags & 0x07] += 1
        census.methods_serialized += 1 if flags & 0x08 else 0
        census.methods_sync_level += 1 if flags >> 4 else 0
        census.method_bytes_total += block_end - offset
        census.method_bytes_max = max(census.method_bytes_max, block_end - offset)
        before_returns = census.opcodes_method["Return"]
        mark = len(self.namespace.created)
        self.method_stack.append(path)
        last_term = self.last_statement(offset, block_end, path, depth + 1)
        self.method_stack.pop()
        self.namespace.forget_since(mark)
        if census.opcodes_method["Return"] == before_returns:
            census.methods_without_return += 1
        if last_term != "Return":
            census.methods_ending_without_return += 1
        if path[-1] == "_OSC":
            self.collect_uuids(offset, block_end)
        return block_end

    def last_statement(self, offset, end, scope, depth):
        """Walk a method body and return the mnemonic of its last top-level statement."""
        last = None
        while offset < end:
            try:
                last, _ = self.opcode(offset)
                offset = self.term(offset, end, scope, depth, statement=True)
            except AmlError as error:
                self.record_error(offset, error)
                return None
        return last

    def collect_uuids(self, offset, end):
        """Record every 16-byte constant buffer inside an _OSC method as a UUID."""
        for match in re.finditer(rb"\x11\x13\x0a\x10", self.data[offset:end]):
            start = offset + match.end()
            if start + 16 <= end:
                self.census.osc_uuids[uuid_from_buffer(self.data[start:start + 16])] += 1

    def op_Buffer(self, offset, end, scope, depth):
        block_end, offset = self.package_length(offset, end)
        size_start = offset
        offset = self.term(offset, block_end, scope, depth + 1)
        if self.counting:
            self.census.buffers += 1
            declared = self.constant_at(size_start)
            payload = self.data[offset:block_end]
            if declared is not None:
                self.census.buffer_bytes_max = max(self.census.buffer_bytes_max, declared)
            descriptors = resource_descriptors(payload) if payload else None
            if descriptors:
                self.census.resource_templates += 1
                for descriptor in descriptors:
                    self.census.resource_descriptors[descriptor] += 1
        return block_end

    def package_elements(self, offset, end, scope, depth):
        """Walk a PackageElementList (20.2.5.4)."""
        count = 0
        while offset < end:
            offset = self.data_object(offset, end, scope, depth + 1)
            count += 1
        if self.counting:
            self.census.packages += 1
            self.census.package_elements_max = max(self.census.package_elements_max, count)
        return end

    def op_Package(self, offset, end, scope, depth):
        block_end, offset = self.package_length(offset, end)
        self.byte(offset)
        return self.package_elements(offset + 1, block_end, scope, depth)

    def op_VarPackage(self, offset, end, scope, depth):
        block_end, offset = self.package_length(offset, end)
        offset = self.term(offset, block_end, scope, depth + 1)
        return self.package_elements(offset, block_end, scope, depth)

    def op_If(self, offset, end, scope, depth):
        block_end, offset = self.package_length(offset, end)
        offset = self.term(offset, block_end, scope, depth + 1)
        return self.term_list(offset, block_end, scope, depth + 1)

    def op_Else(self, offset, end, scope, depth):
        block_end, offset = self.package_length(offset, end)
        return self.term_list(offset, block_end, scope, depth + 1)

    def op_While(self, offset, end, scope, depth):
        block_end, offset = self.package_length(offset, end)
        offset = self.term(offset, block_end, scope, depth + 1)
        self.while_depth += 1
        if self.counting:
            self.census.while_depth_max = max(self.census.while_depth_max, self.while_depth)
        try:
            return self.term_list(offset, block_end, scope, depth + 1)
        finally:
            self.while_depth -= 1

    def op_OperationRegion(self, offset, end, scope, depth):
        name, offset = self.name_string(offset)
        space = self.byte(offset)
        offset += 1
        path = self.declare(scope, name, "OperationRegion")
        dynamic = self.constant_at(offset) is None
        offset = self.term(offset, end, scope, depth + 1)
        dynamic = dynamic or self.constant_at(offset) is None
        offset = self.term(offset, end, scope, depth + 1)
        if path is not None:
            self.namespace.kinds[path] = "OperationRegion:" + str(space)
        if self.counting:
            label = REGION_SPACES.get(space, "OEM" if space >= 0x80 else f"Reserved 0x{space:02X}")
            self.census.region_spaces[label] += 1
            self.census.regions_dynamic_operands += 1 if dynamic else 0
            self.census.regions_in_methods += 1 if self.method_stack else 0
        return offset

    def op_DataRegion(self, offset, end, scope, depth):
        name, offset = self.name_string(offset)
        self.declare(scope, name, "DataRegion")
        for _ in range(3):
            offset = self.term(offset, end, scope, depth + 1)
        return offset

    def op_Mutex(self, offset, end, scope, depth):
        name, offset = self.name_string(offset)
        self.declare(scope, name, "Mutex")
        self.byte(offset)
        return offset + 1

    def op_Event(self, offset, end, scope, depth):
        name, offset = self.name_string(offset)
        self.declare(scope, name, "Event")
        return offset

    def create_buffer_field(self, kind, operands, offset, end, scope, depth):
        """Walk CreateBitField, CreateByteField, ... and CreateField."""
        for _ in range(operands):
            offset = self.term(offset, end, scope, depth + 1)
        name, offset = self.name_string(offset)
        self.declare(scope, name, "BufferField")
        return offset

    def op_CreateBitField(self, offset, end, scope, depth):
        return self.create_buffer_field("CreateBitField", 2, offset, end, scope, depth)

    def op_CreateByteField(self, offset, end, scope, depth):
        return self.create_buffer_field("CreateByteField", 2, offset, end, scope, depth)

    def op_CreateWordField(self, offset, end, scope, depth):
        return self.create_buffer_field("CreateWordField", 2, offset, end, scope, depth)

    def op_CreateDWordField(self, offset, end, scope, depth):
        return self.create_buffer_field("CreateDWordField", 2, offset, end, scope, depth)

    def op_CreateQWordField(self, offset, end, scope, depth):
        return self.create_buffer_field("CreateQWordField", 2, offset, end, scope, depth)

    def op_CreateField(self, offset, end, scope, depth):
        return self.create_buffer_field("CreateField", 3, offset, end, scope, depth)

    def region_space_of(self, scope, name):
        """Return the address space name of the region a Field names, when known."""
        path = self.namespace.resolve(scope, name)
        kind = self.namespace.kinds.get(path, "")
        if kind.startswith("OperationRegion:"):
            space = int(kind.split(":")[1])
            return REGION_SPACES.get(space, "OEM" if space >= 0x80 else "Reserved")
        return "unknown"

    def field_list(self, flags, offset, end, scope, space):
        """Walk a FieldList (20.2.5.2) and count its elements and units."""
        access = flags & 0x0F
        if self.counting:
            self.census.field_access[ACCESS_TYPES.get(access, f"Reserved {access}")] += 1
            self.census.field_lock += 1 if flags & 0x10 else 0
            self.census.field_update[UPDATE_RULES.get((flags >> 5) & 0x03, "Reserved")] += 1
        bit = 0
        while offset < end:
            lead = self.byte(offset)
            if lead == 0x00:
                length, offset = self.field_length(offset + 1, end)
                bit += length
                if self.counting:
                    self.census.field_elements["ReservedField (Offset)"] += 1
            elif lead == 0x01:
                access = self.byte(offset + 1) & 0x0F
                self.byte(offset + 2)
                offset += 3
                if self.counting:
                    self.census.field_elements["AccessField (AccessAs)"] += 1
                    self.census.field_access["AccessAs " + ACCESS_TYPES.get(access, "Reserved")] += 1
            elif lead == 0x03:
                self.integer(offset + 1, 3)
                offset += 4
                if self.counting:
                    self.census.field_elements["ExtendedAccessField"] += 1
            elif lead == 0x02:
                offset = self.connect_field(offset + 1, end, scope)
                if self.counting:
                    self.census.field_elements["ConnectField (Connection)"] += 1
            else:
                segment = self.name_segment(offset)
                length, offset = self.field_length(offset + 4, end)
                self.declare(scope, NameString(False, 0, (segment,)), "FieldUnit")
                if self.counting:
                    self.count_field_unit(bit, length, access, space)
                bit += length
        return end

    def field_length(self, offset, end):
        """Decode the PkgLength-encoded bit length of a field element."""
        lead = self.byte(offset)
        follow = lead >> 6
        if follow == 0:
            return lead & 0x3F, offset + 1
        length = lead & 0x0F
        for index in range(follow):
            length |= self.byte(offset + 1 + index) << (4 + 8 * index)
        return length, offset + follow + 1

    def connect_field(self, offset, end, scope):
        """Walk a ConnectField: a NameString or BufferData, which is a DefBuffer term (20.2.5.2)."""
        if self.byte(offset) == 0x11:
            return self.term(offset, end, scope, 0)
        name, offset = self.name_string(offset)
        self.reference(scope, name)
        return offset

    def count_field_unit(self, bit, length, access, space):
        """Count one field unit, its width and whether it needs more than one access."""
        census = self.census
        census.field_units += 1
        census.field_units_by_space[space] += 1
        census.field_unit_bits_max = max(census.field_unit_bits_max, length)
        census.field_units_wider_than_64 += 1 if length > 64 else 0
        width = ACCESS_WIDTH_BITS.get(access, 8)
        if length and (bit // width) != ((bit + length - 1) // width):
            census.field_units_unaligned += 1

    def op_Field(self, offset, end, scope, depth):
        block_end, offset = self.package_length(offset, end)
        region, offset = self.name_string(offset)
        self.reference(scope, region)
        flags = self.byte(offset)
        if self.counting:
            self.census.named_objects["Field"] += 1
        return self.field_list(flags, offset + 1, block_end, scope, self.region_space_of(scope, region))

    def op_IndexField(self, offset, end, scope, depth):
        block_end, offset = self.package_length(offset, end)
        index_name, offset = self.name_string(offset)
        data_name, offset = self.name_string(offset)
        self.reference(scope, index_name)
        self.reference(scope, data_name)
        flags = self.byte(offset)
        if self.counting:
            self.census.named_objects["IndexField"] += 1
        return self.field_list(flags, offset + 1, block_end, scope, "IndexField")

    def op_BankField(self, offset, end, scope, depth):
        block_end, offset = self.package_length(offset, end)
        region, offset = self.name_string(offset)
        bank, offset = self.name_string(offset)
        self.reference(scope, region)
        self.reference(scope, bank)
        offset = self.term(offset, block_end, scope, depth + 1)
        flags = self.byte(offset)
        if self.counting:
            self.census.named_objects["BankField"] += 1
        return self.field_list(flags, offset + 1, block_end, scope, self.region_space_of(scope, region))


def tables_from_acpidump(text):
    """Extract (signature, bytes) of every table in an acpidump text dump."""
    tables = []
    current = None
    for line in text.splitlines():
        header = re.match(r"^([A-Za-z0-9_!]{4}) @ 0x[0-9A-Fa-f]+\s*$", line)
        if header:
            current = [header.group(1), bytearray()]
            tables.append(current)
            continue
        row = re.match(r"^\s*[0-9A-Fa-f]+: ((?:[0-9A-Fa-f]{2} ){1,16})", line)
        if row and current is not None:
            current[1].extend(bytes.fromhex(row.group(1)))
    return [(signature, bytes(data)) for signature, data in tables]


def load_inputs(path):
    """Return the (label, table bytes) of the definition blocks found in one input file."""
    with open(path, "rb") as handle:
        raw = handle.read()
    if raw[:4] in (b"DSDT", b"SSDT") and not re.match(rb"^[A-Za-z0-9_!]{4} @ 0x", raw):
        return [(os.path.basename(path), raw)]
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        return []
    blocks = []
    serial = collections.Counter()
    for signature, data in tables_from_acpidump(text):
        if signature not in ("DSDT", "SSDT") or data[:4] != signature.encode():
            continue
        serial[signature] += 1
        blocks.append((f"{os.path.basename(path)}:{signature}{serial[signature]}", data))
    return blocks


def static_call_depth(edges):
    """Return (longest acyclic call chain, number of methods on a call cycle)."""
    memo = {}
    on_cycle = set()

    def depth(node, trail):
        if node in trail:
            on_cycle.update(trail[trail.index(node):])
            return 0
        if node in memo:
            return memo[node]
        trail.append(node)
        best = 0
        for callee in edges.get(node, ()):
            best = max(best, 1 + depth(callee, trail))
        trail.pop()
        memo[node] = best
        return best

    longest = 0
    for node in list(edges):
        longest = max(longest, 1 + depth(node, []))
    return longest, len(on_cycle)


def census_of_machine(name, blocks, top):
    """Load every definition block of one machine into one namespace and count its AML: the DSDT, then each
    SSDT in the dump's order, declared in one pass (docs/acpi/specification.md §3.8 step 1, §5.1), then counted."""
    namespace = Namespace()
    census = Census()
    tables = []
    parsed = []
    for label, data in blocks:
        try:
            header = table_header(data)
        except AmlError as error:
            census.errors.append(f"{label}: {error}")
            continue
        length = min(header["length"], len(data))
        header["file"] = label
        header["truncated"] = header["length"] > len(data)
        tables.append(header)
        parsed.append((label, data[:length], header))
    parsed.sort(key=lambda table: table[2]["signature"] != "DSDT")
    for counting in (False, True):
        for label, data, _ in parsed:
            walker = Walker(data, TABLE_HEADER_SIZE, namespace, census, label, counting)
            walker.term_list(TABLE_HEADER_SIZE, len(data), (), 0)
        namespace.place_externals()
    dsdt = [table for table in tables if table["signature"] == "DSDT"]
    integer_bits = None
    if dsdt:
        integer_bits = 32 if dsdt[0]["revision"] < 2 else 64
    call_depth, methods_on_cycles = static_call_depth(census.call_edges)
    opcodes_total = census.opcodes_block + census.opcodes_method
    return {
        "machine": name,
        "tables": tables,
        "aml_bytes": sum(len(data) - TABLE_HEADER_SIZE for _, data, _ in parsed),
        "integer_bits_from_dsdt_revision": integer_bits,
        "opcodes": dict(opcodes_total.most_common()),
        "opcodes_by_table": {label: dict(counts) for label, counts in census.opcodes_by_table.items()},
        "opcodes_at_definition_block_level": dict(census.opcodes_block.most_common()),
        "opcodes_in_method_bodies": dict(census.opcodes_method.most_common()),
        "opcodes_deprecated_or_reserved_seen": {
            opcode: {"count": opcodes_total[opcode], "status": status}
            for opcode, status in SPECIFICATION_STATUS.items() if opcodes_total[opcode]
        },
        "named_objects": dict(census.named_objects.most_common()),
        "named_objects_created_inside_methods": dict(census.named_objects_in_methods.most_common()),
        "switch_temporaries_T_x": sum(
            sum(kinds.values()) for seg, kinds in census.predefined.items()
            if re.match(r"^_T_[0-9A-Z]$", seg)
        ),
        "predefined_names": {
            seg: dict(kinds) for seg, kinds in sorted(census.predefined.items())
        },
        "interesting_names": {
            seg: sum(census.predefined[seg].values()) for seg in INTERESTING_NAMES
        },
        "gpe_methods": sum(
            sum(kinds.values()) for seg, kinds in census.predefined.items()
            if re.match(r"^_[LEQW][0-9A-F]{2}$", seg)
        ),
        "operation_region_spaces": dict(census.region_spaces.most_common()),
        "operation_regions_with_computed_operands": census.regions_dynamic_operands,
        "operation_regions_declared_in_methods": census.regions_in_methods,
        "field_access": dict(census.field_access.most_common()),
        "field_lock_rule_lock": census.field_lock,
        "field_update_rules": dict(census.field_update.most_common()),
        "field_elements": dict(census.field_elements.most_common()),
        "field_units": census.field_units,
        "field_units_by_space": dict(census.field_units_by_space.most_common()),
        "field_unit_bits_max": census.field_unit_bits_max,
        "field_units_wider_than_64_bits": census.field_units_wider_than_64,
        "field_units_spanning_two_accesses": census.field_units_unaligned,
        "methods": {
            "count": census.methods,
            "by_argument_count": {str(k): v for k, v in sorted(census.method_arguments.items())},
            "serialized": census.methods_serialized,
            "nonzero_sync_level": census.methods_sync_level,
            "body_bytes_total": census.method_bytes_total,
            "body_bytes_max": census.method_bytes_max,
            "without_any_return": census.methods_without_return,
            "last_statement_not_return": census.methods_ending_without_return,
            "calls_resolved": census.method_calls,
            "static_call_depth_max": call_depth,
            "methods_on_call_cycles": methods_on_cycles,
        },
        "term_nesting_depth_max": census.term_depth_max,
        "while_nesting_depth_max": census.while_depth_max,
        "osi_strings": dict(census.osi_strings.most_common()),
        "osc_uuids": dict(census.osc_uuids.most_common()),
        "notify_values": dict(census.notify_values.most_common()),
        "sleep_constant_max_ms": census.sleep_max_ms,
        "stall_constant_max_us": census.stall_max_us,
        "stalls_over_100_us": census.stalls_over_100_us,
        "acquire_timeouts": dict(census.acquire_timeouts),
        "packages": {"count": census.packages, "elements_max": census.package_elements_max},
        "buffers": {"count": census.buffers, "declared_bytes_max": census.buffer_bytes_max},
        "strings": {"count": census.strings, "bytes_max": census.string_bytes_max},
        "resource_templates": census.resource_templates,
        "resource_descriptors": dict(census.resource_descriptors.most_common()),
        "name_reference_forms": dict(census.name_reference_forms.most_common()),
        "name_references_unresolved": sum(census.name_references_unresolved.values()),
        "name_references_unresolved_top": dict(census.name_references_unresolved.most_common(top)),
        "predefined_root_references": dict(census.predefined_root_references),
        "pkglength_encoding_bytes": {str(k): v for k, v in sorted(census.pkglength_bytes.items())},
        "parse_errors": len(census.errors),
        "parse_errors_first": census.errors[:top],
    }


def summarize(machines):
    """Total the per-machine censuses and count how many machines use each feature."""
    totals = collections.Counter()
    prevalence = collections.Counter()
    keys = ("opcodes", "named_objects", "named_objects_created_inside_methods", "interesting_names", "operation_region_spaces",
            "field_access", "resource_descriptors", "osi_strings", "osc_uuids",
            "notify_values", "field_elements")
    sections = {key: collections.Counter() for key in keys}
    machines_with = {key: collections.Counter() for key in keys}
    maxima = collections.Counter()
    for machine in machines:
        for key in keys:
            for item, value in machine[key].items():
                sections[key][item] += value
                if value:
                    machines_with[key][item] += 1
        for key in ("term_nesting_depth_max", "while_nesting_depth_max", "field_unit_bits_max",
                    "sleep_constant_max_ms", "stall_constant_max_us"):
            maxima[key] = max(maxima[key], machine[key])
        for key in ("body_bytes_max", "static_call_depth_max"):
            maxima["method_" + key] = max(maxima["method_" + key], machine["methods"][key])
        maxima["package_elements_max"] = max(maxima["package_elements_max"], machine["packages"]["elements_max"])
        totals["machines"] += 1
        totals["aml_bytes"] += machine["aml_bytes"]
        totals["methods"] += machine["methods"]["count"]
        totals["methods_without_any_return"] += machine["methods"]["without_any_return"]
        totals["methods_last_statement_not_return"] += machine["methods"]["last_statement_not_return"]
        totals["methods_serialized"] += machine["methods"]["serialized"]
        totals["field_units"] += machine["field_units"]
        totals["field_units_spanning_two_accesses"] += machine["field_units_spanning_two_accesses"]
        totals["name_references_unresolved"] += machine["name_references_unresolved"]
        totals["parse_errors"] += machine["parse_errors"]
        totals["switch_temporaries_T_x"] += machine["switch_temporaries_T_x"]
        prevalence["machines_with_parse_errors"] += 1 if machine["parse_errors"] else 0
        prevalence["machines_with_32_bit_integers"] += 1 if machine["integer_bits_from_dsdt_revision"] == 32 else 0
    return {
        "totals": dict(totals),
        "maxima": dict(maxima),
        "prevalence": dict(prevalence),
        "sum": {key: dict(counter.most_common()) for key, counter in sections.items()},
        "machines_with": {key: dict(counter.most_common()) for key, counter in machines_with.items()},
    }


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("files", nargs="*",
                        help="raw DSDT/SSDT files or acpidump text dumps, one machine each; a directory adds each file in it")
    parser.add_argument("--machine", action="append", default=[],
                        help="NAME=FILE[,FILE...]: load several raw tables into one namespace")
    parser.add_argument("--per-machine", action="store_true", help="include every machine's census")
    parser.add_argument("--top", type=int, default=20, help="length of the top-N lists")
    options = parser.parse_args(arguments)
    sys.setrecursionlimit(max(sys.getrecursionlimit(), 8 * MAXIMUM_TERM_DEPTH + 1000))
    groups = []
    for path in options.files:
        if os.path.isdir(path):
            groups.extend((os.path.join(path, entry), [os.path.join(path, entry)])
                          for entry in sorted(os.listdir(path))
                          if os.path.isfile(os.path.join(path, entry)))
        else:
            groups.append((path, [path]))
    for spec in options.machine:
        name, _, paths = spec.partition("=")
        groups.append((name, [path for path in paths.split(",") if path]))
    machines = []
    for name, paths in groups:
        blocks = []
        for path in paths:
            blocks.extend(load_inputs(path))
        if not blocks:
            continue
        machines.append(census_of_machine(name, blocks, options.top))
    result = {
        "tool": "aml_census.py",
        "grammar": "ACPI 6.5 chapter 20 (UEFI/ACPI-Specification-Release 5dabe6f)",
        "summary": summarize(machines),
    }
    if options.per_machine:
        result["machines"] = machines
    json.dump(result, sys.stdout, indent=1, sort_keys=False)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
