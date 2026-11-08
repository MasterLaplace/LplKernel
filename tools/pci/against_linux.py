"""Compares the kernel's `[LPLTLM] pci.function` records with the configuration space Linux reads on the same machine.

Usage: python3 -I tools/pci/against_linux.py <guest log> <kernel serial log>

The guest log holds, for each function Linux lists, a `CONFIG <segment:bus:device.function> <bytes>` line and the bytes
of /sys/bus/pci/devices/*/config as `od -A x -t x1` prints them: 4096 when Linux reaches the function's extended space
through its own ECAM, 256 otherwise. Apart from the kernel's code, this script walks those bytes as Linux walks them
(pci_find_capability, pci_find_next_ext_capability): the capability list of the first 256 bytes for the PCI Express
capability, then the extended list from 0x100, each next offset masked to a dword, ended by an offset below 0x100,
after 480 headers at most. Each function must have a kernel record of the same bus, device and function, with the same
vendor and device ID, PCI Express exactly where the list holds the capability, and for such a function the same
capabilities, versions and offsets and the same end of the walk; a function Linux reads 256 bytes of has no extended
space. When lspci is installed, `lspci -vvv -F` decodes the same bytes, and must name every extended capability at the
offset and version the walk gives.
"""
import re
import shutil
import subprocess
import sys
import tempfile

LEGACY = 0x100
BOUND = 480
EXPRESS = 0x10
RECORD = re.compile(r"\[LPLTLM\] pci\.function (.*)$")
CONFIG = re.compile(r"CONFIG (\w{4}):(\w{2}):(\w{2})\.(\w) (\d+)$")
ROW = re.compile(r"^([0-9a-f]{6}) ((?:[0-9a-f]{2} ?){1,16})$")
LSPCI_FUNCTION = re.compile(r"^([0-9a-f]{2}):([0-9a-f]{2})\.([0-9a-f]) ")
LSPCI_EXTENDED = re.compile(r"^\s+Capabilities: \[([0-9a-f]{3}) v(\d+)\]")


def guest_functions(text):
    """Each function of the guest log, as (bus, device, function) to the bytes Linux read."""
    functions, current = {}, None
    for line in text.splitlines():
        line = line.strip("\r")
        head = CONFIG.search(line)
        if head:
            current = tuple(int(head.group(index), 16) for index in (2, 3, 4))
            functions[current] = bytearray()
            continue
        row = ROW.match(line)
        if row and current is not None:
            if int(row.group(1), 16) != len(functions[current]):
                raise SystemExit("not ok guest dump of %02x:%02x.%x jumps to 0x%s" % (current + (row.group(1),)))
            functions[current] += bytes.fromhex(row.group(2))
    return functions


def kernel_records(text):
    """Each `pci.function` record of the kernel's log, as (bus, device, function) to its fields."""
    records = {}
    for line in text.splitlines():
        found = RECORD.search(line)
        if found:
            fields = dict(pair.split("=", 1) for pair in found.group(1).split())
            records[(int(fields["bus"]), int(fields["device"]), int(fields["function"]))] = fields
    return records


def dword(data, offset):
    """The dword at offset of the bytes Linux read, or all ones past them."""
    return int.from_bytes(data[offset:offset + 4], "little") if offset + 4 <= len(data) else 0xFFFFFFFF


def is_express(data):
    """Whether the capability list of the first 256 bytes holds the PCI Express capability, as pci_find_capability."""
    if not data[0x06] & 0x10 or data[0x0E] & 0x7F > 2:
        return False
    offset = data[0x14 if data[0x0E] & 0x7F == 2 else 0x34]
    for _ in range(48):
        if offset < 0x40:
            return False
        offset &= 0xFC
        if data[offset] == 0xFF:
            return False
        if data[offset] == EXPRESS:
            return True
        offset = data[offset + 1]
    return False


def walk(data):
    """The end of the extended list's walk and its capabilities, as `<id>v<version>@<offset>` strings."""
    header = dword(data, LEGACY)
    if header == 0xFFFFFFFF:
        return "no-space", []
    if header == 0:
        return "none", []
    offset, listed = LEGACY, []
    while True:
        listed.append("%04Xv%X@%03X" % (header & 0xFFFF, (header >> 16) & 0xF, offset))
        following = (header >> 20) & 0xFFC
        if following == 0:
            return "listed", listed
        if following < LEGACY:
            return "malformed", listed
        if len(listed) == BOUND:
            return "looped", listed
        offset, header = following, dword(data, following)
        if header == 0xFFFFFFFF:
            return "malformed", listed


