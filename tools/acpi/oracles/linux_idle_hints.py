"""The latency and the residency of each MWAIT hint a CPU slot's C-states target, worked out apart from the kernel's
code from the `aml idle slot` and `aml idle state` lines tools/acpi/oracles/acpiexec_idle.py prints, as the
`aml idle hint` lines `test-acpi --loaded` prints after each slot's states.

Usage: python3 -I tools/acpi/oracles/acpiexec_idle.py ... | python3 -I tools/acpi/oracles/linux_idle_hints.py
       python3 -I tools/acpi/oracles/linux_idle_hints.py --self-test

Every line read is printed again, and after a slot's last state, one line per hint its states target, in increasing
order: `aml idle hint <slot> <hint> latency=<us> residency=<us>`. The rules are Linux's, v6.12:

  - a state of FFixedHW (space 0x7F) whose class, the register's bit offset, is 2 is entered by MWAIT with its address
    as the hint (`acpi_processor_ffh_cstate_probe`, arch/x86/kernel/acpi/cstate.c), and targets the C-state in that
    hint's bits 7:4 (`MWAIT_HINT2CSTATE`, arch/x86/include/asm/mwait.h);
  - any other state of type 1 is C1, hint 0, whatever its register: Linux enters a C1 of FFixedHW whose MWAIT it
    refuses by HLT (`acpi_processor_evaluate_cst`, drivers/acpi/acpi_processor.c), and the kernel enters C1 by HLT or
    MWAIT 0; a state of type 1 that MWAIT enters at a deeper hint targets that hint alone;
  - a state resides as long as its latency, three times its latency when its type is past 1
    (`intel_idle_init_cstates_acpi`, drivers/idle/intel_idle.c), a type 0, which ACPI forbids, residing its latency; a
    residency past 32 bits is taken as the largest one, where Linux's unsigned int would wrap;
  - a hint several states target takes the longest latency and the longest residency of them.

--self-test checks those rules on made-up slots, each expected line written by hand, and exits 1 when one differs.
"""
import re
import sys

FIXED_HARDWARE = 0x7F
NATIVE_CSTATE_BEYOND_HALT = 2
C1 = 1
DEEPER_FACTOR = 3
LARGEST = 0xFFFFFFFF
SLOT = re.compile(r"^aml idle slot (\d+) ")
STATE = re.compile(r"^aml idle state (\d+) \d+ type=(\d+) latency=(\d+) power=\d+ space=0x([0-9A-Fa-f]+) width=\d+ "
                   r"offset=(\d+) access=\d+ address=0x([0-9A-Fa-f]+)$")


def target(kind, space, offset, address):
    """The hint a state targets, or None: MWAIT's when it enters by MWAIT, 0 for any other C1."""
    if space == FIXED_HARDWARE and offset == NATIVE_CSTATE_BEYOND_HALT:
        return (address >> 4) & 0xF
    return 0 if kind == C1 else None


def residency(kind, latency):
    """The residency intel_idle gives a _CST state."""
    return latency if kind <= C1 else min(latency * DEEPER_FACTOR, LARGEST)


def hint_lines(slot, states):
    """The `aml idle hint` lines of a slot whose states are (type, latency, space, offset, address)."""
    longest = {}
    for kind, latency, space, offset, address in states:
        hint = target(kind, space, offset, address)
        if hint is None:
            continue
        previous = longest.get(hint, (0, 0))
        longest[hint] = (max(previous[0], latency), max(previous[1], residency(kind, latency)))
    return ["aml idle hint %u %u latency=%u residency=%u" % (slot, hint, latency, resides)
            for hint, (latency, resides) in sorted(longest.items())]


def annotate(lines):
    """Every line of `lines`, with the hint lines of each slot after its last state."""
    out, slot, states = [], None, []
    for line in lines:
        if slot is not None and not STATE.match(line):
            out.extend(hint_lines(slot, states))
            slot, states = None, []
        out.append(line)
        slot_match, state_match = SLOT.match(line), STATE.match(line)
        if slot_match:
            slot, states = int(slot_match.group(1)), []
        elif state_match and slot is not None:
            states.append((int(state_match.group(2)), int(state_match.group(3)), int(state_match.group(4), 16),
                           int(state_match.group(5)), int(state_match.group(6), 16)))
    if slot is not None:
        out.extend(hint_lines(slot, states))
    return out


def state(slot, index, kind, latency, space, offset, address):
    """A made-up `aml idle state` line."""
    return ("aml idle state %u %u type=%u latency=%u power=0 space=0x%02X width=1 offset=%u access=1 address=0x%X"
            % (slot, index, kind, latency, space, offset, address))


def self_test():
    """Checks annotate() on made-up slots; returns the count of cases that differ."""
    ffh, io = FIXED_HARDWARE, 0x01
    cases = [
        ("C1 by halt and MWAIT 0, C3 by two sub-states, C2 by a port",
         [state(0, 1, 1, 1, ffh, 0, 0), state(0, 2, 1, 2, ffh, 2, 0x00), state(0, 3, 3, 150, ffh, 2, 0x21),
          state(0, 4, 3, 133, ffh, 2, 0x20), state(0, 5, 2, 70, io, 0, 0x414)],
         ["aml idle hint 0 0 latency=2 residency=2", "aml idle hint 0 2 latency=150 residency=450"]),
        ("a C2-type sub-state of C1 resides longer than the longer C1",
         [state(1, 1, 1, 10, ffh, 2, 0x00), state(1, 2, 2, 5, ffh, 2, 0x01)],
         ["aml idle hint 1 0 latency=10 residency=15"]),
        ("a C1 of class 1, a halt after an I/O read, is C1",
         [state(2, 1, 1, 3, ffh, 1, 0x414)], ["aml idle hint 2 0 latency=3 residency=3"]),
        ("three times a latency past 32 bits is the largest residency",
         [state(3, 1, 2, 0x55555556, ffh, 2, 0x10)], ["aml idle hint 3 1 latency=1431655766 residency=4294967295"]),
        ("a deeper MWAIT state of type 1 targets its hint alone", [state(4, 1, 1, 4, ffh, 2, 0x20)],
         ["aml idle hint 4 2 latency=4 residency=4"]),
        ("a state of type 0, which ACPI forbids, resides its latency", [state(5, 1, 0, 10, ffh, 2, 0x30)],
         ["aml idle hint 5 3 latency=10 residency=10"]),
        ("a slot of no state targets no hint", [], []),
    ]
    failures = 0
    for slot, (what, states, expected) in enumerate(cases):
        slot_line = "aml idle slot %u \\_PR.CPU%u states=%u skipped=0 handshake=none" % (slot, slot, len(states))
        got = annotate([slot_line] + states)[1 + len(states):]
        if got != expected:
            print("self-test: %s gives %s, expected %s" % (what, got, expected))
            failures += 1
    print("self-test: pass" if failures == 0 else "self-test: %d failure(s)" % failures)
    return failures


def main():
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(1 if self_test() else 0)
    if sys.argv[1:]:
        sys.exit(__doc__)
    for line in annotate(sys.stdin.read().splitlines()):
        print(line)


if __name__ == "__main__":
    main()
