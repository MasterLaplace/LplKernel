"""The C-states ACPICA's acpiexec evaluates for the processor objects `test-acpi --initialize` published, as the
`aml idle slot` and `aml idle state` lines the harness prints.

Usage: test-acpi --initialize <dump> | python3 -I tools/acpi/acpiexec_idle.py <acpica bin directory> <dump>

The dump is split with acpixtract, and its DSDT then its SSDTs go to acpiexec with region fill 0 and no repair of
return values (`-fv 0 -dr`), which runs _REG, _STA and _INI. acpiexec is then given, on its standard input, since
a batch (`-b`) holds a few hundred characters only, the handshake of the
specification's §3.8 steps 6 and 7: `\\_SB._OSC` asked for nothing, a query then a commit, then each published
processor object's _OSC with Intel's processor UUID or its _PDC, as the harness names it, with the capabilities
of C1 to C3 and MWAIT (0x318). Each one's _CST is evaluated last, and read apart from the kernel's code: a Package
of a Count then as many C-states, each a Package of a Buffer that holds a Generic Register before its End Tag and
three Integers of 32 bits; anything else publishes no state. The first eight states are printed, as the kernel
publishes them. When acpiexec stops before it evaluates a _CST, as it does when a handshake loads a table from a
region of length 0, only `aml idle unevaluated <path>` is printed.
"""
import os
import re
import subprocess
import sys
import tempfile

PLATFORM_UUID = "6E B0 11 08 27 4A F9 44 8D 60 3C BB C2 2E 7B 48"
PROCESSOR_UUID = "16 A6 77 40 0C 29 BE 47 9E BD D8 70 58 71 39 53"
CAPABILITIES = "18 03 00 00"
MAX_STATES = 8

SLOT = re.compile(r"^aml idle slot (\d+) (\S+) states=\d+ handshake=(osc|pdc|none)$")
EVALUATING = re.compile(r"^Evaluating (\S+)$")
RETURNED = re.compile(r"^Evaluation of (\S+) returned object")
FAILED = re.compile(r"^Evaluation of (\S+) failed with status")
PACKAGE = re.compile(r"^\s*\[Package\] Contains (\d+) Elements:")
INTEGER = re.compile(r"^\s*\[Integer\] = ([0-9A-Fa-f]+)")
BUFFER = re.compile(r"^\s*\[Buffer\] Length ([0-9A-Fa-f]+) =")
HEX_ROW = re.compile(r"^\s*[0-9A-Fa-f]{4}: ((?:[0-9A-Fa-f]{2} )*[0-9A-Fa-f]{2})")
OTHER = re.compile(r"^\s*\[([A-Za-z ]+)\]")


class Unreadable(Exception):
    """A result the reader cannot follow."""


def tables_of_dump(bin_directory, dump, work):
    """The DSDT, then the SSDTs in the dump's order, that acpixtract writes from an acpidump text file."""
    subprocess.run([os.path.join(bin_directory, "acpixtract"), "-a", os.path.abspath(dump)], cwd=work,
                   capture_output=True, check=False)
    ssdts = sorted((name for name in os.listdir(work) if re.fullmatch(r"ssdt\d*\.dat", name)),
                   key=lambda name: int(name[4:-4] or 0))
    return (["dsdt.dat"] if os.path.exists(os.path.join(work, "dsdt.dat")) else []) + ssdts


def commands(slots):
    """The commands: the platform handshake, each processor's, then each _CST."""
    listed = ["evaluate \\_SB._OSC (%s) 1 2 (01 00 00 00 00 00 00 00)" % PLATFORM_UUID,
             "evaluate \\_SB._OSC (%s) 1 2 (00 00 00 00 00 00 00 00)" % PLATFORM_UUID]
    for _, path, handshake in slots:
        if handshake == "osc":
            listed.append("evaluate %s._OSC (%s) 1 2 (00 00 00 00 %s)" % (path, PROCESSOR_UUID, CAPABILITIES))
        elif handshake == "pdc":
            listed.append("evaluate %s._PDC (01 00 00 00 01 00 00 00 %s)" % (path, CAPABILITIES))
    listed.extend("evaluate %s._CST" % path for _, path, _ in slots)
    return listed


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
    match = BUFFER.match(line)
    if match:
        length, data, position = int(match.group(1), 16), [], position + 1
        while len(data) < length and position < len(lines) and HEX_ROW.match(lines[position]):
            data.extend(int(byte, 16) for byte in HEX_ROW.match(lines[position]).group(1).split())
            position += 1
        return ("buffer", bytes(data[:length])), position
    match = OTHER.match(line)
    if match:
        return (match.group(1).lower(), None), position + 1
    raise Unreadable(line)


def results(output):
    """Each _CST path's result: an object, or None when its evaluation failed."""
    lines, found, position = output.splitlines(), {}, 0
    while position < len(lines):
        returned, failed = RETURNED.match(lines[position]), FAILED.match(lines[position])
        if failed and failed.group(1).endswith("._CST"):
            found[failed.group(1)] = None
        if returned and returned.group(1).endswith("._CST"):
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


def states(result):
    """The C-states a _CST result lists, or None when it is malformed."""
    if result is None or result[0] != "package" or not result[1]:
        return None
    count = double_word(result[1][0])
    if count is None or count != len(result[1]) - 1:
        return None
    listed = []
    for kind, elements in result[1][1:]:
        if kind != "package" or len(elements) != 4 or elements[0][0] != "buffer":
            return None
        register = generic_register(elements[0][1])
        numbers = [double_word(element) for element in elements[1:]]
        if register is None or None in numbers:
            return None
        listed.append((register, numbers))
    return listed


def main():
    bin_directory, dump = os.path.abspath(sys.argv[1]), sys.argv[2]
    slots = [(int(match.group(1)), match.group(2), match.group(3))
             for match in map(SLOT.match, sys.stdin.read().splitlines()) if match]
    if not slots:
        return
    with tempfile.TemporaryDirectory() as work:
        tables = tables_of_dump(bin_directory, dump, work)
        output = subprocess.run([os.path.join(bin_directory, "acpiexec"), "-fv", "0", "-dr"] + tables, cwd=work,
                                input="\n".join(commands(slots) + ["quit", ""]), capture_output=True, text=True,
                                errors="replace", timeout=600).stdout
    found = results(output)
    unevaluated = [path for _, path, _ in slots if path + "._CST" not in found]
    if unevaluated:
        print("aml idle unevaluated %s" % unevaluated[0])
        return
    for slot, path, handshake in slots:
        listed = states(found.get(path + "._CST")) or []
        print("aml idle slot %u %s states=%u handshake=%s" % (slot, path, min(len(listed), MAX_STATES), handshake))
        for index, ((space, width, offset, access, address), (kind, latency, power)) in enumerate(
                listed[:MAX_STATES]):
            print("aml idle state %u %u type=%u latency=%u power=%u space=0x%02X width=%u offset=%u access=%u "
                  "address=0x%X" % (slot, index + 1, kind, latency, power, space, width, offset, access, address))


if __name__ == "__main__":
    main()
