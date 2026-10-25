"""The lines `test-acpi --machine <dump>` must print after the MADT, from a walker that is not the kernel's.

Usage: python3 -I tools/acpi/expected_machine_language.py <dump>

The table count, the AML bytes, the fault count, the method calls and every opcode count come from aml_census.py,
a walker of the AML grammar in Python that loads every DSDT and SSDT of the dump into one namespace, as the kernel
does. The opcodes are printed in the kernel's descriptor order: one-byte opcodes by encoding, then extended ones.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import aml_census  # noqa: E402


def descriptor_order():
    """The census's opcode names in the kernel's descriptor order."""
    return ([name for _, name in sorted(aml_census.ONE_BYTE_OPCODES.items())]
            + [name for _, name in sorted(aml_census.EXTENDED_OPCODES.items())])


def expected(dump):
    """The lines for a dump."""
    census = aml_census.census_of_machine(dump, aml_census.load_inputs(dump), 5)
    opcodes = census["opcodes"]
    lines = ["aml tables=%u bytes=%u decode_errors=%u calls=%u" % (
        len(census["tables"]), census["aml_bytes"], census["parse_errors"], census["methods"]["calls_resolved"])]
    lines += ["aml opcode %s %u" % (name, opcodes[name]) for name in descriptor_order() if opcodes.get(name)]
    return lines


def main():
    print("\n".join(expected(sys.argv[1])))


if __name__ == "__main__":
    main()
