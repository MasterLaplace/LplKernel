"""Compares the C-states a real machine's kernel published, its `[LPLTLM] acpi.cst` records, with the idle states Linux
took from the same machine's _CST (specification §8.8).

Usage: python3 -I tools/acpi/against_linux_cpuidle.py <serial log> <saved directory>
       python3 -I tools/acpi/against_linux_cpuidle.py --self-test

The serial log is that of one boot of a debug or release kernel on the machine; each `acpi.cst` record gives a CPU
slot, its APIC ID and the states its _CST published, `stateN=type:latency:power:space:width:offset:access:address` in
hexadecimal. The saved directory holds, under Linux on the same machine, `cpuidle/current_driver` and
`cpu<N>/cpuidle/state<K>/{name,desc,latency,residency}` of /sys/devices/system/cpu, and `cpuinfo`, a copy of
/proc/cpuinfo, whose first apicid, cpu0's, binds Linux's states to the record of that slot:

    mkdir saved && cd /sys/devices/system/cpu &&
        cp --parents cpuidle/current_driver cpu[0-9]*/cpuidle/state*/{name,desc,latency,residency} ~/saved/ &&
        cp /proc/cpuinfo ~/saved/cpuinfo

Linux takes its states from ACPI when current_driver is acpi_idle; intel_idle names its own (`intel_idle.max_cstate=0`
on Linux's command line leaves them to acpi_idle). acpi_idle reads the _CST of its first CPU, cpu0, and gives every
CPU those states (v7.3 acpi_processor_register_idle_driver), so cpu0's are compared with the record of cpu0's APIC ID:

  - each state but POLL comes from the record's first state of the register its desc names: `ACPI FFH MWAIT 0x<hint>`
    a Functional Fixed Hardware one, `ACPI IOPORT 0x<port>` a SystemIO one, of the low 32 bits of the record's address;
    `ACPI HLT` a C1 of Functional Fixed Hardware whose MWAIT Linux did not use, or, when the record's first state is no
    C1, the C1 Linux adds of its own;
  - its latency is the record's, in microseconds, or Linux sorted the latencies of the states it kept, as it does
    when they are out of order (v7.3 acpi_processor_power_verify);
  - its residency is twice its latency, Linux's latency_factor;
  - a state of the record Linux has not is listed and fails nothing: acpi_idle leaves out a C2 of address 0, a C3
    without bus-master control, an MWAIT hint the CPU does not support and a space other than those two.

Prints one line per Linux state, `ok` or `not ok`, the states Linux leaves out, then the counts. Exits 0 when every
state of Linux agrees, 1 when one does not, 2 on a usage error or when the inputs cannot be compared: a missing or
unreadable file, a log of no record or of two records of one slot, a driver other than acpi_idle, or states Linux took
from the processor's P_BLK, without _CST.

--self-test checks the comparison on logs and saved directories written by hand, each verdict too, and exits 1 when one
differs.
"""
import os
import re
import sys
import tempfile


FIXED_HARDWARE, SYSTEM_IO = 0x7F, 0x01
SPACES = {FIXED_HARDWARE: "FFH", SYSTEM_IO: "SystemIO"}
LATENCY_FACTOR = 2
LOW_32_BITS = 0xFFFFFFFF
RECORD = re.compile(r"\[LPLTLM\] acpi\.cst (.*)$")
FIXED_HARDWARE_STATE = re.compile(r"^ACPI FFH MWAIT 0x([0-9a-fA-F]+)$")
SYSTEM_IO_STATE = re.compile(r"^ACPI IOPORT 0x([0-9a-fA-F]+)$")
HALT_STATE = "ACPI HLT"
PROCESSOR_BLOCK_STATE = re.compile(r"^ACPI P_LVL[23] IOPORT 0x")
STATE_DIRECTORY = re.compile(r"^state(\d+)$")
CPUINFO_APIC_ID = re.compile(r"^apicid\s*:\s*(\d+)")
OWN = "own"


class Refused(Exception):
    """The inputs cannot be compared: the message says which and why."""


