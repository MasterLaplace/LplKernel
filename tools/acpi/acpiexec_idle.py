"""The C-states a machine's processors publish, worked out apart from the kernel's code: the processor objects and the
CPU slots they bind to from ACPICA's namespace and the decoded MADT, their states from the _CST acpiexec evaluates, as
the `aml idle slot` and `aml idle state` lines `test-acpi --initialize` prints.

Usage: python3 -I tools/acpi/acpiexec_idle.py <acpica bin directory> <MADT lines> <presets> <table>...

The MADT lines are those tools/acpi/expected_madt.py reads from the machine's decoded report: `processor <slot>
uid=<UID>` gives each CPU slot. The tables are those `test-acpi --loaded` writes, the DSDT first: the ones the
initialisation ended with, Load's included, which acpiexec loads statically. The presets are tools/acpi/presets.txt,
whose lines but its comments become acpiexec's -fi file, so that both read the same NVS values. acpiexec loads the
tables without running them to list the namespace (tools/acpi/acpiexec_namespace.py): every Processor or Device with a
_CST, an _OSC or a _PDC, and each one's ancestors. A first run reads the _HID and _UID of those Devices. A second run,
with region fill 0 and no repair of return values (`-fv 0 -dr`), which runs _REG, _STA and _INI, is given on its
standard input, since a batch (`-b`) holds a few hundred characters only, the handshake of the specification's §3.8
steps 6 and 7:
`\\_SB._OSC` asked for nothing, a query then a commit, then the _OSC with Intel's processor UUID, or the _PDC, of each
processor object that binds to a CPU slot, with the capabilities of C1 to C3 and MWAIT (0x318). It then reads the
_STA of every candidate and of the Devices, Processors and ThermalZones above it, and evaluates each _CST.

A processor object is a Processor, or a Device whose _HID is "ACPI0007"; it binds to the first slot of its UID, the
ProcessorID or the _UID. Its _CST is published, in declaration order, when its _STA says present or functioning and
enabled, a _STA that fails keeping it out, no device above it says neither present nor functioning (a missing _STA
reads 0x0F, and one that fails is walked into, as step 4 walks into it), and no object declared before it took its
slot. A _CST is a Package of an Integer Count of at least 1, then as many states; anything else publishes no state. A
state is a Package of a Buffer holding a Generic Register before its End Tag, then three Integers of 32 bits; a
faulty one is skipped and counted, and once seven are published the others are not read. When acpiexec stops before
it evaluates all it was given, as it does when a handshake loads a table from a region of length 0, only `aml idle
unevaluated <path>` is printed, the first path it did not reach.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import acpiexec_namespace  # noqa: E402

PLATFORM_UUID = "6E B0 11 08 27 4A F9 44 8D 60 3C BB C2 2E 7B 48"
PROCESSOR_UUID = "16 A6 77 40 0C 29 BE 47 9E BD D8 70 58 71 39 53"
CAPABILITIES = "18 03 00 00"
OPTIONS = ["-fv", "0", "-dr", "-fi", "nvs.txt"]
MAX_STATES = 7
NO_STATUS = 0x0F
DEVICES = ("Device", "Processor", "Thermal")

NODE = re.compile(r"^aml node (\S+) (\S+)(?: (.*))?$")
SLOT = re.compile(r"^processor (\d+) uid=0x([0-9A-Fa-f]+)")
PROCESSOR_ID = re.compile(r"^ID ([0-9A-Fa-f]{2})")
EVALUATE = re.compile(r"^evaluate (\S+)")
RETURNED = re.compile(r"^Evaluation of (\S+) returned object")
FAILED = re.compile(r"^Evaluation of (\S+) failed with status")
PACKAGE = re.compile(r"^\s*\[Package\] Contains (\d+) Elements:")
INTEGER = re.compile(r"^\s*\[Integer\] = ([0-9A-Fa-f]+)")
STRING = re.compile(r'^\s*\[String\] Length \S+ = "(.*)"')
BUFFER = re.compile(r"^\s*\[Buffer\] Length ([0-9A-Fa-f]+) =")
ROW = re.compile(r"^\s*[0-9A-Fa-f]{4}: ((?:[0-9A-Fa-f]{2} )*[0-9A-Fa-f]{2})")
OTHER = re.compile(r"^\s*\[([A-Za-z ]+)\]")


class Unreadable(Exception):
    """A result the reader cannot follow."""


class Stopped(Exception):
    """acpiexec did not reach every evaluation it was given."""


def acpiexec(bin_directory, work, tables, commands):
    """Each path acpiexec evaluated from @p commands, given on its standard input: its result, or None when the
    evaluation failed; Stopped when it did not reach one."""
    output = subprocess.run([os.path.join(bin_directory, "acpiexec")] + OPTIONS + tables, cwd=work,
                            input="\n".join(commands + ["quit", ""]), capture_output=True, text=True,
                            errors="replace", timeout=600).stdout
    found = results(output)
    for command in commands:
        path = EVALUATE.match(command).group(1)
        if path not in found:
            raise Stopped(path)
    return found


def read_object(lines, position):
    """The object printed from lines[position] on, and the position after it."""
    line = lines[position]
    match = PACKAGE.match(line)
    if match:
        elements, position = [], position + 1
        for _ in range(int(match.group(1))):
            element, position = read_object(lines, position)
            elements.append(element)
        return ("package", elements), position
    match = INTEGER.match(line)
    if match:
        return ("integer", int(match.group(1), 16)), position + 1
    match = STRING.match(line)
    if match:
        return ("string", match.group(1)), position + 1
    match = BUFFER.match(line)
    if match:
        length, data, rows = int(match.group(1), 16), [], [line[match.end():]]
        position += 1
        while len(rows) <= (length + 15) // 16 and position < len(lines) and ROW.match(lines[position]):
            rows.append(lines[position])
            position += 1
        for row in rows:
            found = ROW.match(row)
            if found:
                data.extend(int(byte, 16) for byte in found.group(1).split())
        return ("buffer", bytes(data[:length])), position
    match = OTHER.match(line)
    if match:
        return (match.group(1).lower(), None), position + 1
    raise Unreadable(line)


def results(output):
    """Each evaluated path's result: an object, or None when its evaluation failed."""
    lines, found, position = output.splitlines(), {}, 0
    while position < len(lines):
        returned, failed = RETURNED.match(lines[position]), FAILED.match(lines[position])
        if failed:
            found[failed.group(1)] = None
        if returned:
            try:
                found[returned.group(1)], position = read_object(lines, position + 1)
                continue
            except (Unreadable, IndexError):
                found[returned.group(1)] = None
        position += 1
    return found


