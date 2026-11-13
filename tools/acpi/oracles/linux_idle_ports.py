"""The C-states a CPU slot's processor enters by a port read where MWAIT reaches no state past C1, worked out apart
from the kernel's code from the `aml idle slot` and `aml idle state` lines tools/acpi/oracles/acpiexec_idle.py prints and
the FADT tools/acpi/oracles/expected_fadt.py reads, as the `aml idle port` and `aml idle ports` lines
`test-acpi --loaded` prints after each slot's hints.

Usage: python3 -I tools/acpi/oracles/acpiexec_idle.py ... | python3 -I tools/acpi/oracles/linux_idle_ports.py [--fadt F]
       python3 -I tools/acpi/oracles/linux_idle_ports.py --self-test

Every line read is printed again, and after a slot's last state and its hint lines, one line per state the processor
enters by a port read, in _CST's order, `aml idle port <slot> <depth> port=0x<port> type=<2 or 3> latency=<us>`, the
first one depth 1, since depth 0 is C1; then `aml idle ports <slot> given=<n> refused=<n>`. F holds the `fadt` lines
expected_fadt.py prints; without it, no port lies in the FADT's registers and no PM1 status register is given. The
rules are Linux's acpi_idle, v6.12, on a processor whose MWAIT enumerates no state past C1 and that enters C3 behind
the bus-master check alone (bm_check set, bm_control clear, as on AMD from family 0x17,
`acpi_processor_power_init_bm_check`, arch/x86/kernel/acpi/cstate.c):

  - a state of FFixedHW or SystemIO is kept, and any other space is skipped (`acpi_processor_evaluate_cst`,
    drivers/acpi/acpi_processor.c); a kept state of type 1 is valid; one of type 2 is valid when it is a port read of a
    port other than 0 (`acpi_processor_power_verify`, drivers/acpi/processor_idle.c), and one of FFixedHW is not, since
    MWAIT reaches no state past C1 (`acpi_processor_ffh_cstate_probe_cpu`, arch/x86/kernel/acpi/cstate.c); a C3 is
    valid as a C2 is, since _CST gives it (`acpi_processor_power_verify_c3`); no other type is;
  - when a valid state's type is at least the previous valid state's and its latency below that state's, the
    latencies of the valid states are sorted into increasing order, each state keeping its place and its register
    (`acpi_cst_latency_sort`);
  - each valid state of type 2 or 3 is printed, with its type and its sorted latency.

The kernel's own rules, where Linux has none: a C3 needs a PM1 status register to read BM_STS from, PM1a's event block
of 4 bytes at least and PM1b's, when it gives one, both port ranges that reach no protected port; a port state whose
type is below that of one kept before it is not valid, so that the depths deepen with their order; a port past 0xFFFF,
which
Linux would cut to its low 16 bits, is not valid, nor is one in a register block of the FADT (PM1 event, PM1 control,
PM timer, GPE blocks, the reset register) or its SMI_CMD, one of a device the kernel drives (the PIT, 0x40-0x43; the
keyboard controller, 0x60 and 0x64; CMOS, 0x70-0x71; COM1, 0x3F8-0x3FF), or one the ACPI layer protects (the two
8259s, 0x20-0x21 and 0xA0-0xA1; the ELCR, 0x4D0-0x4D1; PCI configuration mechanism #1, 0xCF8-0xCFF); six states are
printed at most, the depths 1 to 6. A port read of type 2 or 3 that is not printed is refused.

--self-test checks those rules on made-up slots, each expected line written by hand, and exits 1 when one differs.
"""
import re
import sys

SYSTEM_IO = 0x01
FIXED_HARDWARE = 0x7F
C1, C2, C3 = 1, 2, 3
MAX_PORT_DEPTHS = 6
PROTECTED_PORTS = ((0x20, 0x21), (0xA0, 0xA1), (0x4D0, 0x4D1), (0xCF8, 0xCFF))
DRIVEN_PORTS = ((0x40, 0x43), (0x60, 0x60), (0x64, 0x64), (0x70, 0x71), (0x3F8, 0x3FF))
FADT_BLOCKS = ("pm1a_event", "pm1b_event", "pm1a_control", "pm1b_control", "pm_timer", "reset")
SLOT = re.compile(r"^aml idle slot (\d+) ")
STATE = re.compile(r"^aml idle state (\d+) \d+ type=(\d+) latency=(\d+) power=\d+ space=0x([0-9A-Fa-f]+) width=\d+ "
                   r"offset=\d+ access=\d+ address=0x([0-9A-Fa-f]+)$")