def records(log):
    """Each slot's APIC ID and published states, (index, type, latency, space, address), from the acpi.cst records of
    the log text @p log."""
    found = {}
    for line in log.splitlines():
        match = RECORD.search(line)
        if match is None:
            continue
        fields = dict(field.split("=", 1) for field in match.group(1).split() if "=" in field)
        try:
            slot, apic_id, states = int(fields["slot"]), int(fields["apic_id"]), []
            for index in range(1, int(fields["states"]) + 1):
                kind, latency, _, space, _, _, _, address = (int(value, 16)
                                                             for value in fields["state%u" % index].split(":"))
                states.append((index, kind, latency, space, address))
        except (KeyError, ValueError):
            raise Refused("the log holds an acpi.cst record that cannot be read: %s" % line.strip()[:160])
        if slot in found:
            raise Refused("the log holds two acpi.cst records of slot %u: give the log of one boot" % slot)
        found[slot] = (apic_id, states)
    if not found:
        raise Refused("no acpi.cst record in the serial log")
    return found


def attribute(saved, *parts):
    """The text of the file @p parts of the saved directory, stripped; Refused when it is missing or unreadable."""
    path = os.path.join(saved, *parts)
    try:
        with open(path, encoding="latin-1") as file:
            return file.read().strip()
    except FileNotFoundError:
        raise Refused("no %s in %s" % (os.path.join(*parts), saved))
    except OSError as error:
        raise Refused("%s cannot be read: %s" % (path, error.strerror))


def number_attribute(saved, *parts):
    """A file of the saved directory that holds a decimal number, as an integer."""
    text = attribute(saved, *parts)
    if not text.isdigit():
        raise Refused("%s in %s holds %r, no number" % (os.path.join(*parts), saved, text[:40]))
    return int(text)


def boot_processor_apic_id(saved):
    """The APIC ID of Linux's cpu0, the first processor the saved cpuinfo lists."""
    for line in attribute(saved, "cpuinfo").splitlines():
        identified = CPUINFO_APIC_ID.match(line)
        if identified:
            return int(identified.group(1))
    raise Refused("cpuinfo in %s gives no apicid" % saved)


def linux_states(saved):
    """cpu0's states but POLL, in their order, each (number, name, desc, latency, residency)."""
    directory = os.path.join(saved, "cpu0", "cpuidle")
    if not os.path.isdir(directory):
        raise Refused("no cpu0/cpuidle in %s" % saved)
    numbers = sorted(int(match.group(1)) for match in map(STATE_DIRECTORY.match, os.listdir(directory)) if match)
    states = []
    for number in numbers:
        state = ("cpu0", "cpuidle", "state%u" % number)
        states.append((number, attribute(saved, *state, "name"), attribute(saved, *state, "desc"),
                       number_attribute(saved, *state, "latency"), number_attribute(saved, *state, "residency")))
    states = [state for state in states if state[1] != "POLL"]
    if not states:
        raise Refused("cpu0/cpuidle in %s holds no state but POLL" % saved)
    return states


def register_of(desc):
    """The (space, address) a Linux state's desc names, or None for HLT; Refused for a state of the processor's
    P_BLK, or a desc acpi_idle does not write."""
    for space, pattern in ((FIXED_HARDWARE, FIXED_HARDWARE_STATE), (SYSTEM_IO, SYSTEM_IO_STATE)):
        match = pattern.match(desc)
        if match:
            return space, int(match.group(1), 16)
    if desc == HALT_STATE:
        return None
    if PROCESSOR_BLOCK_STATE.match(desc):
        raise Refused("Linux took its state %r from the processor's P_BLK, without _CST: nothing to compare" % desc)
    raise Refused("Linux's state desc %r is none acpi_idle writes" % desc)


def match_states(theirs, ours):
    """For each Linux state, the first of the record's states with its register, OWN for the C1 Linux adds, or
    None."""
    matched = []
    for _, _, desc, _, _ in theirs:
        register = register_of(desc)
        if register is None:
            wanted = [state for state in ours if state[1] == 1 and state[3] == FIXED_HARDWARE]
        else:
            wanted = [state for state in ours if (state[3], state[4] & LOW_32_BITS) == register]
        if wanted:
            matched.append(wanted[0])
        elif register is None and not matched and ours and ours[0][1] != 1:
            matched.append(OWN)
        else:
            matched.append(None)
    return matched