def lspci_extended(functions):
    """What `lspci -vvv -F` decodes of the same bytes: (bus, device, function) to its extended (offset, version)s."""
    if shutil.which("lspci") is None:
        return None
    with tempfile.NamedTemporaryFile("w", suffix=".lspci") as dump:
        for (bus, device, function), data in sorted(functions.items()):
            dump.write("%02x:%02x.%x Device\n" % (bus, device, function))
            for offset in range(0, len(data), 16):
                dump.write(("%02x" if offset < LEGACY else "%03x") % offset + ": " +
                           " ".join("%02x" % byte for byte in data[offset:offset + 16]) + "\n")
            dump.write("\n")
        dump.flush()
        text = subprocess.run(["lspci", "-vvv", "-F", dump.name], capture_output=True, text=True).stdout
    decoded, current = {}, None
    for line in text.splitlines():
        head = LSPCI_FUNCTION.match(line)
        if head:
            current = tuple(int(head.group(index), 16) for index in (1, 2, 3))
            decoded[current] = []
        extended = LSPCI_EXTENDED.match(line)
        if extended and current is not None:
            decoded[current].append((int(extended.group(1), 16), int(extended.group(2))))
    return decoded


def compare(key, data, record, decoded):
    """The disagreements between one function's bytes, its kernel record and lspci's decoding of the bytes."""
    if record is None:
        return ["no kernel record"]
    wrong = []
    identity = (dword(data, 0) & 0xFFFF, dword(data, 0) >> 16)
    if (int(record["vendor"], 16), int(record["device_id"], 16)) != identity:
        wrong.append("vendor and device %04X:%04X, the kernel says %s:%s" % (identity + (record["vendor"],
                                                                                         record["device_id"])))
    express = is_express(data)
    if (record["express"] == "1") != express:
        wrong.append("PCI Express %s, the kernel says %s" % (int(express), record["express"]))
        return wrong
    if not express:
        return wrong
    end, listed = walk(data)
    kept = record.get("capabilities", "").split(",") if record.get("capabilities") else []
    if record.get("walk") != end:
        wrong.append("the walk ends %s, the kernel's %s" % (end, record.get("walk")))
    if int(record.get("extended", "-1")) != len(listed):
        wrong.append("%u extended capabilities, the kernel counts %s" % (len(listed), record.get("extended")))
    if kept != listed[:len(kept)] or len(kept) != min(len(listed), 16):
        wrong.append("capabilities %s, the kernel names %s" % (",".join(listed) or "none", ",".join(kept) or "none"))
    if decoded is not None:
        theirs = decoded.get(key, [])
        ours = [(int(item.split("@")[1], 16), int(item.split("v")[1].split("@")[0], 16)) for item in listed]
        if theirs != ours:
            wrong.append("lspci decodes %s where the walk of the same bytes gives %s" % (theirs, ours))
    return wrong


def main():
    guest = open(sys.argv[1], "rb").read().decode("latin-1")
    kernel = open(sys.argv[2], "rb").read().decode("latin-1")
    functions, records = guest_functions(guest), kernel_records(kernel)
    decoded = lspci_extended(functions)
    if not functions:
        print("not ok the guest listed no function")
        sys.exit(1)
    failed, capabilities = 0, 0
    for number, (key, data) in enumerate(sorted(functions.items()), 1):
        wrong = compare(key, data, records.get(key), decoded)
        capabilities += len(walk(data)[1]) if is_express(data) else 0
        print("%s %u %02x:%02x.%x%s" % ("not ok" if wrong else "ok", number, key[0], key[1], key[2],
                                       "".join("\n    # " + item for item in wrong)))
        failed += bool(wrong)
    extra = sorted(set(records) - set(functions))
    for key in extra:
        print("not ok %02x:%02x.%x: a kernel record of a function Linux does not list" % key)
    print("# %u of %u functions agree, %u extended capabilities compared%s" % (
        len(functions) - failed, len(functions), capabilities,
        "" if decoded is not None else "; lspci is not installed, so no decoding was compared"))
    sys.exit(1 if failed or extra else 0)


if __name__ == "__main__":
    main()