HINT = re.compile(r"^aml idle hint (\d+) ")
FADT_HEADER = re.compile(r"^fadt present=1 .*smi_command=0x([0-9A-Fa-f]+) .*gpe0_length=(\d+) gpe1_length=(\d+) ")
FADT_BLOCK = re.compile(r"^fadt (\w+) space=(\d+) width=(\d+) offset=\d+ access=\d+ address=0x([0-9A-Fa-f]+)$")


def fadt_ranges(lines):
    """The I/O ports the FADT's registers and its SMI_CMD span, as (first, last), from expected_fadt.py's lines."""
    ranges, lengths, blocks = [], {}, {}
    for line in lines:
        header, block = FADT_HEADER.match(line), FADT_BLOCK.match(line)
        if header:
            smi = int(header.group(1), 16)
            if smi:
                ranges.append((smi, smi))
            lengths = {"gpe0": int(header.group(2)), "gpe1": int(header.group(3))}
        elif block:
            blocks[block.group(1)] = (int(block.group(2)), int(block.group(3)), int(block.group(4), 16))
    for name, (space, width, address) in blocks.items():
        size = lengths.get(name, 0) if name in ("gpe0", "gpe1") else (width + 7) // 8
        if space == SYSTEM_IO and address and size and (name in FADT_BLOCKS or name in lengths):
            ranges.append((address, address + size - 1))
    return ranges


def status_register(space, width, address, required):
    """Whether a PM1 event block gives a status register BM_STS is read from: a port range of 4 bytes at least that
    reaches no protected port; an absent block gives none, which only PM1b's may."""
    if address == 0:
        return not required
    return (space == SYSTEM_IO and width >= 32 and address + (width + 7) // 8 - 1 <= 0xFFFF
            and not within(address, PROTECTED_PORTS) and not within(address + 1, PROTECTED_PORTS))


def read_fadt(lines):
    """The FADT's port ranges and whether it gives a PM1 status register, from expected_fadt.py's lines."""
    blocks = {}
    for line in lines:
        block = FADT_BLOCK.match(line)
        if block:
            blocks[block.group(1)] = (int(block.group(2)), int(block.group(3)), int(block.group(4), 16))
    status = (status_register(*blocks.get("pm1a_event", (0, 0, 0)), True) and
              status_register(*blocks.get("pm1b_event", (0, 0, 0)), False))
    return fadt_ranges(lines), status


def within(port, ranges):
    """Whether @p port lies in one of @p ranges."""
    return any(first <= port <= last for first, last in ranges)


def readable(port, fadt):
    """Whether the kernel may read @p port to enter a C-state, beside the FADT ranges @p fadt."""
    return 0 < port <= 0xFFFF and not within(port, PROTECTED_PORTS + DRIVEN_PORTS) and not within(port, fadt)


def valid(kind, space, address, fadt, status):
    """Whether acpi_idle keeps a state, on a processor whose MWAIT reaches no state past C1 and that enters C3 behind
    the bus-master check, with the kernel's own rules: a C3 only beside a PM1 status register."""
    if kind == C1:
        return space in (FIXED_HARDWARE, SYSTEM_IO)
    return space == SYSTEM_IO and readable(address, fadt) and (kind == C2 or (kind == C3 and status))


def kept_states(states, fadt, status):
    """The indexes of the valid states of @p states, a port state whose type is below that of one kept before it
    left out."""
    kept, deepest = [], 0
    for index, (kind, _, space, address) in enumerate(states):
        if not valid(kind, space, address, fadt, status):
            continue
        if kind in (C2, C3):
            if kind < deepest:
                continue
            deepest = kind
        kept.append(index)
    return kept


def sorted_latencies(states, fadt, status):
    """The latency acpi_idle gives each valid state of @p states, (type, latency, space, address) in _CST's order."""
    kept = kept_states(states, fadt, status)
    latencies = [states[index][1] for index in kept]
    buggy, last_type, last_latency = False, 0, 0
    for index in kept:
        kind, latency = states[index][0], states[index][1]
        if kind >= last_type and latency < last_latency:
            buggy = True
        last_type, last_latency = kind, latency
    if buggy:
        latencies.sort()
    return dict(zip(kept, latencies))


def port_lines(slot, states, fadt, status):
    """The `aml idle port` and `aml idle ports` lines of a slot whose states are (type, latency, space, address)."""
    latencies = sorted_latencies(states, fadt, status)
    lines, refused = [], 0
    for index, (kind, _, space, address) in enumerate(states):
        if space == SYSTEM_IO and kind in (C2, C3) and index not in latencies:
            refused += 1
        elif index in latencies and kind in (C2, C3):
            if len(lines) == MAX_PORT_DEPTHS:
                refused += 1
                continue
            lines.append("aml idle port %u %u port=0x%04X type=%u latency=%u"
                         % (slot, len(lines) + 1, address, kind, latencies[index]))
    return lines + ["aml idle ports %u given=%u refused=%u" % (slot, len(lines), refused)]