def verdict(state, kernel, sorted_by_linux):
    """Whether a Linux state agrees with the record's state it comes from, and why."""
    _, _, _, latency, residency = state
    if kernel is None:
        return False, "no state of the record has its register"
    if kernel == OWN:
        return True, "Linux's own, since the record's first state is no C1"
    if residency != LATENCY_FACTOR * latency:
        return False, "residency %u us, not twice its latency %u us" % (residency, latency)
    if latency == kernel[2]:
        return True, "the record's state %u, latency %u us" % (kernel[0], latency)
    if sorted_by_linux:
        return True, "the record's state %u, latency %u us, as Linux sorts them" % (kernel[0], latency)
    return False, "latency %u us, the record's state %u %u us" % (latency, kernel[0], kernel[2])


def compare(log, saved):
    """The exit status and the lines comparing the records of the serial log text @p log with the saved directory
    @p saved; Refused when they cannot be compared."""
    found = records(log)
    driver = attribute(saved, "cpuidle", "current_driver")
    if driver != "acpi_idle":
        raise Refused("Linux took its states from %s, not from ACPI: boot it with intel_idle.max_cstate=0, so that "
                      "acpi_idle takes them from _CST" % driver)
    apic_id = boot_processor_apic_id(saved)
    slots = sorted(slot for slot, (identifier, _) in found.items() if identifier == apic_id)
    if not slots:
        raise Refused("no acpi.cst record of APIC ID %u, Linux's cpu0" % apic_id)
    ours, theirs = found[slots[0]][1], linux_states(saved)
    matched = match_states(theirs, ours)
    pairs = [(state[3], kernel[2]) for state, kernel in zip(theirs, matched) if kernel not in (None, OWN)]
    sorted_by_linux = [linux for linux, _ in pairs] == sorted(kernel for _, kernel in pairs)
    lines, agreeing = [], 0
    for count, (state, kernel) in enumerate(zip(theirs, matched)):
        agrees, why = verdict(state, kernel, sorted_by_linux)
        agreeing += 1 if agrees else 0
        lines.append("%s %u cpu0 state%u %s %s: %s" % ("ok" if agrees else "not ok", count + 1, state[0], state[1],
                                                        state[2], why))
    for index, kind, _, space, address in (state for state in ours if state not in matched):
        lines.append("# the record's state %u (type %u, %s 0x%X) is not among Linux's states, which acpi_idle may "
                     "leave out" % (index, kind, SPACES.get(space, "space 0x%02X" % space), address))
    lines.append("# cpuidle: %u of Linux's %u states agree with slot %u (APIC ID %u), Linux's cpu0"
                 % (agreeing, len(theirs), slots[0], apic_id))
    return (0 if agreeing == len(theirs) else 1), lines


def record_state(index, kind, latency, space, address):
    """One `stateN=` field of an acpi.cst record, power 1000 and a register of width 8 for SystemIO."""
    width = 8 if space == SYSTEM_IO else 0
    return "state%u=%08X:%08X:%08X:%02X:%02X:00:00:%016X" % (index, kind, latency, 1000, space, width, address)


def record(states, slot=0, apic_id=0, evaluated=1):
    """An acpi.cst record line of @p states, each (type, latency, space, address)."""
    fields = " ".join(record_state(index + 1, *state) for index, state in enumerate(states))
    return ("[LPLTLM] acpi.cst slot=%u apic_id=%u uid=%u evaluated=%u states=%u skipped=0 %s"
            % (slot, apic_id, slot, evaluated, len(states), fields)).rstrip()


