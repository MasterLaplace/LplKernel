"""Census of the AML and the static tables of linuxhw/ACPI acpidump reports.

Usage: python3 -I real_firmware_census.py <mapping.tsv> <data-dir> <out.json>

mapping.tsv holds "<index>\t<repository path>" lines; <data-dir>/<index>.bin is the
acpidump hex text of that report (the format of linuxhw/ACPI's .bin files).

The AML walker follows the grammar of ACPI 6.6 chapter 20 (opcode values as in
ACPICA source/include/amlcode.h, argument kinds as in ACPICA
source/include/acopcode.h: a name in a SuperName or Target position may be a
method call, a name in a SimpleName position never is). Method invocations need
the argument count of the method, so every machine is walked three times: twice
to declare the namespace (the second walk resolves the calls made at table
load), then once to measure. A term that cannot be parsed abandons the rest of
the enclosing PkgLength-bounded block and is counted as a parse error.
"""
import collections
import json
import re
import sys

sys.setrecursionlimit(20000)

HEADER_RE = re.compile(r"^(\S.{0,7}?) @ 0x([0-9A-Fa-f]+)\s*$")
LINE_RE = re.compile(r"^\s+([0-9A-Fa-f]{4,8}):((?: [0-9A-Fa-f]{2}){1,16})")

SPACES = {
    0: "SystemMemory", 1: "SystemIO", 2: "PCI_Config", 3: "EmbeddedControl",
    4: "SMBus", 5: "SystemCMOS", 6: "PciBarTarget", 7: "IPMI",
    8: "GeneralPurposeIo", 9: "GenericSerialBus", 10: "PCC", 11: "PlatformRtMechanism",
    0x7F: "FFixedHW",
}
ACCESS = {0: "AnyAcc", 1: "ByteAcc", 2: "WordAcc", 3: "DWordAcc", 4: "QWordAcc", 5: "BufferAcc"}
UPDATE = {0: "Preserve", 1: "WriteAsOnes", 2: "WriteAsZeros", 3: "Reserved3"}

SIMPLE = {
    0x70: ("Store", "TS"), 0x71: ("RefOf", "M"), 0x72: ("Add", "TTR"),
    0x73: ("Concatenate", "TTR"), 0x74: ("Subtract", "TTR"), 0x75: ("Increment", "S"),
    0x76: ("Decrement", "S"), 0x77: ("Multiply", "TTR"), 0x78: ("Divide", "TTRR"),
    0x79: ("ShiftLeft", "TTR"), 0x7A: ("ShiftRight", "TTR"), 0x7B: ("And", "TTR"),
    0x7C: ("Nand", "TTR"), 0x7D: ("Or", "TTR"), 0x7E: ("Nor", "TTR"), 0x7F: ("Xor", "TTR"),
    0x80: ("Not", "TR"), 0x81: ("FindSetLeftBit", "TR"), 0x82: ("FindSetRightBit", "TR"),
    0x83: ("DerefOf", "S"), 0x84: ("ConcatenateResTemplate", "TTR"), 0x85: ("Mod", "TTR"),
    0x86: ("Notify", "ST"), 0x87: ("SizeOf", "S"), 0x88: ("Index", "TTR"),
    0x89: ("Match", "TBTBTT"), 0x8A: ("CreateDWordField", "TTN"),
    0x8B: ("CreateWordField", "TTN"), 0x8C: ("CreateByteField", "TTN"),
    0x8D: ("CreateBitField", "TTN"), 0x8E: ("ObjectType", "M"),
    0x8F: ("CreateQWordField", "TTN"), 0x90: ("LAnd", "TT"), 0x91: ("LOr", "TT"),
    0x92: ("LNot", "T"), 0x93: ("LEqual", "TT"), 0x94: ("LGreater", "TT"),
    0x95: ("LLess", "TT"), 0x96: ("ToBuffer", "TR"), 0x97: ("ToDecimalString", "TR"),
    0x98: ("ToHexString", "TR"), 0x99: ("ToInteger", "TR"), 0x9C: ("ToString", "TTR"),
    0x9D: ("CopyObject", "TM"), 0x9E: ("Mid", "TTTR"), 0x9F: ("Continue", ""),
    0xA3: ("Noop", ""), 0xA4: ("Return", "T"), 0xA5: ("Break", ""), 0xCC: ("BreakPoint", ""),
    0x06: ("Alias", "NN"), 0x15: ("External", "NBB"),
}
EXTENDED = {
    0x01: ("Mutex", "NB"), 0x02: ("Event", "N"), 0x12: ("CondRefOf", "MR"),
    0x13: ("CreateField", "TTTN"), 0x1F: ("LoadTable", "TTTTTT"), 0x20: ("Load", "NS"),
    0x21: ("Stall", "T"), 0x22: ("Sleep", "T"), 0x23: ("Acquire", "SW"),
    0x24: ("Signal", "S"), 0x25: ("Wait", "ST"), 0x26: ("Reset", "S"),
    0x27: ("Release", "S"), 0x28: ("FromBCD", "TR"), 0x29: ("ToBCD", "TR"),
    0x2A: ("Unload", "S"), 0x30: ("Revision", ""), 0x31: ("Debug", ""),
    0x32: ("Fatal", "BDT"), 0x33: ("Timer", ""), 0x88: ("DataRegion", "NTTT"),
}
CREATE_FIELD_OPS = {0x8A, 0x8B, 0x8C, 0x8D, 0x8F}

