# tools

Each script prints its usage with `--help`. This page says where each kind of tool lives.

| Where | What |
|---|---|
| `instructions.sh` | counts the instructions a window of a kernel boot executes, and checks them against a budget |
| `parity.sh` | compares the KTAP records of two runs: the host against ring 0, or two runs of the same harness |
| `deps.sh` | the sibling checkouts against `DEPENDENCIES.lock` |
| `unbuilt-branches.sh`, `unbuilt-branches.declared` | the sources, branches and macros no gate profile builds, and why |
| `nvme-test-image.py` | the disk image the NVMe driver test reads |
| `budgets/` | the counts `instructions.sh --budget` checks: the ACPI windows and the server's tick |
| `acpi/corpus.sh` | the ACPI layer on the recorded firmware: fetch, check, ring 0 against the host, the oracles |
| `acpi/host/` | the sources of the ACPI host harness, `test-acpi` |
| `acpi/oracles/` | the scripts and the program that compare the layer with ACPICA, uACPI, iasl and Linux |
| `acpi/lists/` | the machine lists, the NVS presets and the decided divergences those runs read |
| `acpi/*.sh` | the checks CI runs: portability, the release objects, the generated fixtures, the iasl sandbox |
| `acpi/boot-cst.sh`, `acpi/boot-cst.asl`, `acpi/boot-cst-ports.asl` | a kernel booted on QEMU with a made-up `_CST`, MWAIT states or AMD's port states (`--ports`), and the C-states and sleep depths it prints |
| `pci/against-linux.sh` | each PCI function the kernel reads through ECAM against what Linux reads of it, on the same QEMU q35 |
| `qemu/` | the QEMU plugin `instructions.sh` counts with |
| `xmake/` | the xmake module that builds the kernel image |
