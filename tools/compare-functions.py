#!/usr/bin/env python3
"""Compares every function of two linked kernels, instruction by instruction, addresses set aside.

usage: tools/compare-functions.py <before.kernel> <after.kernel>

Each kernel is disassembled with i686-elf-objdump -d. A function's instructions are compared with their offsets
removed, a branch or call target reduced to the symbol it names, and an absolute address of the kernel's half
(0xc0000000 and up, or a displacement from -0x40000000 to -1, the same address signed) written ADDR, since a
function moved in one object shifts every address after it. Prints how many functions are identical, which differ
(with their first lines that differ), and which exist in one kernel only. A constant in the same range, such as
AMD's MSR 0xc0010299, is set aside with the addresses: a change of one would not be seen.
It answers whether a change that should not change the code, a refactor, compiles to the same instructions:
build the kernel before and after it, in the same profile, then compare the two kernel/lpl.kernel.
Exit code: 0 when every function of both kernels is identical, 1 otherwise, 2 on a wrong usage.
"""
import re
import subprocess
import sys

FUNCTION_HEADER = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
INSTRUCTION = re.compile(r"^\s*[0-9a-f]+:\s*(.*)$")
TARGET = re.compile(r"\b[0-9a-f]+ <([^>+]+)(\+0x[0-9a-f]+)?>")
KERNEL_ADDRESS = re.compile(r"0xc[0-9a-f]{7}\b|-0x[0-3][0-9a-f]{7}\b")


def functions_of(kernel):
    disassembly = subprocess.run(["i686-elf-objdump", "-d", "--no-show-raw-insn", kernel], capture_output=True,
                                 text=True, check=True).stdout
    functions = {}
    current = None
    for line in disassembly.splitlines():
        header = FUNCTION_HEADER.match(line)
        if header:
            current = functions.setdefault(header.group(1), [])
            continue
        instruction = INSTRUCTION.match(line)
        if current is None or instruction is None or not instruction.group(1):
            continue
        current.append(KERNEL_ADDRESS.sub("ADDR", TARGET.sub(r"<\1>", instruction.group(1))))
    return functions


def first_difference(before, after):
    for index, (old, new) in enumerate(zip(before, after)):
        if old != new:
            return f"line {index + 1}: {old!r} -> {new!r}"
    return f"{len(before)} lines -> {len(after)} lines"


def main():
    if len(sys.argv) != 3 or "--help" in sys.argv:
        print(__doc__)
        return 2
    before, after = functions_of(sys.argv[1]), functions_of(sys.argv[2])
    if not before:
        print("no function in the first kernel: refusing to compare nothing")
        return 1
    common = sorted(before.keys() & after.keys())
    differ = [name for name in common if before[name] != after[name]]
    only_before = sorted(before.keys() - after.keys())
    only_after = sorted(after.keys() - before.keys())
    print(f"{len(common) - len(differ)} identical, {len(differ)} differ, {len(only_before)} only before, "
          f"{len(only_after)} only after")
    for name in differ:
        print(f"differ {name}: {first_difference(before[name], after[name])}")
    for name in only_before:
        print(f"only before: {name}")
    for name in only_after:
        print(f"only after: {name}")
    return 0 if not differ and not only_before and not only_after else 1


if __name__ == "__main__":
    sys.exit(main())