SMALL_RESOURCES = {
    0x4: "IRQ", 0x5: "DMA", 0x6: "StartDependentFn", 0x7: "EndDependentFn", 0x8: "IO",
    0x9: "FixedIO", 0xA: "FixedDMA", 0xE: "VendorShort", 0xF: "EndTag",
}
LARGE_RESOURCES = {
    0x01: "Memory24", 0x02: "GenericRegister", 0x04: "VendorLarge", 0x05: "Memory32",
    0x06: "Memory32Fixed", 0x07: "DWordSpace", 0x08: "WordSpace", 0x09: "ExtendedInterrupt",
    0x0A: "QWordSpace", 0x0B: "ExtendedSpace", 0x0C: "GpioConnection", 0x0D: "PinFunction",
    0x0E: "SerialBus", 0x0F: "PinConfig", 0x10: "PinGroup", 0x11: "PinGroupFunction",
    0x12: "PinGroupConfig", 0x13: "ClockInput",
}
SERIAL_BUS = {1: "I2cSerialBus", 2: "SpiSerialBus", 3: "UartSerialBus", 4: "Csi2Bus"}

NAME_LEAD = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ_")
NAME_CHARS = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789")
NAME_START = NAME_LEAD | {0x5C, 0x5E, 0x2E, 0x2F}
GPE_METHOD_RE = re.compile(r"^_[LE][0-9A-F]{2}$")


class ParseError(Exception):
    pass


def read_acpidump(path):
    tables = []
    current = None
    with open(path, "r", encoding="latin-1") as dump:
        for line in dump:
            header = HEADER_RE.match(line)
            if header:
                current = {"label": header.group(1).strip(), "bytes": bytearray(), "gaps": 0}
                tables.append(current)
                continue
            row = LINE_RE.match(line)
            if row and current is not None:
                if int(row.group(1), 16) != len(current["bytes"]):
                    current["gaps"] += 1
                current["bytes"] += bytes.fromhex(row.group(2).replace(" ", ""))
    return tables


def text(raw):
    return raw.decode("latin-1").rstrip("\x00").rstrip()


def describe_table(entry):
    data = bytes(entry["bytes"])
    label = entry["label"]
    info = {"label": label, "size": len(data), "gaps": entry["gaps"]}
    if label.startswith("RSD PTR") or data[:8] == b"RSD PTR ":
        info["signature"] = "RSDP"
        info["revision"] = data[15] if len(data) > 15 else None
        return info, data
    info["signature"] = text(data[0:4]) if len(data) >= 4 else label
    if info["signature"] == "FACS" or len(data) < 36:
        return info, data
    length = int.from_bytes(data[4:8], "little")
    info.update({
        "length": length,
        "revision": data[8],
        "checksum_ok": sum(data[:length]) % 256 == 0,
        "oem_id": text(data[10:16]),
        "oem_table_id": text(data[16:24]),
        "oem_revision": int.from_bytes(data[24:28], "little"),
        "creator_id": text(data[28:32]),
        "creator_revision": int.from_bytes(data[32:36], "little"),
    })
    return info, data


def eisa_to_string(value):
    swapped = int.from_bytes(value.to_bytes(4, "little"), "big")
    letters = "".join(chr(0x40 + ((swapped >> shift) & 0x1F)) for shift in (26, 21, 16))
    return letters + "%04X" % (swapped & 0xFFFF)


class Machine:
    def __init__(self):
        self.namespace = {}
        self.stats = collections.defaultdict(collections.Counter)
        self.values = collections.defaultdict(int)
        self.maxima = collections.defaultdict(int)
        self.device_ids = collections.defaultdict(set)
        self.method_calls = collections.defaultdict(set)