def annotate(lines, fadt=(), status=False):
    """Every line of `lines`, with the port lines of each slot after its states and its hint lines."""
    out, slot, states = [], None, []
    for line in lines:
        if slot is not None and not STATE.match(line) and not HINT.match(line):
            out.extend(port_lines(slot, states, fadt, status))
            slot, states = None, []
        out.append(line)
        slot_match, state_match = SLOT.match(line), STATE.match(line)
        if slot_match:
            slot, states = int(slot_match.group(1)), []
        elif state_match and slot is not None:
            states.append((int(state_match.group(2)), int(state_match.group(3)), int(state_match.group(4), 16),
                           int(state_match.group(5), 16)))
    if slot is not None:
        out.extend(port_lines(slot, states, fadt, status))
    return out


def state(slot, index, kind, latency, space, address, offset=0):
    """A made-up `aml idle state` line."""
    return ("aml idle state %u %u type=%u latency=%u power=0 space=0x%02X width=8 offset=%u access=1 address=0x%X"
            % (slot, index, kind, latency, space, offset, address))


def ports(slot, given, refused, *entries):
    """The lines a slot that gives @p entries, (port, latency) of type 2 or (port, latency, type), and refuses
    @p refused, prints."""
    return (["aml idle port %u %u port=0x%04X type=%u latency=%u"
             % (slot, depth + 1, entry[0], entry[2] if len(entry) > 2 else C2, entry[1])
             for depth, entry in enumerate(entries)] +
            ["aml idle ports %u given=%u refused=%u" % (slot, given, refused)])


# The FADT lines expected_fadt.py prints for the Framework Laptop 16 of the corpus, its reset register moved from 0xB0,
# its SMI_CMD, to 0xCF9.
MADE_UP_FADT = [
    "fadt present=1 length=268 revision=5 hardware_reduced=0 flags=0x0020C5BD sci=9 smi_command=0xB0 enable=0xA0 "
    "pstate=0x00 cst=0x00 reset_value=0xFB gpe0_length=8 gpe1_length=0 gpe1_base=0",
    "fadt pm1a_event space=1 width=32 offset=0 access=3 address=0x400",
    "fadt pm1b_event space=0 width=0 offset=0 access=0 address=0x0",
    "fadt pm1a_control space=1 width=16 offset=0 access=2 address=0x404",
    "fadt pm_timer space=1 width=32 offset=0 access=3 address=0x408",
    "fadt gpe0 space=1 width=64 offset=0 access=4 address=0x420",
    "fadt reset space=1 width=8 offset=0 access=1 address=0xCF9",
]