def write_saved(directory, driver, states, cpus=1, apic_ids=None, cpuinfo=True):
    """Writes a saved directory: @p driver, then @p states, each (name, desc, latency, residency), for each CPU, POLL
    first, and a cpuinfo of @p apic_ids, CPU 0 the first."""
    os.makedirs(os.path.join(directory, "cpuidle"))
    with open(os.path.join(directory, "cpuidle", "current_driver"), "w") as file:
        file.write(driver + "\n")
    listed = [("POLL", "CPUIDLE CORE POLL IDLE", 0, 0)] + states
    for cpu in range(cpus):
        for index, (name, desc, latency, residency) in enumerate(listed):
            state = os.path.join(directory, "cpu%u" % cpu, "cpuidle", "state%u" % index)
            os.makedirs(state)
            for attribute, value in (("name", name), ("desc", desc), ("latency", latency), ("residency", residency)):
                with open(os.path.join(state, attribute), "w") as file:
                    file.write("%s\n" % value)
    if cpuinfo:
        with open(os.path.join(directory, "cpuinfo"), "w") as file:
            for cpu, apic_id in enumerate(apic_ids or range(cpus)):
                file.write("processor\t: %u\nvendor_id\t: GenuineIntel\napicid\t\t: %u\ninitial apicid\t: %u\n\n"
                           % (cpu, apic_id, apic_id))