class Walker:
    def __init__(self, machine, data, table_revision, mode, signature):
        self.signature = signature
        self.machine = machine
        self.data = data
        self.integer_width = 64 if table_revision >= 2 else 32
        self.mode = mode
        self.measure = mode == "measure"
        self.scope = ()
        self.method = None
        self.device = None
        self.block_depth = 0
        self.while_depth = 0
        self.expression_depth = 0
        self.method_has_return = False
        self.local_names = {}
        self.loop_exits = []

    def count(self, name, amount=1):
        if self.measure:
            self.machine.stats["opcodes"][name] += amount

    def bump(self, key, amount=1):
        if self.measure:
            self.machine.values[key] += amount

    def peak(self, key, value):
        if self.measure and value > self.machine.maxima[key]:
            self.machine.maxima[key] = value

    def byte(self, position):
        if position >= len(self.data):
            raise ParseError("past end")
        return self.data[position]

    def pkg_length(self, position):
        lead = self.byte(position)
        follow = lead >> 6
        if follow == 0:
            return lead & 0x3F, position + 1
        value = lead & 0x0F
        for index in range(follow):
            value |= self.byte(position + 1 + index) << (4 + 8 * index)
        return value, position + 1 + follow

    def name_string(self, position):
        root = False
        parents = 0
        if self.byte(position) == 0x5C:
            root = True
            position += 1
        else:
            while self.byte(position) == 0x5E:
                parents += 1
                position += 1
        lead = self.byte(position)
        if lead == 0x00:
            return (root, parents, ()), position + 1
        if lead == 0x2E:
            count = 2
            position += 1
        elif lead == 0x2F:
            count = self.byte(position + 1)
            position += 2
        else:
            count = 1
        segments = []
        for _ in range(count):
            raw = self.data[position:position + 4]
            if len(raw) < 4 or raw[0] not in NAME_LEAD or any(c not in NAME_CHARS for c in raw):
                raise ParseError("bad NameSeg")
            segments.append(raw.decode("ascii"))
            position += 4
        self.peak("name_segments", count)
        return (root, parents, tuple(segments)), position

    def absolute(self, name):
        root, parents, segments = name
        if root:
            return segments
        base = self.scope[: max(0, len(self.scope) - parents)]
        return base + segments

    def known(self, path):
        return path in self.machine.namespace or path in self.local_names

    def lookup(self, name):
        root, parents, segments = name
        if not segments:
            return None
        if not root and parents == 0 and len(segments) == 1:
            for depth in range(len(self.scope), -1, -1):
                candidate = self.scope[:depth] + segments
                if self.known(candidate):
                    return candidate
            return None
        candidate = self.absolute(name)
        return candidate if self.known(candidate) else None

    def declare(self, name, kind, argument_count=0):
        path = self.absolute(name)
        if self.mode == "declare" and self.method is None:
            previous = self.machine.namespace.get(path)
            if previous is None or previous[0] == "external":
                self.machine.namespace[path] = (kind, argument_count)
        if self.method is not None and path not in self.machine.namespace:
            self.local_names[path] = (kind, argument_count)
        if self.measure:
            if self.method is None:
                self.machine.values["named_objects"] += 1
                self.machine.values["named_objects_" + self.signature.lower()] += 1
                self.machine.stats["named_object_kinds"][kind] += 1
            else:
                self.machine.values["named_objects_in_methods"] += 1
                self.machine.stats["named_objects_in_methods_kinds"][kind] += 1
            last = path[-1] if path else "\\"
            if last.startswith("_"):
                self.machine.stats["predefined"][last] += 1
                self.machine.stats["predefined_kinds"]["%s:%s" % (last, kind)] += 1
        return path

    def term_list(self, position, end):
        while position < end:
            try:
                position = self.term(position)
            except (ParseError, IndexError, RecursionError) as error:
                if self.measure:
                    self.machine.values["parse_errors"] += 1
                    self.machine.values["bytes_skipped"] += end - position
                    self.machine.stats["parse_error_kinds"][str(error)[:40]] += 1
                return end
        return end

    def block(self, start):
        length, position = self.pkg_length(start)
        end = start + length
        if end > len(self.data):
            raise ParseError("PkgLength past end")
        return end, position

    def term(self, position):
        opcode = self.byte(position)
        if self.measure and self.method is None:
            declaration = opcode in (0x06, 0x08, 0x10, 0x14, 0x15, 0x8A, 0x8B, 0x8C, 0x8D, 0x8F) or (
                opcode == 0x5B and self.byte(position + 1) in (0x01, 0x02, 0x13, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88))
            if not declaration and opcode not in (0xA0, 0xA1):
                self.machine.values["table_level_code_terms"] += 1
                self.machine.stats["table_level_code_opcodes"]["0x%02X%s" % (opcode, ("%02X" % self.byte(position + 1)) if opcode == 0x5B else "")] += 1
        if opcode == 0x5B:
            return self.extended(position)
        if opcode in (0x10, 0x14, 0x12, 0x13, 0x11, 0xA0, 0xA1, 0xA2, 0x08):
            return self.structured(position)
        if opcode in SIMPLE:
            return self.simple(position)
        position, _ = self.term_arg(position)
        return position

    def simple(self, position):
        opcode = self.byte(position)
        name, arguments = SIMPLE[opcode]
        self.count(name)
        position += 1
        if opcode == 0x15:
            target, position = self.name_string(position)
            object_type = self.byte(position)
            argument_count = self.byte(position + 1)
            position += 2
            path = self.absolute(target)
            if self.mode == "declare" and path not in self.machine.namespace:
                self.machine.namespace[path] = ("method" if object_type == 8 else "external", argument_count & 7)
            if self.measure:
                self.machine.stats["external_types"][object_type] += 1
            return position
        if opcode == 0x06:
            _, position = self.name_string(position)
            alias, position = self.name_string(position)
            self.declare(alias, "alias")
            return position
        if opcode == 0xA4:
            self.method_has_return = True
        if opcode in (0xA4, 0xA5) and self.loop_exits:
            self.loop_exits[-1] = True
        values = []
        for kind in arguments:
            position, value = self.argument(position, kind)
            values.append(value)
        if opcode == 0x86 and self.measure:
            self.machine.values["notify"] += 1
            if isinstance(values[1], int):
                self.machine.stats["notify_values"]["0x%02X" % values[1]] += 1
        if opcode in CREATE_FIELD_OPS:
            self.declare(values[-1], "buffer_field")
        return position

    def argument(self, position, kind):
        if kind == "T":
            return self.term_arg(position)
        if kind == "S":
            return self.super_name(position), None
        if kind == "M":
            if self.byte(position) in NAME_START:
                return self.name_reference(position, possible_call=False), None
            return self.super_name(position), None
        if kind == "R":
            if self.byte(position) == 0x00:
                return position + 1, None
            return self.super_name(position), None
        if kind == "N":
            name, position = self.name_string(position)
            return position, name
        if kind == "B":
            return position + 1, self.byte(position)
        if kind == "W":
            return position + 2, self.byte(position) | self.byte(position + 1) << 8
        if kind == "D":
            return position + 4, int.from_bytes(self.data[position:position + 4], "little")
        raise ParseError("bad argument kind")

    def super_name(self, position):
        lead = self.byte(position)
        if 0x60 <= lead <= 0x6E:
            self.count("Local" if lead <= 0x67 else "Arg")
            return position + 1
        if lead == 0x5B and self.byte(position + 1) == 0x31:
            self.count("Debug")
            return position + 2
        if lead in NAME_START:
            return self.name_reference(position, possible_call=True)
        position, _ = self.term_arg(position)
        return position

    def record_osi(self, position):
        position, value = self.term_arg(position)
        if self.measure:
            self.machine.values["osi_calls"] += 1
            if isinstance(value, str):
                self.machine.stats["osi_strings"][value] += 1
            else:
                self.machine.values["osi_calls_non_literal"] += 1
        return position

    def name_reference(self, position, possible_call):
        name, position = self.name_string(position)
        resolved = self.lookup(name)
        if self.measure:
            root, parents, segments = name
            form = "root" if root else ("parent" if parents else ("single" if len(segments) == 1 else "multi"))
            self.machine.stats["name_reference_forms"][form] += 1
            if form == "single" and resolved is not None:
                self.machine.stats["single_name_search_levels"][min(len(self.scope) - (len(resolved) - 1), 9)] += 1
        last = name[2][-1] if name[2] else ""
        if resolved is None:
            if self.measure and last in ("_REV", "_OS_", "_GL_"):
                self.machine.stats["root_objects_used"][last] += 1
            elif self.measure:
                self.machine.values["unresolved_names"] += 1
                if self.method is not None:
                    self.machine.values["unresolved_names_in_methods"] += 1
                self.machine.stats["unresolved_samples"][".".join(name[2])] += 1
            return position
        kind, argument_count = self.machine.namespace.get(resolved) or self.local_names[resolved]
        if kind == "method" and possible_call:
            self.count("MethodInvocation")
            if self.measure and self.method is None:
                self.machine.values["table_level_method_calls"] += 1
            if resolved == ("_OSI",):
                return self.record_osi(position)
            if self.measure:
                self.machine.values["method_calls"] += 1
                if self.method is not None:
                    self.machine.method_calls[self.method].add(resolved)
                    if resolved == self.method:
                        self.machine.values["self_recursive_calls"] += 1
            for _ in range(argument_count):
                position, _ = self.term_arg(position)
        elif self.measure and resolved == ("_OSI",):
            self.machine.values["osi_referenced_not_called"] += 1
        return position

    def term_arg(self, position):
        self.expression_depth += 1
        self.peak("expression_depth", self.expression_depth)
        try:
            return self.term_arg_inner(position)
        finally:
            self.expression_depth -= 1

    def term_arg_inner(self, position):
        lead = self.byte(position)
        if lead == 0x00:
            self.count("Zero")
            return position + 1, 0
        if lead == 0x01:
            self.count("One")
            return position + 1, 1
        if lead == 0xFF:
            self.count("Ones")
            return position + 1, (1 << self.integer_width) - 1
        if lead == 0x0A:
            self.count("BytePrefix")
            return position + 2, self.byte(position + 1)
        if lead == 0x0B:
            self.count("WordPrefix")
            return position + 3, int.from_bytes(self.data[position + 1:position + 3], "little")
        if lead == 0x0C:
            self.count("DWordPrefix")
            return position + 5, int.from_bytes(self.data[position + 1:position + 5], "little")
        if lead == 0x0E:
            self.count("QWordPrefix")
            value = int.from_bytes(self.data[position + 1:position + 9], "little")
            if self.integer_width == 32:
                self.bump("qword_constants_in_32bit_tables")
                if value >> 32:
                    self.bump("qword_constants_truncated_in_32bit_tables")
            return position + 9, value
        if lead == 0x0D:
            self.count("StringPrefix")
            end = self.data.index(0, position + 1)
            value = self.data[position + 1:end].decode("latin-1")
            self.peak("string_length", len(value))
            return end + 1, value
        if 0x60 <= lead <= 0x67:
            self.count("Local")
            return position + 1, None
        if 0x68 <= lead <= 0x6E:
            self.count("Arg")
            return position + 1, None
        if lead in NAME_START:
            return self.name_reference(position, possible_call=True), None
        if lead == 0x5B:
            second = self.byte(position + 1)
            if second in (0x30, 0x31, 0x33):
                self.count(EXTENDED[second][0])
                return position + 2, None
            return self.extended(position), None
        if lead in (0x11, 0x12, 0x13):
            return self.structured(position), None
        if lead in SIMPLE:
            return self.simple(position), None
        raise ParseError("unknown opcode 0x%02X" % lead)

    def structured(self, position):
        opcode = self.byte(position)
        if opcode == 0x08:
            self.count("Name")
            name, position = self.name_string(position + 1)
            path = self.declare(name, "name")
            position, value = self.data_object(position)
            last = path[-1] if path else ""
            if self.measure and last in ("_HID", "_CID") and self.device is not None:
                if isinstance(value, int) and value <= 0xFFFFFFFF:
                    self.machine.device_ids[eisa_to_string(value)].add(self.device)
                elif isinstance(value, str):
                    self.machine.device_ids[value].add(self.device)
            if self.measure and last.startswith("_S") and last.endswith("_") and len(last) == 4:
                self.machine.stats["sleep_packages"][last] += 1
            return position
        end, position = self.block(position + 1)
        if opcode == 0x10:
            self.count("Scope")
            name, position = self.name_string(position)
            saved = self.scope
            self.scope = self.resolve_scope(name)
            self.term_list(position, end)
            self.scope = saved
            return end
        if opcode == 0x14:
            return self.method_definition(position, end)
        if opcode == 0x11:
            self.count("Buffer")
            position, _ = self.term_arg(position)
            if self.measure:
                self.resource_census(self.data[position:end])
                self.peak("buffer_bytes", end - position)
            return end
        if opcode in (0x12, 0x13):
            self.count("Package" if opcode == 0x12 else "VarPackage")
            if opcode == 0x12:
                self.peak("package_elements", self.byte(position))
                position += 1
            else:
                position, _ = self.term_arg(position)
            while position < end:
                position, _ = self.data_object(position, in_package=True)
            return end
        if opcode in (0xA0, 0xA2):
            return self.conditional(opcode == 0xA2, position, end)
        if opcode == 0xA1:
            self.count("Else")
            self.block_depth += 1
            self.peak("block_depth", self.block_depth)
            try:
                self.term_list(position, end)
            finally:
                self.block_depth -= 1
            return end
        raise ParseError("unhandled structured opcode")

    def conditional(self, is_while, position, end):
        self.count("While" if is_while else "If")
        predicate_start = position
        position, _ = self.term_arg(position)
        self.block_depth += 1
        self.peak("block_depth", self.block_depth)
        if is_while:
            self.while_depth += 1
            self.peak("while_depth", self.while_depth)
            if self.measure:
                self.machine.values["while_loops"] += 1
                if predicate_start + 1 == position and self.data[predicate_start] in (0x01, 0xFF):
                    self.machine.values["while_constant_true"] += 1
                body = self.data[position:end]
                if b"\x5b\x21" in body or b"\x5b\x22" in body:
                    self.machine.values["while_with_sleep_or_stall"] += 1
        constant_true = predicate_start + 1 == position and self.data[predicate_start] in (0x01, 0xFF)
        if is_while:
            self.loop_exits.append(False)
        try:
            self.term_list(position, end)
        finally:
            self.block_depth -= 1
            if is_while:
                self.while_depth -= 1
                exits = self.loop_exits.pop()
                if constant_true and not exits:
                    self.bump("while_true_without_break_or_return")
                    if self.measure:
                        self.machine.stats["endless_loop_methods"][".".join(self.method or ("<table>",))] += 1
        if self.method is None and self.measure:
            self.machine.values["table_level_" + ("while" if is_while else "if")] += 1
        return end

    def resolve_scope(self, name):
        resolved = self.lookup(name)
        return resolved if resolved is not None else self.absolute(name)

    def method_definition(self, position, end):
        self.count("Method")
        name, position = self.name_string(position)
        flags = self.byte(position)
        position += 1
        argument_count = flags & 7
        if self.method is not None:
            self.bump("nested_method_definitions")
        path = self.declare(name, "method", argument_count)
        if self.measure:
            self.machine.values["methods"] += 1
            if flags & 0x08:
                self.machine.values["serialized_methods"] += 1
            if flags >> 4:
                self.machine.values["methods_with_sync_level"] += 1
            self.machine.stats["method_argument_counts"][argument_count] += 1
            self.peak("method_bytes", end - position)
            if len(path) == 2 and path[0] == "_GPE" and GPE_METHOD_RE.match(path[1]):
                self.machine.stats["gpe_methods"][path[1][:2]] += 1
            if path and path[-1] == "_EVT":
                self.machine.values["evt_methods"] += 1
        if not self.measure:
            return end
        saved = (self.scope, self.method, self.block_depth, self.while_depth, self.method_has_return)
        self.scope = path
        self.method = path
        self.block_depth = 0
        self.while_depth = 0
        self.method_has_return = False
        try:
            self.term_list(position, end)
        finally:
            if not self.method_has_return:
                self.machine.values["methods_without_return"] += 1
            self.scope, self.method, self.block_depth, self.while_depth, self.method_has_return = saved
        return end

    def data_object(self, position, in_package=False):
        lead = self.byte(position)
        if lead in NAME_START:
            self.count("NameReferenceInPackage" if in_package else "NameReferenceInName")
            _, position = self.name_string(position)
            return position, None
        if lead in (0x11, 0x12, 0x13):
            return self.structured(position), None
        return self.term_arg(position)

    def extended(self, position):
        second = self.byte(position + 1)
        position += 2
        if second == 0x80:
            return self.operation_region(position)
        if second in (0x81, 0x86, 0x87):
            return self.field(position, second)
        if second in (0x82, 0x83, 0x84, 0x85):
            return self.named_scope(position, second)
        if second not in EXTENDED:
            raise ParseError("unknown extended opcode 0x5B%02X" % second)
        name, arguments = EXTENDED[second]
        self.count(name)
        values = []
        for kind in arguments:
            position, value = self.argument(position, kind)
            values.append(value)
        if second == 0x01:
            self.declare(values[0], "mutex")
            if self.measure and values[1]:
                self.machine.values["mutexes_with_sync_level"] += 1
        elif second == 0x02:
            self.declare(values[0], "event")
        elif second == 0x13:
            self.declare(values[-1], "buffer_field")
        elif second == 0x88:
            self.declare(values[0], "data_region")
        elif second == 0x23 and self.measure:
            timeout = values[1]
            self.machine.stats["acquire_timeouts"]["0xFFFF" if timeout == 0xFFFF else ("0" if timeout == 0 else "other")] += 1
        elif second in (0x21, 0x22) and self.measure and isinstance(values[0], int):
            self.peak("stall_us" if second == 0x21 else "sleep_ms", values[0])
            if second == 0x21 and values[0] > 100:
                self.machine.values["stall_over_100us"] += 1
        return position

    def operation_region(self, position):
        self.count("OperationRegion")
        name, position = self.name_string(position)
        space = self.byte(position)
        position += 1
        position, offset = self.term_arg(position)
        position, length = self.term_arg(position)
        self.declare(name, "region")
        if self.measure:
            if space in SPACES:
                space_name = SPACES[space]
            elif space >= 0x80:
                space_name = "OEM_0x%02X" % space
            else:
                space_name = "Reserved_0x%02X" % space
            self.machine.stats["region_spaces"][space_name] += 1
            if self.method is not None:
                self.machine.values["regions_in_methods"] += 1
            if offset is None:
                self.machine.values["regions_dynamic_offset"] += 1
            if length is None:
                self.machine.values["regions_dynamic_length"] += 1
            if space == 3 and self.device is not None:
                self.machine.stats["ec_region_devices"][self.device] += 1
        return position

    def named_scope(self, position, second):
        end, position = self.block(position)
        name, position = self.name_string(position)
        kind = {0x82: "device", 0x83: "processor", 0x84: "power_resource", 0x85: "thermal_zone"}[second]
        self.count({0x82: "Device", 0x83: "Processor", 0x84: "PowerResource", 0x85: "ThermalZone"}[second])
        path = self.declare(name, kind)
        if second == 0x83:
            position += 6
        elif second == 0x84:
            position += 3
        saved = (self.scope, self.device)
        self.scope = path
        if second == 0x82:
            self.device = path
        try:
            self.term_list(position, end)
        finally:
            self.scope, self.device = saved
        return end

    def field(self, position, second):
        end, position = self.block(position)
        self.count({0x81: "Field", 0x86: "IndexField", 0x87: "BankField"}[second])
        _, position = self.name_string(position)
        if second in (0x86, 0x87):
            _, position = self.name_string(position)
        if second == 0x87:
            position, _ = self.term_arg(position)
        flags = self.byte(position)
        position += 1
        if self.measure:
            self.machine.stats["field_access"][ACCESS.get(flags & 0x0F, "Reserved")] += 1
            self.machine.stats["field_update"][UPDATE[(flags >> 5) & 3]] += 1
            if flags & 0x10:
                self.machine.values["fields_with_lock"] += 1
            if self.method is not None:
                self.machine.values["fields_in_methods"] += 1
        while position < end:
            lead = self.byte(position)
            if lead == 0x00:
                _, position = self.pkg_length(position + 1)
                self.bump("reserved_field_entries")
            elif lead == 0x01:
                access = self.byte(position + 1)
                if self.measure:
                    self.machine.stats["access_as"][ACCESS.get(access & 0x0F, "Reserved")] += 1
                    attribute = self.byte(position + 2)
                    if access & 0x0F == 5 or (access >> 6):
                        self.machine.stats["access_attributes"]["0x%02X/0x%02X" % (access, attribute)] += 1
                position += 3
            elif lead == 0x02:
                self.bump("connect_fields")
                if self.byte(position + 1) == 0x11:
                    position = self.structured(position + 1)
                else:
                    _, position = self.name_string(position + 1)
            elif lead == 0x03:
                self.bump("extended_access_fields")
                if self.measure:
                    access = self.byte(position + 1)
                    self.machine.stats["access_as"][ACCESS.get(access & 0x0F, "Reserved")] += 1
                    self.machine.stats["access_attributes"]["ext 0x%02X/0x%02X/%d" % (access, self.byte(position + 2), self.byte(position + 3))] += 1
                position += 4
            else:
                raw = self.data[position:position + 4]
                if len(raw) < 4 or raw[0] not in NAME_LEAD or any(c not in NAME_CHARS for c in raw):
                    raise ParseError("bad field element")
                position += 4
                width, position = self.pkg_length(position)
                self.declare((False, 0, (raw.decode("ascii"),)), "field_unit")
                if self.measure:
                    self.machine.values["field_units"] += 1
                    self.peak("field_unit_bits", width)
        return end

    def resource_census(self, raw):
        position = 0
        found = []
        while position < len(raw):
            tag = raw[position]
            if tag & 0x80:
                if position + 3 > len(raw):
                    return
                kind = tag & 0x7F
                length = raw[position + 1] | raw[position + 2] << 8
                if kind not in LARGE_RESOURCES:
                    return
                name = LARGE_RESOURCES[kind]
                if kind == 0x0E and position + 6 <= len(raw):
                    name = SERIAL_BUS.get(raw[position + 5], "SerialBus?")
                found.append(name)
                position += 3 + length
            else:
                kind = (tag >> 3) & 0x0F
                length = tag & 0x07
                if kind not in SMALL_RESOURCES:
                    return
                if kind == 0xF:
                    if position + 2 != len(raw) or length != 1:
                        return
                    for name in found:
                        self.machine.stats["resource_descriptors"][name] += 1
                    self.machine.values["resource_templates"] += 1
                    return
                found.append(SMALL_RESOURCES[kind])
                position += 1 + length