def self_test():
    """Checks annotate() on made-up slots; returns the count of cases that differ."""
    ffh, io = FIXED_HARDWARE, SYSTEM_IO
    cases = [
        ("AMD's C1 by MWAIT, C2 by a port, C3 refused without a PM1 status register",
         [state(0, 1, 1, 1, ffh, 0, 2), state(0, 2, 2, 18, io, 0x414), state(0, 3, 3, 350, io, 0x415)],
         ports(0, 1, 1, (0x414, 18))),
        ("a C2 slower than the C3 after it, refused, keeps its latency",
         [state(1, 1, 1, 1, ffh, 0, 2), state(1, 2, 2, 400, io, 0x414), state(1, 3, 3, 350, io, 0x415)],
         ports(1, 1, 1, (0x414, 400))),
        ("two C2 of falling latency are sorted",
         [state(2, 1, 1, 1, ffh, 0, 2), state(2, 2, 2, 400, io, 0x414), state(2, 3, 2, 300, io, 0x416)],
         ports(2, 2, 0, (0x414, 300), (0x416, 400))),
        ("MWAIT states past C1, a type 0 and a type 4 are no port state; port 0, 0x10000 and a protected port are refused",
         [state(3, 1, 1, 1, ffh, 0, 2), state(3, 2, 2, 33, ffh, 0x10, 2), state(3, 3, 2, 40, io, 0),
          state(3, 4, 2, 41, io, 0xCF9), state(3, 5, 0, 42, io, 0x416), state(3, 6, 4, 43, io, 0x417),
          state(3, 7, 2, 44, io, 0x10000)], ports(3, 0, 3)),
        ("a port C1 is no port depth, and a sort counts it",
         [state(4, 1, 1, 30, io, 0x410), state(4, 2, 2, 18, io, 0x414)], ports(4, 1, 0, (0x414, 30))),
        ("seven port states give six depths, the seventh refused",
         [state(5, index + 1, 2, 10 + index, io, 0x500 + index) for index in range(7)],
         ports(5, 6, 1, *[(0x500 + index, 10 + index) for index in range(6)])),
        ("a C1 in memory is skipped, and so no sort counts it",
         [state(6, 1, 1, 30, 0x00, 0x1000), state(6, 2, 2, 18, io, 0x414)], ports(6, 1, 0, (0x414, 18))),
        ("a state of type 0, which no sort counts, leaves a later C2 its latency",
         [state(7, 1, 1, 1, ffh, 0, 2), state(7, 2, 0, 500, io, 0x416), state(7, 3, 2, 18, io, 0x414)],
         ports(7, 1, 0, (0x414, 18))),
        ("two C1 of one latency after a C2 of a higher type sort nothing",
         [state(8, 1, 2, 50, io, 0x414), state(8, 2, 1, 10, ffh, 0, 2), state(8, 3, 1, 10, ffh, 0, 2)],
         ports(8, 1, 0, (0x414, 50))),
        ("ports the kernel drives are refused",
         [state(9, 1, 2, 1, io, port) for port in (0x40, 0x43, 0x60, 0x64, 0x70, 0x71, 0x3F8)],
         ports(9, 0, 7)),
        ("a slot of no state enters none", [], ports(10, 0, 0)),
    ]
    fadt_cases = [
        ("ports in the FADT's blocks and its SMI_CMD are refused, the one past GPE0 is not",
         [state(11, 1, 2, 1, io, port) for port in (0x400, 0x403, 0x404, 0x405, 0x408, 0x40B, 0x420, 0x427, 0xB0,
                                                       0x428)],
         ports(11, 1, 9, (0x428, 1))),
        ("AMD's C2 and C3 beside a PM1 status register, the C3 of type 3",
         [state(12, 1, 1, 1, ffh, 0, 2), state(12, 2, 2, 18, io, 0x414), state(12, 3, 3, 350, io, 0x415)],
         ports(12, 2, 0, (0x414, 18), (0x415, 350, C3))),
        ("a C2 of 400 us before a C3 of 350 us: the sort counts the C3",
         [state(13, 1, 1, 1, ffh, 0, 2), state(13, 2, 2, 400, io, 0x414), state(13, 3, 3, 350, io, 0x415)],
         ports(13, 2, 0, (0x414, 350), (0x415, 400, C3))),
        ("a C2 after a C3 is refused, and sorts nothing",
         [state(14, 1, 1, 1, ffh, 0, 2), state(14, 2, 3, 350, io, 0x415), state(14, 3, 2, 18, io, 0x414)],
         ports(14, 1, 1, (0x415, 350, C3))),
        ("a C3 at the PM1a event block's own port is refused",
         [state(15, 1, 1, 1, ffh, 0, 2), state(15, 2, 2, 0, io, 0x416), state(15, 3, 3, 0, io, 0x400)],
         ports(15, 1, 1, (0x416, 1))),
    ]
    short_status_cases = [
        ("a C3 beside a PM1a event block of 2 bytes is refused",
         [state(16, 1, 1, 1, ffh, 0, 2), state(16, 2, 2, 18, io, 0x414), state(16, 3, 3, 350, io, 0x415)],
         ports(16, 1, 1, (0x414, 18))),
    ]
    failures = 0
    for slot, (what, states, expected) in enumerate(cases):
        failures += check(slot, what, states, expected, ((), False))
    for offset, (what, states, expected) in enumerate(fadt_cases):
        failures += check(len(cases) + offset, what, states, expected, read_fadt(MADE_UP_FADT))
    short = [line.replace("pm1a_event space=1 width=32", "pm1a_event space=1 width=16") for line in MADE_UP_FADT]
    for offset, (what, states, expected) in enumerate(short_status_cases):
        failures += check(len(cases) + len(fadt_cases) + offset, what, states, expected, read_fadt(short))
    print("self-test: pass" if failures == 0 else "self-test: %d failure(s)" % failures)
    return failures


def check(slot, what, states, expected, fadt):
    """1 when the slot of @p states gives other lines than @p expected, which it prints, else 0."""
    slot_line = "aml idle slot %u \\_PR.CPU%u states=%u skipped=0 handshake=none" % (slot, slot, len(states))
    got = annotate([slot_line] + states, *fadt)[1 + len(states):]
    if got != expected:
        print("self-test: %s gives %s, expected %s" % (what, got, expected))
        return 1
    return 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(1 if self_test() else 0)
    fadt = ((), False)
    if len(sys.argv) == 3 and sys.argv[1] == "--fadt":
        with open(sys.argv[2], encoding="latin-1") as lines:
            fadt = read_fadt(lines.read().splitlines())
    elif sys.argv[1:]:
        sys.exit(__doc__)
    for line in annotate(sys.stdin.read().splitlines(), *fadt):
        print(line)


if __name__ == "__main__":
    main()