def generic_register(template):
    """The first Generic Register of a resource template, before its End Tag, or None."""
    offset = 0
    while offset < len(template):
        tag = template[offset]
        if tag & 0x80:
            if offset + 3 > len(template):
                return None
            length = template[offset + 1] | template[offset + 2] << 8
            data = template[offset + 3:offset + 3 + length]
            if len(data) < length:
                return None
            if tag == 0x82 and length >= 12:
                return data[0], data[1], data[2], data[3], int.from_bytes(data[4:12], "little")
            offset += 3 + length
        else:
            if (tag >> 3) & 0x0F == 0x0F:
                return None
            length = tag & 0x07
            if offset + 1 + length > len(template):
                return None
            offset += 1 + length
    return None


def double_word(element):
    """An Integer that fits 32 bits, or None."""
    kind, value = element
    return value if kind == "integer" and value <= 0xFFFFFFFF else None


def state(element):
    """One C-state, or None when it is faulty."""
    kind, elements = element
    if kind != "package" or len(elements) != 4 or elements[0][0] != "buffer":
        return None
    register = generic_register(elements[0][1])
    numbers = [double_word(number) for number in elements[1:]]
    if register is None or None in numbers:
        return None
    return register, numbers


def states(result):
    """The states a _CST result publishes and the faulty ones it skips; none when the package is malformed."""
    if result is None or result[0] != "package" or not result[1]:
        return [], 0
    count = double_word(result[1][0])
    if count is None or count < 1 or count != len(result[1]) - 1:
        return [], 0
    published, skipped = [], 0
    for element in result[1][1:]:
        if len(published) == MAX_STATES:
            break
        read = state(element)
        if read is None:
            skipped += 1
        else:
            published.append(read)
    return published, skipped


def integer_of(result):
    """An Integer result, or a String one read as an operand's hexadecimal digits, or None."""
    if result is None:
        return None
    if result[0] == "integer":
        return result[1]
    if result[0] == "string" and re.fullmatch(r"[0-9A-Fa-f]+", result[1] or ""):
        return int(result[1], 16)
    return None


def above(path):
    """The paths of the nodes above @p path, the root left out, from the top down."""
    parts = path.lstrip("\\").split(".")
    return ["\\" + ".".join(parts[:depth]) for depth in range(1, len(parts))]