def call_graph_depth(edges):
    depth = {}
    on_stack = set()
    back_edges = [0]

    def visit(node):
        if node in depth:
            return depth[node]
        if node in on_stack:
            back_edges[0] += 1
            return 0
        on_stack.add(node)
        best = 0
        for callee in edges.get(node, ()):
            best = max(best, 1 + visit(callee))
        on_stack.discard(node)
        depth[node] = best
        return best

    deepest = 0
    for node in list(edges):
        deepest = max(deepest, visit(node))
    return {"max_static_call_depth": deepest, "back_edges": back_edges[0],
            "callers": len(edges), "edges": sum(len(value) for value in edges.values())}


def walk(machine, tables, mode):
    for info, data in tables:
        walker = Walker(machine, data, info.get("revision", 1), mode, info["signature"])
        walker.term_list(36, min(len(data), info.get("length", len(data))))


def analyse(path):
    described = [describe_table(entry) for entry in read_acpidump(path)]
    machine = Machine()
    machine.namespace[("_OSI",)] = ("method", 1)
    aml = [(info, data) for info, data in described if info["signature"] in ("DSDT", "SSDT")]
    walk(machine, aml, "declare")
    walk(machine, aml, "declare")
    walk(machine, aml, "measure")
    report = {"tables": [info for info, _ in described]}
    ec_devices = sorted(machine.device_ids.get("PNP0C09", set()))
    report["ec_devices"] = ["\\" + ".".join(device) for device in ec_devices]
    report["ec_with_reg"] = sum(1 for device in ec_devices if device + ("_REG",) in machine.namespace)
    report["device_ids"] = {key: len(value) for key, value in machine.device_ids.items()}
    report["stats"] = {
        key: {("\\" + ".".join(name) if isinstance(name, tuple) else name): amount for name, amount in value.items()}
        for key, value in machine.stats.items()
    }
    report["values"] = dict(machine.values)
    report["maxima"] = dict(machine.maxima)
    report["call_graph"] = call_graph_depth(machine.method_calls)
    report["platform_osc"] = ("_SB_", "_OSC") in machine.namespace
    children = collections.Counter(path[:-1] for path in machine.namespace if path)
    report["namespace_shape"] = {
        "nodes": len(machine.namespace),
        "max_depth": max((len(path) for path in machine.namespace), default=0),
        "max_children": max(children.values(), default=0),
        "median_children": sorted(children.values())[len(children) // 2] if children else 0,
        "root_children": children.get((), 0),
    }
    report["namespace_methods"] = sum(1 for kind, _ in machine.namespace.values() if kind == "method")
    for info, data in described:
        marker = data.find(b"$FID") if info["signature"] == "FIDT" else -1
        if marker >= 0 and len(data) >= marker + 51:
            year = int.from_bytes(data[marker + 44:marker + 46], "little")
            month, day = data[marker + 46], data[marker + 47]
            if 2000 <= year <= 2030 and 1 <= month <= 12 and 1 <= day <= 31:
                report["fidt_build_date"] = "%04d-%02d-%02d" % (year, month, day)
                report["fidt_bios_tag"] = data[marker + 7:marker + 16].split(b"\x00")[0].decode("latin-1")
        if info["signature"] == "FACP" and len(data) >= 116:
            flags = int.from_bytes(data[112:116], "little")
            report["fadt"] = {
                "revision": info.get("revision"),
                "length": len(data),
                "minor": data[131] if len(data) > 131 else None,
                "pm_profile": data[45],
                "flags": flags,
                "hardware_reduced": bool(flags >> 20 & 1),
                "low_power_s0_idle": bool(flags >> 21 & 1),
                "boot_arch": int.from_bytes(data[109:111], "little"),
                "sci_int": int.from_bytes(data[46:48], "little"),
                "smi_cmd": int.from_bytes(data[48:52], "little"),
                "has_x_dsdt": len(data) >= 148 and int.from_bytes(data[140:148], "little") != 0,
                "dsdt32": int.from_bytes(data[40:44], "little") != 0,
            }
    return report


def main():
    mapping_path, data_dir, out_path = sys.argv[1:4]
    results = {}
    with open(mapping_path, encoding="utf-8") as mapping:
        for line in mapping:
            index, repository_path = line.rstrip("\n").split("\t")
            report = analyse("%s/%s.bin" % (data_dir, index))
            report["path"] = repository_path
            results[index] = report
            print(index, repository_path, file=sys.stderr)
    with open(out_path, "w", encoding="utf-8") as out:
        json.dump(results, out, indent=1, default=str)


if __name__ == "__main__":
    main()