def self_test():
    """Checks compare() on hand-written logs and saved directories; each case's exit status and the lines it must
    print, or the words of its refusal, are written by hand. Returns the count of cases that differ."""
    c1, c2, c3 = (1, 1, FIXED_HARDWARE, 0x00), (2, 80, SYSTEM_IO, 0x414), (3, 109, SYSTEM_IO, 0x415)
    linux_c1, linux_c2 = ("C1", "ACPI FFH MWAIT 0x0", 1, 2), ("C2", "ACPI IOPORT 0x414", 80, 160)
    linux_c3 = ("C3", "ACPI IOPORT 0x415", 109, 218)
    agree = [linux_c1, linux_c2, linux_c3]
    cases = [
        ("the states of one _CST", record([c1, c2, c3]), "acpi_idle", agree, {}, 0,
         ["ok 1 cpu0 state1 C1 ACPI FFH MWAIT 0x0: the record's state 1, latency 1 us",
          "ok 2 cpu0 state2 C2 ACPI IOPORT 0x414: the record's state 2, latency 80 us",
          "ok 3 cpu0 state3 C3 ACPI IOPORT 0x415: the record's state 3, latency 109 us",
          "# cpuidle: 3 of Linux's 3 states agree with slot 0 (APIC ID 0), Linux's cpu0"]),
        ("a latency one more than the record's", record([c1, c2, c3]), "acpi_idle",
         [linux_c1, ("C2", "ACPI IOPORT 0x414", 81, 162), linux_c3], {}, 1,
         ["not ok 2 cpu0 state2 C2 ACPI IOPORT 0x414: latency 81 us, the record's state 2 80 us"]),
        ("a residency other than twice the latency", record([c1, c2, c3]), "acpi_idle",
         [linux_c1, ("C2", "ACPI IOPORT 0x414", 80, 400), linux_c3], {}, 1,
         ["not ok 2 cpu0 state2 C2 ACPI IOPORT 0x414: residency 400 us, not twice its latency 80 us"]),
        ("a C3 Linux leaves out", record([c1, c2, c3]), "acpi_idle", [linux_c1, linux_c2], {}, 0,
         ["# the record's state 3 (type 3, SystemIO 0x415) is not among Linux's states, which acpi_idle may leave out",
          "# cpuidle: 2 of Linux's 2 states agree with slot 0 (APIC ID 0), Linux's cpu0"]),
        ("a state of Linux the record lacks", record([c1, c2]), "acpi_idle", agree, {}, 1,
         ["not ok 3 cpu0 state3 C3 ACPI IOPORT 0x415: no state of the record has its register"]),
        ("latencies out of order, which Linux sorts",
         record([c1, (2, 109, SYSTEM_IO, 0x414), (3, 80, SYSTEM_IO, 0x415)]), "acpi_idle", agree, {}, 0,
         ["ok 2 cpu0 state2 C2 ACPI IOPORT 0x414: the record's state 2, latency 80 us, as Linux sorts them"]),
        ("a C1 of FFH Linux enters with HLT", record([c1, c2]), "acpi_idle",
         [("C1", "ACPI HLT", 1, 2), linux_c2], {}, 0,
         ["ok 1 cpu0 state1 C1 ACPI HLT: the record's state 1, latency 1 us"]),
        ("a first state no C1, so that Linux adds its own", record([c2, c3]), "acpi_idle",
         [("C1", "ACPI HLT", 0, 0), linux_c2, linux_c3], {}, 0,
         ["ok 1 cpu0 state1 C1 ACPI HLT: Linux's own, since the record's first state is no C1"]),
        ("an FFH address above 32 bits, whose low 32 Linux prints", record([(1, 1, FIXED_HARDWARE, 0x100000000), c2]),
         "acpi_idle", [linux_c1, linux_c2], {}, 0,
         ["ok 1 cpu0 state1 C1 ACPI FFH MWAIT 0x0: the record's state 1, latency 1 us"]),
        ("a register of another space at the same address", record([c1, (2, 80, FIXED_HARDWARE, 0x414)]), "acpi_idle",
         [linux_c1, linux_c2], {}, 1,
         ["not ok 2 cpu0 state2 C2 ACPI IOPORT 0x414: no state of the record has its register"]),
        ("a slot with no _CST evaluated", record([], evaluated=0), "acpi_idle", agree, {}, 1,
         ["not ok 1 cpu0 state1 C1 ACPI FFH MWAIT 0x0: no state of the record has its register"]),
        ("Linux's cpu0 at APIC ID 2, the record's slot 1",
         record([c1], slot=0, apic_id=0) + "\n" + record([c1, c2, c3], slot=1, apic_id=2), "acpi_idle", agree,
         {"cpus": 2, "apic_ids": [2, 0]}, 0,
         ["# cpuidle: 3 of Linux's 3 states agree with slot 1 (APIC ID 2), Linux's cpu0"]),
        ("states another driver named", record([c1, c2, c3]), "intel_idle", agree, {}, "Refused",
         ["Linux took its states from intel_idle, not from ACPI"]),
        ("no copy of /proc/cpuinfo", record([c1, c2, c3]), "acpi_idle", agree, {"cpuinfo": False}, "Refused",
         ["no cpuinfo in"]),
        ("two records of one slot", record([c1]) + "\n" + record([c1]), "acpi_idle", agree, {}, "Refused",
         ["two acpi.cst records of slot 0"]),
        ("no record", "[LPLTLM] acpi.init status=ok", "acpi_idle", agree, {}, "Refused",
         ["no acpi.cst record in the serial log"]),
        ("states Linux took from P_BLK", record([]), "acpi_idle",
         [linux_c1, ("C2", "ACPI P_LVL2 IOPORT 0x414", 80, 160)], {}, "Refused", ["from the processor's P_BLK"]),
    ]
    failures = 0
    for what, log, driver, states, options, expected_status, expected_lines in cases:
        with tempfile.TemporaryDirectory() as work:
            saved = os.path.join(work, "saved")
            write_saved(saved, driver, states, **options)
            try:
                status, lines = compare(log, saved)
                missing = [line for line in expected_lines if line not in lines]
            except Refused as refused:
                status, lines = "Refused", [str(refused)]
                missing = [words for words in expected_lines if words not in lines[0]]
        if status != expected_status or missing:
            print("self-test: %s gives %s, %s; expected %s with %s" % (what, status, lines, expected_status, missing))
            failures += 1
    print("self-test: pass" if failures == 0 else "self-test: %d failure(s)" % failures)
    return failures


def main():
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(1 if self_test() else 0)
    if len(sys.argv) != 3:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        sys.exit(2)
    try:
        with open(sys.argv[1], encoding="latin-1") as log:
            status, lines = compare(log.read(), sys.argv[2])
    except (Refused, OSError) as refused:
        print("not compared: %s" % refused)
        sys.exit(2)
    print("\n".join(lines))
    sys.exit(status)


if __name__ == "__main__":
    main()