def publish(nodes, found, candidates, slot_of):
    """The published slots: for each, its processor object's path, its states and its skipped count."""
    def status_of(device, failed):
        if device + "._STA" not in nodes:
            return NO_STATUS
        value = integer_of(found.get(device + "._STA"))
        return failed if value is None else value

    published, taken = {}, set()
    for path in candidates:
        if any(device in nodes and nodes[device][0] in DEVICES and status_of(device, NO_STATUS) & 0x09 == 0
               for device in above(path)):
            continue
        own, slot = status_of(path, 0), slot_of(path)
        if own & 0x09 == 0 or own & 0x02 == 0 or slot is None or slot in taken:
            continue
        taken.add(slot)
        published[slot] = (path,) + states(found[path + "._CST"])
    return published


def main():
    bin_directory = os.path.abspath(sys.argv[1])
    slots = [int(match.group(2), 16) for match in map(SLOT.match, open(sys.argv[2]).read().splitlines()) if match]
    presets = [line for line in open(sys.argv[3]).read().splitlines() if line.strip() and not line.startswith("#")]
    nodes = {}
    with tempfile.TemporaryDirectory() as work:
        tables = []
        for index, table in enumerate(sys.argv[4:]):
            tables.append("table%03u.dat" % index)
            shutil.copyfile(table, os.path.join(work, tables[-1]))
        if not tables:
            return
        with open(os.path.join(work, "nvs.txt"), "w") as nvs:
            nvs.write("".join(line + "\n" for line in presets))
        for line in acpiexec_namespace.namespace(bin_directory, tables, work):
            match = NODE.match(line)
            if match:
                nodes[match.group(1)] = (match.group(2), match.group(3) or "")
        processors = [path for path, (kind, _) in nodes.items() if kind in ("Processor", "Device") and
                      any(path + "." + name in nodes for name in ("_CST", "_OSC", "_PDC"))]
        try:
            identity = acpiexec(bin_directory, work, tables,
                                ["evaluate %s.%s" % (path, name) for path in processors
                                 if nodes[path][0] == "Device" for name in ("_HID", "_UID") if path + "." + name in nodes])

            def is_processor(path):
                return nodes[path][0] == "Processor" or identity.get(path + "._HID") == ("string", "ACPI0007")

            def slot_of(path):
                if nodes[path][0] == "Processor":
                    match = PROCESSOR_ID.match(nodes[path][1])
                    unique = int(match.group(1), 16) if match else None
                else:
                    unique = integer_of(identity.get(path + "._UID"))
                return slots.index(unique) if unique in slots else None

            candidates = [path for path in processors if path + "._CST" in nodes and is_processor(path)]
            wanted = {device for path in candidates for device in above(path) + [path]}
            commands = []
            if "\\_SB_._OSC" in nodes:
                commands += ["evaluate \\_SB._OSC (%s) 1 2 (01 00 00 00 00 00 00 00)" % PLATFORM_UUID,
                             "evaluate \\_SB._OSC (%s) 1 2 (00 00 00 00 00 00 00 00)" % PLATFORM_UUID]
            for path in processors:
                if not is_processor(path) or slot_of(path) is None:
                    continue
                if path + "._OSC" in nodes:
                    commands.append("evaluate %s._OSC (%s) 1 2 (00 00 00 00 %s)" % (path, PROCESSOR_UUID, CAPABILITIES))
                elif path + "._PDC" in nodes:
                    commands.append("evaluate %s._PDC (01 00 00 00 01 00 00 00 %s)" % (path, CAPABILITIES))
            commands += ["evaluate %s._STA" % path for path in nodes if path in wanted and
                         nodes[path][0] in DEVICES and path + "._STA" in nodes]
            commands += ["evaluate %s._CST" % path for path in candidates]
            found = acpiexec(bin_directory, work, tables, commands)
        except Stopped as stopped:
            print("aml idle unevaluated %s" % stopped)
            return
    published = publish(nodes, found, candidates, slot_of)
    for slot in sorted(published):
        path, listed, skipped = published[slot]
        handshake = "osc" if path + "._OSC" in nodes else ("pdc" if path + "._PDC" in nodes else "none")
        print("aml idle slot %u %s states=%u skipped=%u handshake=%s" % (slot, path, len(listed), skipped, handshake))
        for index, ((space, width, offset, access, address), (kind, latency, power)) in enumerate(listed):
            print("aml idle state %u %u type=%u latency=%u power=%u space=0x%02X width=%u offset=%u access=%u "
                  "address=0x%X" % (slot, index + 1, kind, latency, power, space, width, offset, access, address))


if __name__ == "__main__":
    main()
