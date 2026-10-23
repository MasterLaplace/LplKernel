# LplKernel's ACPI layer and AML interpreter: specification

Status: adopted for #57 on 2026-10-24, when every recommendation of §10 was taken; each slice of §9 is a
sub-issue of #57. ACPI 6.6 still answered HTTP 403 that day, so its numbers are 6.5's and Q14 stays open. Written on 2026-10-23 from six reader reports (uACPI, ACPICA, LAI and rust-osdev/acpi,
the specification with QEMU's tables, 90 real machines, LplKernel's own needs), then revised after two checks: one
of every citation, one of completeness and coherence (what was not taken, and why, is in the Review notes at the
end). Nothing here is code yet.

**In one sentence.** LplKernel gets a from-scratch AML interpreter that executes the firmware's bytes in place
from the read-only firmware window, on fixed explicit stacks, with integers held by value and a hashed namespace
of 32-bit indices; it implements the opcodes that real firmware's path to `_CST` reaches (including `_OSI`,
`_INI`, `_OSC` and `Load`), fails loudly and counts everything else, and is judged by instruction counts and by
differential oracles on 114 real machines on the host, and on a quick list of them in ring 0. QEMU is today's
test bench, one corpus among others: it is neither the target nor the limit.

**How to read the labels.**

| Label | Meaning |
|---|---|
| [code] | read in the pinned source and checked there |
| [comment] | a README, a guide or a source comment says it; not checked against code |
| [measured] | counted by a script on pinned data; the script is named |
| [spec] | the ACPI specification says it (6.5 numbering, see below) |
| [claimed] | stated by a document that is not pinned here; to verify before relying on it |
| [estimate] | a design estimate of this spec, to be replaced by a measurement |
| [reading], [inferred] | a reader report's finding from reading code, or deduced from its paths; nothing was run |

**The specification text.** uefi.org answers HTTP 403 to every 6.6 page, so section numbers and permalinks are
those of the UEFI Forum's ACPI 6.5 reStructuredText source at
[UEFI/ACPI-Specification-Release@5dabe6f](https://github.com/UEFI/ACPI-Specification-Release/tree/5dabe6f37fbe753d90292a2a419105be5d55a778).
Two independent citations (rust-osdev's "§19.6.101", [mod.rs L2990-L2996](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L2990-L2996),
and uACPI's "6.6: 5.5.2.4.8", [types.h L411-L413](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/types.h#L411-L413))
match the recomputed 6.5 numbers, and ACPICA tags no AML grammar item as 6.6 (specification report §1.1).
Re-checking every number against 6.6 is open question Q14.

**The corpus.** "The 90 machines" are the census sample of `real-firmware.md` (appendix A); "the 62" are the 42
clients and 20 servers of `needs.md` (appendix A). Eight of those servers have only decoded text, and 30 of the
remaining 54 are already among the 90, so the runnable corpus is **114 distinct machines**
(`scripts/vc_bin_presence.py`, output next to it) [measured]. Counts out of 90 or 42 keep their own denominator.

**The reports this spec synthesises**, kept outside the repository with the study's data:
`uacpi.md`, `acpica.md`, `lai-and-rust-acpi.md`, `specification-and-qemu.md`, `real-firmware.md`, `needs.md`.
Numbers quoted without another source come from them, with their denominators. A script named `scripts/…` is one
of the study's: `select_sample.py` and `real_firmware_census.py` enter the repository with #533, `aml_census.py` and
`check_against_iasl.py` with #535, and the others were one-off counts.

**Two numbers recomputed for this spec.** The census tool of the specification report (`scripts/aml_census.py`)
was run over the 90 real-firmware dumps, and `scripts/synthesis/table_level.py` tabulated opcode prevalence by
level (definition block or method body), by modernity (FADT ≥ 6.0) and against QEMU's 42 configurations. Output:
`scripts/synthesis/opcode_levels.md`; 0 parse errors on 90 machines [measured]. Its whole-file counts agree with
the real-firmware report's own parser (for example `Load` 55/90, `External` 47/90, `QWordPrefix` 64/90).

---

## 1. Goals and non-goals

### 1.1 Principles

1. **From scratch.** uACPI, ACPICA, LAI and rust-osdev/acpi are read for ideas and run as differential oracles on
   the host. No line of their code is copied or ported. Each idea taken names its source.
2. **Real firmware decides, QEMU is one bench.** The interpreter is for modern firmware on desktops, notebooks,
   servers, mini PCs and tablets, mostly written and tested against Windows. QEMU 10.2.1 uses 62 of the 113
   opcodes, no `_OSI`, no `Sleep`, no `Load`, no table-level code, and 32-bit integers in all 42 `pc`/`q35`
   configurations (the 5 `microvm` ones are revision 2) (specification report §3). Every scope decision below
   cites the real-firmware corpus; a QEMU count is shown next to it and never decides alone. Real firmware runs
   in ring 0 too, not only on the host (§8.4).
3. **Only what LplKernel needs.** Every opcode is decoded, because an undecoded opcode makes the rest of its term
   list unreadable (specification report §3). Execution is written only for the opcodes that the needs path of
   the corpus reaches (§4.1); the rest fails loudly and is counted.
4. **Modern, with the legacy real targets carry.** Written for ACPI 6.5/6.6. Legacy kept only with evidence:
   the RSDT and revision-0 RSDP (QEMU gives nothing else), the 116-byte FADT revision 1 (QEMU `pc`), the
   deprecated `Processor` opcode (58/90 machines, 42/42 QEMU configurations), 32-bit integers (21/90 DSDTs, QEMU
   `pc` and `q35`), `_PDC` as a fallback (64/90).
5. **Fast, and measured.** Speed is judged by instruction counts in ring 0 (`tools/instructions.sh`) and by
   deterministic counters (allocations, opcodes executed, lookups), never by a clock.
6. **Sourced.** Each decision names the need it serves and where its idea comes from.

### 1.2 The needs it serves

From `needs.md` §2.2; priorities from project 7. "Now" means an Urgent or High issue, or #57's closing criterion.

| Need | What the ACPI layer must deliver | Issues | When | Interpreter slices |
|---|---|---|---|---|
| N1 RSDP from every boot path | the pointer from Multiboot2, the EFI table or PVH, the BIOS scan kept | #279, #278, #96 (Urgent), #458 | now | outside the interpreter: an S0 prerequisite of S7's real-machine step, since UEFI-only machines may have no RSDP below 1 MiB |
| N2 table directory | RSDT/XSDT, FADT, DSDT, FACS through read-only windows | #57 | #530 | S1 |
| N3 complete MADT, CPU binding | x2APIC (type 9), NMI (4, 0xA), no 32-CPU cap, Enabled/Online Capable flags, ACPI Processor UID kept | #62, #258; bug to open | now | S2 |
| N4 FADT fixed hardware | PM timer, RESET_REG, SCI_INT, SMI_CMD, ACPI_ENABLE, CST_CNT, PSTATE_CNT, sleep registers, flags | #60, #294 | now | S2 |
| N5, N6 MCFG, HPET | ECAM bases; the second reference clock | #58, #60 | now | S2 |
| N7 SRAT | node ranges and CPU affinity, local APIC (type 0) and x2APIC (type 2) | #125 (Urgent) | now, already done | outside this spec: `numa_policy` reads it through slice 1's directory ([numa_policy.c L18-L112](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/kernel/policy/numa_policy.c#L18-L112): types 0, 2 and 1 at L31, L59, L83; [L125](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/kernel/policy/numa_policy.c#L125)) [code] |
| N10 namespace, dynamic tables included | DSDT + SSDTs, then `Load` and `LoadTable` at run time | #57 | now | S3, S4, S6 |
| N11 firmware handshake | ACPI mode, `\_OSI`, `\_REV`, `_REG`, `_STA`/`_INI`, `\_SB._OSC`, processor `_OSC`/`_PDC` | #57 (implicit) | now | S4, S6 |
| N12 `_CST` for every CPU | entry register, type, latency, power, bound to a CPU slot | #298, #135 (Urgent), #57's criterion | now | S7 |
| N13 P-states, CPPC | `_PSS`, `_PCT`, `_PPC`, `_PSD`, `_CPC` | #302 (Urgent), #138 | next | after #57 |
| N14 PCI INTx routing | `_PRT`, `PNP0C0F` links, and `\_PIC(1)` with them (§3.8 step 5) | #59 (High; INTx is its fallback) | soon | after #57 |
| N16 reset, soft-off | RESET_REG; `_PTS(5)`, `\_S5` | #294 | soon | after #57 |
| N7's `_PXM`, N15 PCIe `_OSC`, N17 events, N18 EC + global lock, N19-N21 AC, battery, thermal, lid, N22 D-states, N23 sleep, N24 platform devices | | #125, #304, #307, #295, #55, #412 | later | not in this spec's slices; the architecture leaves room (§3.7) |

### 1.3 Corrections the evidence forces on #57's plan

The plan in the [#57 comment](https://github.com/MasterLaplace/LplKernel/issues/57#issuecomment-6368709305)
holds, with five corrections:

1. **Dynamic table loading is in, not out.** 16 of 21 Intel clients put `_CST` in Intel `PmRef` SSDTs that the
   processor `_OSC` loads with `Load`; Intel servers use `LoadTable`; without them 16 of 42 clients have no
   `_CST` at all (`needs.md` N10). `Load` is on 55/90 machines, `LoadTable` on 4/90 (real-firmware B.1).
2. **"No method run yet" in slice 3 holds only for QEMU.** `If` runs at definition-block level on 69/90 machines
   and table-level method calls on 53/90 (real-firmware §2.2); QEMU has none in 42 configurations. Slice 3 loads
   declarations and counts the table-level code it skips; slice 4 runs it.
3. **The MADT must read x2APIC entries.** Three 2024 Intel clients list their CPUs only as x2APIC structures and
   seven servers exceed 32 local APICs (`needs.md` N3). `_CST` binding goes through that table.
4. **The FACS address choice of slice 1 should be revisited.** ACPICA and uACPI both prefer the 32-bit
   `FIRMWARE_CTRL` whenever it is non-zero
   ([ACPICA acpixf.h L176-L182](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acpixf.h#L176-L182),
   [tbutils.c L43-L66](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/tables/tbutils.c#L43-L66),
   [uACPI tables.c L1568-L1578](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/tables.c#L1568-L1578))
   [code]; the reason both give, resume failures on real machines (Linux bug 74021), is a comment in each
   [comment]. Slice 1 takes the 64-bit field first unless it lies above 4 GiB. See Q9.
5. **Slice 1 drops a table whose checksum fails**, and with it, on one machine of the 90, the SSDT that holds
   `_CST`. Both references warn and load such a table by default (§5.1). See Q20.

### 1.4 Non-goals

Each item is left out on purpose. "Brings it back" names the evidence that would reverse the decision.

| Left out | Why (evidence) | Brings it back |
|---|---|---|
| `DataTableRegion` execution | 0/90 machines, 0/62 in `needs.md` | a target machine whose needs path runs one; it then decodes already and costs one handler |
| `Unload` execution | 1/90, an older Surface Pro 3 with FADT 5.0 (real-firmware §3); ACPICA says "never been seen in the field" yet runs it ([exconfig.c L700-L769](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exconfig.c#L700-L769)); uACPI refuses it ([interpreter.c L3522-L3543](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L3522-L3543)) [code] | a target that relies on it; decoded and refused meanwhile (§4.5) |
| The event core (SCI, fixed events, GPE `_Lxx`/`_Exx`) | no issue asks; needed for N17-N23. Its absence has two costs, stated here: once §3.8 step 0 puts the platform in ACPI mode, the power button no longer soft-switches off through SMM (only the four-second override of [§4.8.2.2.1.3][s04-override] remains), and the published `_CST` stays the one evaluated for the power source present at boot | the first notebook target, or the server power button (`needs.md` N17); `Notify` is queued, drained once at boot (§3.8 step 9) and counted meanwhile |
| The embedded controller handler and `_Qxx` | N18 is "later"; EC regions on 41/90 (real-firmware §2.5). This departs from the LAI report's C7.3, which asks for the EC from the start, for one reason: `_REG(EmbeddedControl, 1)` is never called, so firmware keeps the guard flag its `_REG` sets and skips its own EC accesses, as it does before any OS EC driver starts. 29 of 42 clients have a `_REG` that tests that space (`scripts/vr_reg_spaces.py`) [measured] | N19-N21 (battery, AC, thermal, lid); or S5's count of EC accesses on the needs path, under the same rule as the global lock |
| The global lock | `Lock` fields on 81/90 (`real90-speccensus.json`, §4.4), but `needs.md` expects the needs path to avoid them. Until it lands, a `Lock` field access and `Acquire (\_GL)` fail loudly with `global_lock_unavailable`, never silently unlocked | S5 counts `global_lock_unavailable` on the needs path of the corpus; any non-zero count moves the global lock into S6 (§9) |
| Region spaces other than SystemMemory, SystemIO, PCI_Config | SystemCMOS 23/90; GenericSerialBus 9/90, GPIO 5/90, IPMI 2/90, PRM 1/90, OEM 5/90; SMBus, PCC, FFixedHW 0/90 (real-firmware §2.5) | a driver that owns the device behind the space (EC first; CMOS for RTC wake, N23; GSBus and GPIO for I2C input, #55) |
| `BufferAcc` protocols and `Connection()` execution | 12/90 and 9/90, older tablets and Surfaces; 0 of the 7 FADT 6.5 machines | the same drivers; the field-list elements are decoded from S3 so the parser never loses its place |
| Return-value repair (ACPICA's 258-entry predefined table) | keeps results raw, so they compare with `acpiexec -dr`; the ACPICA reader puts repair in the consumer (acpica.md §8.1 item 15) | a consumer that meets a broken object repairs it locally |
| Implicit return of the *last* value (ACPICA slack mode) | uACPI and LAI do not do it; not established what NT does (§5) | a needs-path method in the corpus whose result differs between `acpiexec` and `acpiexec -es` |
| Auto-serialisation of methods that create names | only matters when two evaluations interleave; ours never do (§3.7) | concurrent AML execution |
| PSDT and OSDT loading | none in 90 dumps (real-firmware B.2) | a machine that ships one |
| `LoadTable`'s RootPath, ParameterPath and ParameterData | all 2,888 `LoadTable` call sites in the 8 decoded texts that have one pass `""`, `""`, `Zero` (`scripts/vc_loadtable_forms.py`) [measured]; any other form fails with `load_form_unsupported` | a needs-path call site with another form |
| `Load` from a Buffer | no dump of `needs.md` loads from one (N10); it fails with `load_form_unsupported` | a needs-path call site that does |
| `Nor` and `Revision` execution | 0/90 machines; each would also need a §5 row, a test and a mutant | a needs-path count |
| Opcodes outside the needs-path closure (§4.1) | not reached by the path to `_CST` on any corpus machine | the closure of a later need, or a counted `unimplemented_operator` |
| `_REG` for SystemMemory and SystemIO | [§6.5.4][s06-reg-exceptions] declares both always available and says OSPM does not run their `_REG`; uACPI skips them ([opregion.c L112-L120](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/opregion.c#L112-L120)), ACPICA runs them ([evregion.c L72-L87](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/events/evregion.c#L72-L87), [evhandler.c L36-L42](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/events/evhandler.c#L36-L42)) [code]. One `_REG` of 416 on the 42 clients tests SystemMemory, a tablet's touch controller, and none tests SystemIO (`scripts/vr_reg_spaces.py`) [measured] | a needs-path `_REG` that tests either space |
| T-states, S1/S2, `_GTS`/`_BFS`, `_TTS`, hotplug, WMI, video extensions, APEI, active cooling | `needs.md` §8.4, each with its count | the issue that names it |
| A parse tree, method-body copies, a disassembler or debugger in ring 0 | execution runs from the bytes (§3.6) | none |
| Resource conversion into native structures | a zero-copy iterator over descriptors suffices (§3.2) | none expected |
| `_OSI` answers for non-Windows vendor strings and absent features | ACPICA removed "Linux" in 2009 and answers FALSE to them; a 2023 notebook degrades when told "Linux" (`needs.md` N11) | a driver that implements the feature |

---

## 2. Ground rules from LplKernel

| Rule | What it means here | Source |
|---|---|---|
| The determinism contract | AML is integer-only and fixed-width, so every evaluation is a pure function of the AML bytes, the simulated regions, the `_OSI` answers, the clock source and the target's parameters (physical address width, profile caps). Its results can fold on host and i686 alike when both run through the same simulated host (§8.7). Nothing ACPI reads is authoritative; a platform reading that ever steers the World enters as a journaled input. | CONTRIBUTING "The determinism contract"; `needs.md` §2.5 |
| No wall clock on a compared path | Loop and sleep budgets count work: iterations and the *requested* milliseconds of `Sleep`/`Stall`, never elapsed time. In parity runs `Timer` is a virtual clock advanced by those requests and by a fixed quantum on every `Timer` read, so a loop that polls `Timer` without sleeping ends the same way on both targets. A wait on hardware (the ACPI-mode switch, §3.8 step 0) is bounded in time, as the kernel's own waits are ([irq.h L68-L74](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/include/kernel/cpu/irq.h#L68-L74)), and its polls are counted; it is never on a compared path. | CONTRIBUTING; real-firmware §5.1 |
| Fixed-width folds | Folds see `uint32_t`/`uint64_t` fields only; node indices, never pointers; never a `size_t` or an occupancy. | CONTRIBUTING "A parity signature folds only fixed-width data" |
| Integers | Every AML integer is stored as `uint64_t` on both targets and masked to 32 bits when the DSDT revision is below 2 (§5). 64-bit division on i686 goes through libgcc, which the kernel links (`needs.md` §2.5). | [§19.6.29][s19-intwidth] |
| Freestanding C | `-std=gnu11` (kernel/Makefile [L258](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/Makefile#L258)) and `-ffreestanding` ([L16](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/Makefile#L16)), libk only, no inline assembly: port I/O, 64-bit memory-mapped accesses on i686 (§5.3) and the global lock's compare-exchange go through `asmutils` routines. A header C++ includes wraps its declarations in `extern "C"`. | CONTRIBUTING "Kernel code" |
| Naming | Externs spelled out, as slice 1 does: `advanced_configuration_and_power_interface_…`, types `AdvancedConfigurationAndPowerInterface…_t`; static helpers first in each file, with short names (slice 1 uses `acpi_` for statics). No comment inside a function body; Doxygen only, function docs deployed, members `/**< */`. | slice 1 `acpi.c`; the project's conventions; Q2 |
| Tests | `KERNEL_TEST` in `kernel/tests/acpi/` (arch-free code) or `kernel/arch/i386/tests/acpi/`, one `KERNEL_TEST_SUITE` per file, KTAP output, `kernel_test_measure` for values; a test an image must run goes on `lpl.test.require`. | CONTRIBUTING; `kernel/include/kernel/testing/test.h` |
| Speed | Judged by `tools/instructions.sh` counts (`--inside <function> --times <n>`), budgets in `tools/*.budget`. A window that runs at boot is recorded on a release server kernel, like `tools/server-tick.budget` ([L1-L3](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/tools/server-tick.budget#L1-L3)). A window inside a `KERNEL_TEST` can only be a debug-kernel budget, because `test.h` refuses to compile without `LPL_KERNEL_ENABLE_SMOKE_TESTS` and `LPL_KERNEL_DEBUG_POISON` ([test.h L50-L52](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/include/kernel/testing/test.h#L50-L52)); it is measured with `--inside kernel_test_body_<name>` ([test.h L121-L126](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/include/kernel/testing/test.h#L121-L126)), since `--test` targets `LPL_TEST` bodies only ([instructions.sh L68](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/tools/instructions.sh#L68)). | CONTRIBUTING "A change made for speed" |
| Unbuilt branches | The host harness target and every compile-time switch of the interpreter get a gate profile that builds them, or a line in `tools/unbuilt-branches.declared`. `kernel/kernel/acpi/` builds only where `KERNEL_ARCH_HAS_FIRMWARE_TABLES` is 1 (i386 sets it, the arm64 targets clear it, [capabilities.h L96-L100](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/include/kernel/arch/capabilities.h#L96-L100), [i386_pc.h L45](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/include/kernel/arch/targets/i386_pc.h#L45)); the targets that leave it out get their line in that file (#356). | CONTRIBUTING |
| i686 now, long mode later (#96) | No pointer is stored in the namespace or in an object, with two exceptions that are never folded: the loaded-table registry's address of a table's bytes, and a frame's transient code pointer (§3.4). Physical addresses are `uint64_t` in every interface. The hosted harness does not prove the code builds for a 64-bit kernel (it has libc, a red zone, a user code model), so CI also compiles `kernel/kernel/acpi/*.c` as objects with the host's gcc and `-ffreestanding -m64 -mno-red-zone -mcmodel=kernel -nostdinc` against libk's headers. On i686 an address above 4 GiB fails loudly and is never truncated, as slice 1 already does for XSDT entries. | `needs.md` §6, §5 item 13 |
| Firmware memory is read-only | AML bytes are read through the cached read-only firmware window; the interpreter never writes to them (ACPICA writes a NUL into the table for an unterminated string, [psargs.c L575-L578](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psargs.c#L575-L578) [code], which would fault here). | slice 1, `vmm.h` firmware window |


---

## 3. Architecture

### 3.1 Layers

```
L0 tables        RSDP -> RSDT/XSDT -> FADT (normalised), FACS, MADT, MCFG, HPET; ACPI-mode switch  S1, S2
L1 decoder       PkgLength, NameString, opcode descriptors, operand kinds                    every opcode S3
L2 namespace     nodes, hash, resolution, definition-block load (DSDT, SSDTs, Load)           S3, S4, S6
L3 interpreter   values, objects, frames, control flow, conversions, budgets                  S4
L4 regions       SystemMemory, SystemIO, PCI_Config handlers, fields, mapping cache           S5
L5 OS services   _OSI/_OS/_REV, Notify queue, Sleep/Stall/Timer, host interface               S4, S5
L6 consumers     namespace initialisation, _OSC, _CST; later _PIC with _PRT, _PSS/_CPC, _S5   S6, S7, later
```

Each layer only calls the ones above it. L6 is ordinary kernel code that asks the interpreter to evaluate objects
and reads plain results; it never touches interpreter structures. L0 is architecture-free except the BIOS-area
RSDP scan (§3.2). The SRAT is not in L0: `numa_policy` already reads it through slice 1's directory, x2APIC
affinity included (§1.2, N7), and nothing here needs to move it.

### 3.2 Files

The interpreter and the table parsers have no architecture dependency (AML and the tables are
architecture-neutral), so they live outside `arch/`, where long mode (#96, #276) reuses them unchanged; the
architecture supplies the host interface. Names follow §2; the exact prefix is Q2.

| Path | Holds | Slice |
|---|---|---|
| `kernel/arch/i386/cpu/acpi.c`, `kernel/include/kernel/cpu/acpi.h` | slice 1's code; from S2 only the BIOS-area RSDP scan stays here | S1 |
| `kernel/kernel/acpi/tables.c`, `kernel/include/kernel/acpi/tables.h` | slice 1's directory walk, moved; the normalised FADT, the complete MADT, MCFG, HPET; the ACPI-mode switch (SCI_EN, SMI_CMD); all behind the host interface's `map` | S2 |
| `kernel/include/kernel/acpi/machine_language.h` | the public interface: load, evaluate by path, read results, counters | S3 |
| `kernel/include/kernel/acpi/machine_language_host.h` | the host interface the interpreter calls (§3.3) | S3 |
| `kernel/kernel/acpi/machine_language_decoder.c` | opcode descriptor tables, PkgLength, NameString, operand kinds | S3 |
| `kernel/kernel/acpi/machine_language_namespace.c` | node arena, hash, resolution, temporary nodes | S3 |
| `kernel/kernel/acpi/machine_language_load.c` | definition-block load, loaded-table registry, `Load`, `LoadTable` | S3, S6 |
| `kernel/kernel/acpi/machine_language_objects.c` | values, object heap, reference counts, conversions | S3, S4 |
| `kernel/kernel/acpi/machine_language_interpreter.c` | the execution loop, frames, control flow, operator handlers | S4 |
| `kernel/kernel/acpi/machine_language_regions.c` | region attach, mapping cache, field engine, port and PCI-header protection | S5 |
| `kernel/kernel/acpi/machine_language_services.c` | `_OSI`/`_OS`/`_REV`, Notify queue, sleep and loop budgets | S4 |
| `kernel/kernel/acpi/initialization.c` | the ACPI-mode switch call, `_INI`/`_STA`/`_REG` walk, platform and processor `_OSC`, the post-`Load` initialisation | S6 |
| `kernel/kernel/acpi/resources.c` | zero-copy resource-template iterator (Generic Register first) | S7 |
| `kernel/kernel/acpi/processor_idle.c` | `_CST` per processor, binding to CPU slots, the published table | S7 |
| `kernel/arch/i386/cpu/acpi_host.c` | the host interface in ring 0: windows, memory types, ports, PCI, time, sleep | S2-S5 |
| `kernel/arch/i386/lib/asmutils.s`, `kernel/include/kernel/lib/asmutils.h` | a 64-bit memory-mapped read and write as two 32-bit accesses, low then high (§5.3) | S5 |
| `kernel/tests/acpi/simulated_host.c` | the simulated host: sparse memory, zero-filled ports, simulated PCI, virtual clock, target parameters; one source for the host harness and the ring-0 corpus test (§8.4) | S2 |
| `kernel/tests/acpi/*.c` | `KERNEL_TEST`s with made-up AML, and the ring-0 corpus test; also compiled into the host harness | S2-S7 |
| `tools/acpi/host/` | the host harness: the simulated host, acpidump reader, corpus runner, needs-path closure, KTAP printer (Q1) | S2 |
| `tools/acpi-corpus.sh` | fetches the pinned corpora by commit into `build/acpi-corpus/`, builds the deduplicated slice list and one Multiboot module per machine; never commits their bytes | S2 |

### 3.3 The host interface

Kept as small as LAI's weak symbols and far smaller than ACPICA's 62 `AcpiOs*` functions or uACPI's 43 required
callbacks (54 with the optional ones) ([acpiosxf.h L60-L562](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acpiosxf.h#L60-L562),
[kernel_api.h L11-L427](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/kernel_api.h#L11-L427),
[LAI host.h L39-L74](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/include/lai/host.h#L39-L74)) [code].

Two implementations exist. Ring 0 reaches the machine. The simulated host (`kernel/tests/acpi/simulated_host.c`)
serves both the host harness and the ring-0 corpus test, so that a fold compares the interpreter and nothing else
(§8.7).

| Function | Ring 0 | Simulated host |
|---|---|---|
| `map(physical u64, length u64, kind)` → address or 0; `kind` is firmware read-only, firmware read-write (cached), or device (uncached) | `kernel_vmm_map_firmware_window`, a new cached read-write firmware window (Q16), `kernel_vmm_map_device_window` | a sparse byte store keyed by physical address; the kind is ignored |
| `memory_type(physical u64, length u64)` → RAM, ACPI reclaim, ACPI NVS, reserved, or unknown | the boot memory map the PMM keeps | `reserved` unless a per-machine description says otherwise |
| `unmap(address, length, kind)` | the matching unmap | no-op |
| `port_read(port u16, width u8)`, `port_write(...)` | `asmutils` in/out routines | the simulated I/O space, zero-filled |
| `pci_read(segment u16, bus u8, device u8, function u8, offset u16, width u8)`, `pci_write(...)` | the kernel's PCI accessor; offsets from 0x100 need ECAM (#58) and fail with `address_unreachable` until it lands | simulated configuration space; absent devices read all ones |
| `clock_100ns()` | the kernel tick (`clock_get_tick_count`, [clock.h L76](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/include/kernel/cpu/clock.h#L76)) scaled to 100 ns | the virtual clock (§2) |
| `sleep_milliseconds(u32)`, `stall_microseconds(u32)` | at boot: `Sleep` halts until a deadline on the tick (`interrupt_request_deadline_*`), `Stall` spins (`interrupt_request_spin_microseconds`), both rounding up to a whole tick ([irq.h L68-L99](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/include/kernel/cpu/irq.h#L68-L99)); after boot the worker thread blocks (#273) | advance the virtual clock |
| `target()` → physical address width (32 on i686, 52 in long mode), profile caps of §6.1 | the build's values | given on the command line, printed in every record header |
| `pages(count)` | `kernel_vmm_alloc_pages` | `malloc` |
| `find_table(signature, oem_id, oem_table_id, index)` | the directory of S2 | the tables of the machine under test |
| `log(level, text)` | serial, compiled out below the build's level | stdout |

AML runs at boot only after `clock_initialize` has started the tick. The milliseconds `Sleep` and `Stall` request
at boot, and the ticks actually waited, go into the `[LPLTLM] acpi` line: `instructions.sh` does not see that
boot time.

No callback re-enters the interpreter. The few evaluations a region access needs (`_ADR`, `_SEG`, `_BBN`, `_REG`)
are pushed on the same explicit stack (§3.6), unlike uACPI, where they nest on the C stack outside its depth limit
(uacpi.md §0; [opregion.c L1065-L1109](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/opregion.c#L1065-L1109), [default_handlers.c L286-L430](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/default_handlers.c#L286-L430)) [code].

### 3.4 Data structures and their sizes

Every structure is the same size on i686 and x86-64 unless stated, because it holds no pointer. Sizes are
[estimate] until the first build prints `sizeof` in a test.

| Structure | Fields | Bytes | Why this shape |
|---|---|---|---|
| **Value** (`…Value_t`) | `kind u8`, `flags u8`, `auxiliary u16`, `handle u32`, `payload u64` | 16 | Integers live in `payload` with no allocation. Strings, buffers and packages are `handle` = object; a string or buffer literal becomes one when it is evaluated. The layout leaves room for `handle` = table, `payload` = offset and length, which candidate C5 (zero-copy literals, §7.3) would use. A reference is `kind` + target node or object + element index. Idea: integers by value (LAI, [internal-exec.h L144-L155](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/include/lai/internal-exec.h#L144-L155)) [code] |
| **Node** | `name u32` (NameSeg as an integer), `parent u32`, `first_child u32`, `next_sibling u32`, `previous_sibling u32` (circular, so the first child's previous is the last), `type u8`, `flags u8`, `owner u16` (table or "temporary"), `value Value` | 40 | Declaration order kept for walks (`_INI` order, dumps); O(1) append and O(1) unlink of temporaries. The object of a Name, Method, buffer field, mutex, Processor or PowerResource fits in `value`: no second allocation. |
| **Hash slot** | `node u32` (0 is empty) | 4, at load ≤ 1/2 so 8 per node | One open-addressing table for the whole namespace, keyed by (parent, NameSeg), linear probing, backward-shift deletion so no tombstones. A fixed, unseeded 32-bit multiplicative mix. |
| **Object header** | `references u32` (saturating), `kind u8`, `flags u8`, `auxiliary u16`, `length u32`, padding | 16 | One allocation per string, buffer or package: the bytes or the `length` element values follow the header. uACPI uses three per string or buffer by its reader's layout model (uacpi.md §6) [estimate], LAI two per string, buffer or package (lai-and-rust-acpi.md A6) [code]. |
| **Region object** | `space u8`, `state u8`, `pci_device u32` (segment, bus, device, function packed once attached), `offset u64`, `length u64`, `mapping u32`, `owner_device u32` | 32 | Attached lazily at first access. |
| **Field unit object** | `kind u8` (normal, index, bank), `access u8`, `update u8`, `lock u8`, `region u32` (or index field node), `data_or_bank u32`, `bank_value u32`, `bit_offset u32`, `bit_length u32` | 28 | A 4 MiB field still costs 28 bytes (§6). |
| **Method** | in the node's `value`: `handle` = table, `payload` = body offset u32 and body length u32, `auxiliary` = MethodFlags | 0 extra | Bodies are never copied (uACPI copies every body to the heap, [interpreter.c L3829-L3841](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L3829-L3841); rust-osdev clones it per call, [mod.rs L3090-L3100](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L3090-L3100) with [object.rs L25](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/object.rs#L25)) [code]. |
| **Buffer field** | in the node's `value`: `handle` = buffer object, `payload` = bit offset u32 and bit length u32 | 0 extra | 75.6 k buffer fields are created inside methods across the sample (real-firmware §2.2) |
| **Frame** | `code` pointer, `pc u32`, `end u32`, `method u32`, `scope u32`, `arguments[7]`, `locals[8]` (Values), bases into the shared stacks (`u16` × 5), `argument_count u8`, `flags u8` | about 272 on i686, 280 on x86-64 | The only pointer is the transient code pointer; frames are never folded. |
| **Pending operator** | `descriptor u16`, `operands_done u8`, `flags u8`, `start u32`, `end u32` (its PkgLength end, if any), `value_base u16`, `target u16` | 16 | |
| **Block** | `kind u8` (scope, if, else, while, package, table), `start u32` (a While's predicate), `end u32`, `scope u32` | 16 | |
| **Loaded table** | `physical u64`, `bytes` (address of the kernel-owned copy for `Load`, of the firmware window for static tables and `LoadTable`), `length u32`, `checksum u8`, `checksum_valid u8`, `oem_table_id[8]`, `origin u8` (static, `Load`, `LoadTable`), `owner u16` | 36 on i686, 40 on x86-64 | A list of its own, not slice 1's 64-entry directory, which the densest dumps already fill to 80 % (`needs.md` N2, §8.3 item 8). `bytes` is one of the two pointers of §2, printed and never folded. |
| **Evaluation context** | 64 frames, 256 pending operators, 512 blocks, 1,024 values, 1,024 temporary nodes, 32 held mutexes, a 64-entry Notify ring | about 50 KiB, once per worker | Allocated once at initialisation and reused: no allocation and no zeroing per evaluation. uACPI allocates and zeroes one context per evaluation, 7,528 B (5,204 on i686) by the reader's layout model (uacpi.md §6); LAI puts 1,136-1,664 B on the C stack plus a 428-736 B heap allocation per call (`lai_invocation`, [exec.c L1631](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec.c#L1631)) [measured, LAI report A6]. |

**Footprint** [estimate]: a node and its hash share cost 48 bytes, an integer `Name` included. The median machine
(3.2 k named objects, real-firmware §2.2) needs about 154 KiB of nodes; the largest (33.6 k, Intel ArcherCity)
about 1.6 MiB. For comparison an integer `Name` costs 112/76 bytes and three allocations in uACPI, about 120/76
bytes in ACPICA (LP64/i686), by the readers' layout models (uacpi.md §6, acpica.md §6.1).

### 3.5 Memory strategy

| Lifetime | Holds | Allocator | Freed |
|---|---|---|---|
| Permanent | nodes created by a table load, their objects, loaded-table copies | the namespace arena: 64 KiB runs of pages from `kernel_vmm_alloc_pages`, up to a profile cap (§6); never the kernel heap, so the World's heap never competes | never (no `Unload`) |
| Temporary | nodes a method creates (`Name`, `CreateXField`, `OperationRegion`, `Field`, nested `Method`: 117 k across the sample, real-firmware §2.2) | node arena free list; their indices are pushed on the context's temporary-node stack | at frame exit, in reverse order: O(1) unlink each, no namespace walk (LAI scans every node per uninstall, [ns.c L100-L106](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/ns.c#L100-L106) [code]) |
| Values | strings, buffers, packages built during an evaluation | the object heap, size classes over the same page runs | reference count reaches 0 |
| Evaluation scratch | frames, operator, block and value stacks | the evaluation context | reset at the end of each evaluation |

Rules:

- **Reference counts are plain `uint32_t`, saturating.** One owner runs AML (§3.7), so no atomics. A count that
  reaches `0xFFFFFFFF` sticks there and the object leaks instead of being freed twice (uACPI's "bugged" count,
  [shareable.h L8-L38](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/internal/shareable.h#L8-L38));
  ACPICA's count is a 16-bit integer ([acobject.h L55](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acobject.h#L55))
  incremented without a bound ([utdelete.c L395-L396](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utdelete.c#L395-L396),
  [L420-L423](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utdelete.c#L420-L423)),
  so it wraps; L487-L492 only warn above `ACPI_MAX_REFERENCE_COUNT` (0x4000) [code].
- **No reference chains are propagated.** A reference holds one count on its direct target, not on the whole
  chain (uACPI propagates down chains, [types.c L733-L743](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/types.c#L733-L743) [code]).
- **Cycles are refused at store time.** A store that would make an object reach itself fails with
  `reference_cycle`, after an iterative walk with a bounded work list (uACPI,
  [interpreter.c L2351-L2432](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L2351-L2432)).
- **Literals become objects when evaluated.** Keeping them in the table until written (candidate C5, §7.3) breaks
  as soon as something refers to the literal before a write: a buffer field over a `Name`d template, an `Index`
  or `RefOf` reference, a store target, all common in `_CRS` (75.6 k buffer fields created inside methods,
  real-firmware §2.2), and Intel's `_CST` writes into a buffer inside a named package (`needs.md` N12). If C5 is
  ever adopted, each of those materialises the literal first, and `literal_materialised[cause]` counts why.
- **`Load` copies the loaded table** into the namespace arena, checksum computed on the copy. ACPICA does the same
  because "the memory region is corrupted after suspend on some machines"
  ([exconfig.c L555-L568](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exconfig.c#L555-L568)) [comment];
  and the region lies in NVS memory firmware may rewrite. The copies are small: 26 KB at most per machine (eight
  `PmRef` tables). **`LoadTable` does not copy**: its table is listed in the root directory, so it runs in place
  from its read-only firmware window like a static SSDT. Those tables are large: 3.0 MB in three tables on one
  server, 1.6 MB the largest (`scripts/vc_dynamic_sizes.py`) [measured].
- **Every size computed from AML is computed in 64 bits** and checked against a cap before any allocation
  (ACPICA's `VarPackage` and `CreateField` arithmetic wraps on i386, acpica.md §5.3 items 1-2 [reading]).
- **Occupancy is printed, never folded.** Peak arena, heap and stack use go out as one `[LPLTLM] acpi …` line.

### 3.6 Execution model

**One loop, explicit stacks, no C recursion.** Shared by all the readers' "take" lists: uACPI's frames on a heap
stack ([interpreter.c L5534-L5608](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L5534-L5608)),
ACPICA's walk-state list ([psparse.c L497-L547](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psparse.c#L497-L547)),
LAI's four stacks ([internal-exec.h L161-L265](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/include/lai/internal-exec.h#L161-L265)),
rust-osdev's context stack ([mod.rs L473-L497](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L473-L497)) [code].
Ours differs in two ways: the stacks are fixed arrays sized by §6's limits (no reallocation, so no pointer
refresh, uACPI's A9 hazard), and a limit is an error, not a panic.

**No parse tree.** AML runs straight from the bytes; an untaken branch is skipped by its PkgLength (uACPI T2,
ACPICA's execute-mode parse, LAI, rust-osdev). Method bodies are skipped at load by their PkgLength and decoded
only when called, so load time follows declarations and table-level code: bodies are 81 % of the server AML bytes
and 37 % of QEMU's (specification report §4 item 1) [measured].

**The loop**, in order of precedence at each step:

1. If the top pending operator has all its operands, run its handler, pop it, and hand the result to the operator
   below (as its next operand) or drop it when the operator was a statement.
2. Else, if `pc` reached the end of the innermost block, close the block: a `While` jumps back to its predicate,
   a taken `If` skips a following `Else` by its PkgLength, a `Scope`/`Device` restores the scope, a method end
   performs the implicit return (§5), a table end finishes the load frame.
3. Else decode one term at `pc`:
   - a constant (`Zero`, `One`, `Ones`, the integer prefixes, `String`, `Revision`), `LocalX` or `ArgX` is a
     value pushed directly, with no operator record (constants are about 30 % of terms, real-firmware §4 item 1);
   - an opcode reads its fixed fields at once (ByteData, WordData, DWordData, PkgLength, a declaration's
     NameString) and pushes a pending operator for its TermArg operands;
   - a NameString in a TermArg position is resolved: a method becomes a call that collects `argc` operands; a data
     object becomes a value; a field unit becomes a field read; an unresolved name is an error (§6).
4. A call whose arguments are complete pushes a frame whose code is the method body inside its table, and moves
   the arguments from the value stack into the frame. `Return` pops frames up to the method's own.

**Operand kinds.** Each opcode descriptor lists its operands as 4-bit kinds packed in one `uint32_t`: TermArg,
SuperName, Target, SimpleName, NameString, ByteData, WordData, DWordData, QWordData, PkgLength, FieldList,
PackageElementList, MatchOpcode. The idea is shared: ACPICA packs 5-bit argument types in a `UINT32`
([acmacros.h L398-L421](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acmacros.h#L398-L421)),
LAI keeps operand-mode flags ([exec_impl.h L45-L84](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec_impl.h#L45-L84)),
rust-osdev a `ResolveBehaviour` slice ([mod.rs L2970-L3002](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L2970-L3002)) [code].
We do not take uACPI's 60 decode "parse ops", which add a second interpretation layer per opcode
([opcodes.h L18-L269](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/internal/opcodes.h#L18-L269), uacpi.md §4).

**Decoding.** Two 256-entry tables (one-byte opcodes, then `0x5B`-prefixed) map to a compact array of about 120
descriptors of 8 bytes: handler, operand count, operand kinds, flags (has PkgLength, opens a scope, declares a name,
yields a value). Idea from ACPICA's two direct-indexed tables
([psopinfo.c L186-L254](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psopinfo.c#L186-L254))
and uACPI's 256-entry table ([opcodes.c L16-L50](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/opcodes.c#L16-L50)) [code].
Handlers are one `switch`, so the compiler can inline the hot ones.

**Bounds at decode time.** Every PkgLength end is checked against the enclosing end before anything else reads past
it; every fixed field and every NameString is checked against the innermost end. This closes uACPI's own TODO
("do package bounds checking at parse time", [interpreter.c L736](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L736)),
whose absence lets `If (Zero)` with an overlong PkgLength send the reader past the method (uacpi.md §5.5 item 1
[reading]). The spec makes a crossing PkgLength fatal ([§5.4.1][s05-pkglen]).

**The program counter is committed per whole term.** An error leaves `pc` at the start of the faulty term, never
inside it (LAI, [exec.c L1907-L1916](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec.c#L1907-L1916) [code]).
The error report names the table, the offset, the opcode and the method path.

**A table load is a frame.** The definition block runs as a pseudo-method whose named objects are permanent and
whose `Local0`-`Local7` exist (a Surface Pro runs a `Switch` on `Local0` at the root, real-firmware §2.2). Idea
from uACPI T4 ([interpreter.c L1334-L1384](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L1334-L1384))
and ACPICA's single-pass load ([nsparse.c L27-L147](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nsparse.c#L27-L147)) [code].
`Load` and `LoadTable` push such a frame on the same stack, so a load from a method costs no C stack (uACPI,
[interpreter.c L6190-L6201](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L6190-L6201)).
Loading is single-pass and in order, like ACPICA and uACPI: table-level code sees only what was declared before
it. The corpus harness counts table-level forward references that fail; a non-zero count reopens the question.

**Evaluations a region needs** (`_ADR`, `_SEG`, `_BBN`, `_HID`/`_CID` while attaching a PCI_Config region; `_REG`
when a method creates a PCI_Config region after initialisation) are pushed as frames marked "internal" on the same stacks,
and the access resumes when they return. They count against the same depth limit. Idea: uacpi.md A8's remedy.

**Errors** carry a typed status (§6.2). In a method, an error unwinds every frame of the evaluation: temporaries
removed, mutexes released, values dropped. At table level it skips the innermost enclosing PkgLength construct and
the load goes on; that granularity is this spec's own rule. ACPICA keeps loading after any module-level error: it
"skip[s] parsing whatever caused the error and keep[s] going"
([psobject.c L626-L638](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psobject.c#L626-L638))
and turns AML exceptions at module level into `AE_OK`
([psloop.c L546-L561](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psloop.c#L546-L561)) [code].
uACPI forgives only a name that already exists or is not found in a table frame, and then drops the enclosing
top-level `If` or `While`; any other error fails the table
([interpreter.c L5995-L6022](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L5995-L6022)) [code].

### 3.7 Concurrency

**One owner runs AML, one evaluation at a time, to completion.**

| Phase | Who runs AML | Where | Why |
|---|---|---|---|
| Boot | the bootstrap processor | before any World tick starts, after the PMM's second pass (#528) and `clock_initialize` | ACPI mode, namespace load, initialisation, `_OSC`, `_CST` all happen once |
| After boot | the ACPI worker thread (#273), pinned to a core that holds no World tick | a request ring (#272) carries "evaluate X" in and results out | AML traps to SMM through port 0xB2 (45/62 dumps), sleeps and polls (`needs.md` §4); a client's budget is 1 ms latency (#147) and 100 µs jitter (#349) |
| Interrupts | never | the SCI handler (later, N17) only acknowledges, disables the GPE and queues work | ACPICA and uACPI defer `_Lxx`/`_Exx` and Notify the same way ([evgpe.c L929-L1032](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/events/evgpe.c#L929-L1032), [uACPI event.c L660-L936](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/event.c#L660-L936)) [code] |

Consequences, each a simplification the readers' implementations do not get:

- **No lock in the interpreter.** uACPI drops and retakes its namespace lock around `Sleep`
  ([interpreter.c L3472-L3474](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L3472-L3474)),
  a firmware request ([L4104-L4106](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L4104-L4106))
  and a region handler ([opregion.c L340-L342](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/opregion.c#L340-L342)),
  then needs keep-alive references for each window (uacpi.md A6); ACPICA takes the interpreter and namespace
  mutexes on entry ([exutils.c L61-L82](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exutils.c#L61-L82))
  and releases the interpreter around non-default region handlers
  ([evregion.c L254-L263](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/events/evregion.c#L254-L263)) [code].
  Here a blocking `Sleep` blocks the worker; no other AML runs meanwhile.
- **`Serialized` costs a sync-level check, not a mutex object** (§5).
- **`Acquire` of an AML mutex always succeeds or nests**, because mutexes still held are released when an
  evaluation ends (as ACPICA and uACPI do, [exmutex.c L506-L555](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exmutex.c#L506-L555),
  [interpreter.c L6461-L6477](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L6461-L6477)).
  Only `\_GL` contends, with firmware.
- **Results are published, not shared.** A consumer gets a plain copy (for `_CST`, a per-CPU table of states);
  the idle path reads that table and never calls the interpreter (`needs.md` §4).
- **Notify never runs inside the evaluation that raised it**; it goes to a bounded ring and is delivered after the
  evaluation ends (`needs.md` §5 item 10). At boot, step 9 of §3.8 drains the ring; after boot, the worker (#273)
  drains it between requests. Until N17, the only Notify acted on is 0x81 on a processor (`_CST` changed).

What would change this: concurrent evaluations (several workers) would bring back a namespace lock and per-method
owner checks; nothing in the needs asks for it.

### 3.8 The initialisation sequence

Run once at boot by the bootstrap processor. Each step names its source.

0. **Put the platform in ACPI mode**, before any AML runs, as both references do: ACPICA's `AcpiEnable` runs in
   `AcpiEnableSubsystem` ([utxfinit.c L155-L171](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utxfinit.c#L155-L171),
   [hwacpi.c L36-L132](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/hardware/hwacpi.c#L36-L132)),
   before `AcpiInitializeObjects` runs `_INI` ([L237-L292](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utxfinit.c#L237-L292));
   uACPI's `enter_acpi_mode_initial` runs in `uacpi_initialize`
   ([uacpi.c L383-L384](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/uacpi.c#L383-L384),
   [L271-L302](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/uacpi.c#L271-L302)),
   before the namespace loads [code]. Firmware written for Windows has only run its `_INI` and `_OSC` in ACPI mode.
   1. Skip the switch if SCI_EN is already set, if SMI_CMD is 0, or if the FADT is hardware-reduced, which boots
      in ACPI mode only ([§3.11.1 L1346-L1350][s03-hwreduced]).
   2. Clear every GPE enable bit and every PM1 fixed-event enable bit before the switch, and keep the SCI line
      (SCI_INT) masked, in the 8259 or the I/O APIC: there is no SCI handler before N17. Both references clear the
      GPE enables when they create a GPE block ([evgpeblk.c L285-L287](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/events/evgpeblk.c#L285-L287);
      uACPI [event.c L1685](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/event.c#L1685)) [code];
      doing it before the switch is this spec's choice, so that no event raises an SCI nothing handles.
   3. Write ACPI_ENABLE to SMI_CMD, "synchronously from the boot processor", and wait for SCI_EN
      ([§5.2.9 L1302-L1311][s05-smi-cmd]): at most 3,000 polls 1 ms apart on the kernel tick, ACPICA's bound
      ([hwacpi.c L117-L127](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/hardware/hwacpi.c#L117-L127)) [code], with the polls counted.
   The cost is the power button's soft-off through SMM until N17 (§1.4).
1. Load the DSDT, then every SSDT of the root table in directory order, each as a table frame. A failing table is
   counted and the next one loads (ACPICA, [tbxfload.c L134-L254](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/tables/tbxfload.c#L134-L254);
   uACPI, [uacpi.c L449-L536](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/uacpi.c#L449-L536)).
   A table whose checksum fails is loaded and counted (§5.1). Integer width is fixed from the DSDT revision before
   the first byte runs.
2. Run `\_INI`, then `\_SB._INI`, before any `_REG`: "for Windows compatibility (Vista+)"
   ([ACPICA nsinit.c L170-L208](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nsinit.c#L170-L208) [comment]);
   uACPI runs the same four steps in the same order
   ([uacpi.c L649-L684](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/uacpi.c#L649-L684)) [code].
3. Run `_REG(PCI_Config, 1)` once in every scope that declares a PCI_Config region, as both references do. Not
   for SystemMemory and SystemIO: [§6.5.4][s06-reg-exceptions] makes them always available and says OSPM runs no
   `_REG` for them; uACPI follows that, ACPICA does not (§1.4). 70/90 machines define `_REG` (real-firmware §2.6);
   on the 42 clients, 359 `_REG` bodies on 13 machines test PCI_Config (`scripts/vr_reg_spaces.py`) [measured].
4. Walk the devices in declaration order: no `_STA` means 0x0F; present → run `_INI` and descend; not present but
   functioning → skip `_INI`, descend; neither → skip the subtree ([§6.3.7][s06-sta], [§6.5.1][s06-ini]). A
   pre-pass marks subtrees that hold an `_INI`, so most devices need no `_STA` evaluation (ACPICA,
   [nsinit.c L492-L547](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nsinit.c#L492-L547)).
   19 of 21 Intel clients call `_OSI` from an `_INI`, so this step sets the firmware's OS year (`needs.md` N11).
5. **`\_PIC` is not called** until N14 routes PCI INTx through the I/O APIC; the count `pic_mode_deferred` says it
   was skipped. Firmware assumes PIC mode until told
   ([§5.8.1 L18247-L18257](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L18247-L18257)),
   and PIC mode is what the kernel still uses for PCI INTx: the HD Audio capture takes "the firmware-assigned
   line" through the 8259 ([hal_audio.c L47-L85](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/arch/i386/hal/hal_audio.c#L47-L85)) [code].
   On 21 of the 42 clients, all AMD, `\_PIC(1)` calls `\_SB.DSPI`
   (`scripts/verify_completeness_probe.py`) [measured]; on the one body read in full it writes 0x1F into every
   PIRQ router, HD Audio's included (ASRock B650 PG Lightning, linuxhw/ACPI, CC-BY-4.0:
   [`_PIC` L1048-L1057](https://github.com/linuxhw/ACPI/blob/d6d30c6ef6f45506567f25a302c64f009d8f5f4e/Desktop/ASRock/B650/B650%20PG%20Lightning/69B64D1D19B3#L1048-L1057),
   [`DSPI` L5514-L5534](https://github.com/linuxhw/ACPI/blob/d6d30c6ef6f45506567f25a302c64f009d8f5f4e/Desktop/ASRock/B650/B650%20PG%20Lightning/69B64D1D19B3#L5514-L5534),
   `HDAD = Arg0` at L5539). Calling it now would silence every PCI device the kernel drives through the 8259.
   This overrides `needs.md` N11, which asked for `\_PIC(1)` before N14.
6. Platform `\_SB._OSC` with UUID `0811B06E-…`, query then commit, with the DWORD-2 bits of Q19: by default none,
   since the kernel exercises none of the controls they claim at S6 ([§6.2.11.1][s06-osc-rules],
   [§6.2.11.2][s06-osc-platform]). Intel's `GCAP` reads the result (`\_SB.OSCP`) and loads its HWP table only when
   bit 6, CPPC revision 2, is granted (`needs.md` N11).
7. For each processor object: `_OSC` with Intel's processor UUID `4077A616-290C-47BE-9EBD-D87058713953`, or `_PDC`
   when the processor has no `_OSC` ([Appendix C][sC-pdc]). This is where `Load` runs on Intel clients.
8. Write CST_CNT to SMI_CMD when it is non-zero ([§5.2.9 L1402-L1405][s05-cst-cnt]; 8 of 20 servers, 0 of 42
   clients, `needs.md` N4), only once step 0 has put the platform in ACPI mode; then `_CST` for each processor
   object, bound to its CPU slot (S7).
9. Drain the Notify ring. A Notify 0x81 on a processor queued after its step-8 evaluation triggers one more
   `_CST` evaluation; every other Notify is counted and dropped. The published table is then valid for the power
   source present at boot until N17 delivers later Notifies.

Linux's and Windows' own orders are not established by the pinned sources; steps 5-8 follow the dependencies the
corpus shows (`GCAP` reads `OSCP`; `_CST` exists only after step 7 on Intel).

**When a step fails.** No failure stops the sequence; each is counted and named in the log.

| Step | On failure or absence | Then | Counter |
|---|---|---|---|
| 0 ACPI mode | SCI_EN still clear after the bound | continue in legacy mode; step 8 writes no CST_CNT | `acpi_mode_failed` |
| 1 a table | the table's failure (§6.2) | the next table loads | `table_load_failed` |
| 2 `\_INI`, `\_SB._INI` | the evaluation's status | continue | `initialization_failed` |
| 3 a `_REG` | the evaluation's status | the next scope | `region_registration_failed` |
| 4 a `_STA` | the evaluation's status | no `_INI` for that device; its children are still walked, as ACPICA does ([nsinit.c L617-L622](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nsinit.c#L617-L622)) [code] | `status_failed` |
| 4 an `_INI` | the evaluation's status | continue and descend; ACPICA ignores `_INI` errors too ([L706-L728](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nsinit.c#L706-L728)) [code] | `initialization_failed` |
| 5 `\_PIC` | not called (absent on 1/42 clients anyway) | — | `pic_mode_deferred` |
| 6 platform `_OSC` | absent, failed, or an error bit in the returned DWORD 1 | continue as if nothing was granted | `platform_capabilities_failed` |
| 7 processor `_OSC`/`_PDC` | the evaluation's status | continue; that processor may have no `_CST` | `processor_capabilities_failed` |
| 8 a `_CST` | the evaluation's status, or a malformed package | that CPU keeps the states the kernel enters without ACPI (HLT, MWAIT C1, §6.2) | `processor_idle_failed` |

**Tables loaded at run time.** A table that step 7 loads can declare regions and devices. Once the evaluation
that loaded it ends, its scopes get the `_REG` of step 3, then the `_STA`/`_INI` walk of step 4 over its device
subtrees: `_INI` "is run only when OSPM loads a description table" ([§6.5.1 L8015][s06-ini-load]). Neither
reference interpreter does this inside its load operator (ACPICA only initialises the new objects,
[exconfig.c L665-L676](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exconfig.c#L665-L676);
uACPI matches GPEs, [interpreter.c L1370-L1383](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L1370-L1383))
[code]; the counter `late_initializations` shows on the corpus how often it does anything.


---

## 4. The AML subset

### 4.1 How the subset is decided

1. **Decode everything.** Every encoding of the 6.5 byte table ([§20.3][s20-table]) is decoded with its exact
   operands, plus `Processor` (permanently reserved since 6.4, [L1883-L1887][s20-processor-reserved]) and `Unload`
   (reserved, [L1843-L1847][s20-unload-reserved]). An undecoded opcode makes the rest of its term list unreadable
   (specification report §3), so decoding is never "left out". A byte that is not in the table is `invalid_opcode`.
2. **Execute what the needs path reaches.** At the end of S3, the host harness computes, on each of the 114
   machines, the static closure of the needs path: table-level code, `\_INI`, `\_SB._INI`, `_REG`, `_STA`, `_INI`,
   `\_SB._OSC`, processor `_OSC`/`_PDC`, `_CST`, and every method they call, the dynamic SSDTs of the dumps
   included. Being static, it covers every branch, so it is a superset of what runs. S4 implements every opcode
   the closure reaches on any machine, the most common first (§4.3's counts order the work); every other opcode
   fails loudly with `unimplemented_operator`, counted, until a count asks for it. Each later need adds the closure
   of its own entry points (`_PSS`/`_CPC` for N13, `_PRT` for N14, `_PTS`/`\_S5` for N16). `Nor` and `Revision`
   (0/90), `DataTableRegion` (0/90) and `Unload` (1/90, FADT 5.0) are decoded only. Which other S4 rows fall
   outside is not known before the closure runs; `needs.md` §3 saw none of N11 to N16 need `Event`, `Signal`,
   `Wait` or `Reset`.
3. **Only three region spaces get a handler**: SystemMemory, SystemIO, PCI_Config (90/90, 90/90, 84/90). Every
   other space decodes and fails loudly at access (§4.5).
4. **The slices order the work by need.** S3 declares, S4 evaluates, S5 reaches hardware, S6 loads tables at run
   time. The column "Runs from" says the earliest slice that may execute the opcode.
5. **The subset is re-measured on the needs path.** From S5 on, once fields can be read, the host harness and the
   ring-0 corpus test run the initialisation sequence of §3.8 and `_CST` on every corpus machine and print the
   opcodes they executed (§8.4). An opcode, a region space or an access type executed there and not implemented
   is a slice's red test, not a surprise on hardware.

Neither QEMU column decides anything: 51 of the 113 opcodes never appear in QEMU's 42 configurations, among them
`Sleep`, `Stall`, `Load`, `CondRefOf`, `Ones` and `QWordPrefix` (specification report §3).

### 4.2 The decoder's alphabet

| Kind | Encoding | Spec | Notes |
|---|---|---|---|
| PkgLength | 1-4 bytes; bits 7-6 of the lead byte count the follow bytes; 1 byte uses bits 5-0, otherwise bits 3-0 then up to 3 bytes; maximum 2^28; **the length counts its own bytes** | [§20.2.4][s20-pkglen], [§5.4.1][s05-pkglen] | ACPICA takes the start before reading the length and ends at start + PackageLength ([psargs.c L130-L145](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psargs.c#L130-L145)) [code]. Encodings seen: 1 and 2 bytes dominate; 4 bytes only on servers (specification report §2.4). |
| NameSeg | 4 bytes: `A-Z` or `_` first, then `A-Z`, `0-9`, `_`; compared as a `uint32_t` | [§20.2.2][s20-names] | |
| NameString | `\` or `^`*, then a NameSeg, `0x2E` + 2 segments, `0x2F` + count (1-255) + segments, or `0x00` (NullName) | [§20.2.2][s20-namestring] | count 0 is a decode error (uACPI rejects it: "MultiNamePrefix but SegCount is 0", [interpreter.c L546-L554](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L546-L554)) [code] |
| TermArg | any expression, a `LocalX`, an `ArgX`, a constant, or a NameString (data object, field read, or method call) | [§20.2.5][s20-terms] | the only context-sensitive point: a call's argument count comes from the namespace |
| SuperName | SimpleName, `Debug`, or a reference-yielding expression (`RefOf`, `DerefOf`, `Index`) | [§20.2.2][s20-namestring] | a method there is called, as in ACPICA ([psargs.c L1081-L1126](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psargs.c#L1081-L1126)) [code] |
| Target | SuperName or NullName | | NullName discards the result |
| SimpleName | NameString, `ArgX`, `LocalX` | | **never a call**: `CondRefOf` and `RefOf` operand, `ObjectType`, `CopyObject` destination (ACPICA [acopcode.h L67](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acopcode.h#L67), [L70](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acopcode.h#L70), [L127](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acopcode.h#L127), [L134](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acopcode.h#L134)) [code]. The spec's grammar allows a call there ([L1084-L1090][s20-condrefof]); following it literally loses sync on 85/90 machines at `CondRefOf (\_OSI, Local0)` (real-firmware §1.3) [measured]. |
| ByteData, WordData, DWordData, QWordData | little-endian fixed fields | [§20.2.3][s20-data] | |
| FieldList | named, reserved (`0x00`), AccessAs (`0x01`), Connection (`0x02` + NameString or Buffer), ExtendedAccessAs (`0x03`) | [L494-L827][s20-named] | all five decoded from S3 |
| PackageElementList | DataRefObject or NameString per element | [L1378-L1410][s20-package] | an expression as an element is refused (ACPICA does, [dspkginit.c L171-L197](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dspkginit.c#L171-L197)); the corpus parses under this grammar with 0 errors |

**Where the spec contradicts itself** the decoder accepts both forms and the test suite holds one case per form:

| Opcode | Grammar | Byte-value table or ACPICA | Decoded as |
|---|---|---|---|
| `CondRefOf` | `SuperName Target` ([L1084-L1090][s20-condrefof]) | `SuperName SuperName` ([L1778-L1782][s20-condrefof-row]) | SimpleName, then Target |
| `Load` | `NameString Target` ([L1260-L1266][s20-load]) | `NameString SuperName` ([L1793-L1797][s20-load-row]) | SuperName, then Target |
| `DerefOf` | `TermArg` ([L1108-L1120][s20-derefof]) | ACPICA parses a SuperName ([acopcode.h L80](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acopcode.h#L80)) | TermArg whose value must be a reference or a string path |
| `Notify` | `SuperName TermArg` | uACPI decodes a NameString only, after "about 500 AML dumps" ([opcodes.h L787-L800](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/internal/opcodes.h#L787-L800)) [comment] | SuperName, the spec's form; it costs nothing more here. The uACPI reader's own sample confirms uACPI's observation: 0 of 8,120 `Notify` on `ArgX` or `LocalX` (uacpi.md §5.6, appendix A) |

### 4.3 The opcodes

Columns: the byte encoding; the ASL section of ACPI 6.5 that defines its semantics; its row in the byte-value table
of §20.3 (permalink); the machines using it among the 90 (whole tables), among the 43 with FADT ≥ 6.0, and at
definition-block level; QEMU's 42 `pc`/`q35` configurations; the earliest slice that may execute it (inside S4, the
needs-path closure of §4.1 decides whether a row ships there or waits for a count). Machine counts come
from `scripts/synthesis/opcode_levels.md` (see the introduction) and agree with real-firmware.md appendix B.1; QEMU counts from
specification-and-qemu.md §3. `Local1`-`Local6` and `Arg1`-`Arg5` sit between the rows shown.

**Data objects and constants (decoded S3; built at load S3, everywhere S4)**

| Encoding | Opcode | ASL § | 20.3 row | Machines /90 | FADT ≥ 6 /43 | Table level /90 | QEMU /42 | Runs from |
|---|---|---|---|---:|---:|---:|---:|---|
| `0x00` | Zero | 19.6.153 | [L1633](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1633) | 90 | 43 | 81 | 42 | S3 |
| `0x01` | One | 19.6.97 | [L1638](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1638) | 90 | 43 | 80 | 42 | S3 |
| `0xFF` | Ones | 19.6.98 | [L2313](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2313) | 88 | 43 | 52 | 0 | S3 |
| `0x0A` | ByteConst | 20.2.3 | [L1668](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1668) | 90 | 43 | 90 | 42 | S3 |
| `0x0B` | WordConst | 20.2.3 | [L1673](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1673) | 90 | 43 | 90 | 42 | S3 |
| `0x0C` | DWordConst | 20.2.3 | [L1678](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1678) | 90 | 43 | 90 | 42 | S3 |
| `0x0E` | QWordConst | 20.2.3 | [L1688](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1688) | 64 | 43 | 45 | 0 | S3 |
| `0x0D` | String | 19.3.2.2 | [L1683](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1683) | 90 | 43 | 87 | 42 | S3 |
| `0x11` | Buffer | 19.6.10 | [L1703](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1703) | 90 | 43 | 90 | 42 | S3 |
| `0x12` | Package | 19.6.101 | [L1708](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1708) | 90 | 43 | 90 | 42 | S3 |
| `0x13` | VarPackage | 19.6.101 | [L1713](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1713) | 1 | 1 | 1 | 0 | S4 |
| `5B 30` | Revision | 19.6.119 | [L1848](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1848) | 0 | 0 | 0 | 0 | decoded only |
| `5B 31` | Debug | 19.6.26 | [L1853](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1853) | 81 | 42 | 5 | 42 | S4 |

**Namespace modifiers and named objects (declared S3)**

| Encoding | Opcode | ASL § | 20.3 row | Machines /90 | FADT ≥ 6 /43 | Table level /90 | QEMU /42 | Runs from |
|---|---|---|---|---:|---:|---:|---:|---|
| `0x08` | Name | 19.6.89 | [L1658](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1658) | 90 | 43 | 90 | 42 | S3 |
| `0x10` | Scope | 19.6.120 | [L1698](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1698) | 90 | 43 | 90 | 42 | S3 |
| `0x14` | Method | 19.6.84 | [L1718](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1718) | 90 | 43 | 90 | 42 | S3 |
| `0x15` | External | 19.6.45 | [L1723](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1723) | 47 | 39 | 47 | 0 | S3 |
| `0x06` | Alias | 19.6.4 | [L1648](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1648) | 65 | 33 | 65 | 0 | S3 |
| `5B 82` | Device | 19.6.31 | [L1878](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1878) | 90 | 43 | 90 | 42 | S3 |
| `5B 83` | Processor | App. C | [L1883](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1883) | 58 | 15 | 58 | 42 | S3 |
| `5B 84` | PowerResource | 19.6.107 | [L1888](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1888) | 58 | 36 | 58 | 0 | S3 |
| `5B 85` | ThermalZone | 19.6.133 | [L1893](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1893) | 59 | 33 | 59 | 0 | S3 |
| `5B 01` | Mutex | 19.6.88 | [L1768](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1768) | 87 | 42 | 87 | 42 | S3 |
| `5B 02` | Event | 19.6.41 | [L1773](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1773) | 29 | 19 | 29 | 0 | S3 |
| `5B 80` | OperationRegion | 19.6.99 | [L1868](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1868) | 90 | 43 | 90 | 42 | S3 (access S5) |
| `5B 81` | Field | 19.6.47 | [L1873](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1873) | 90 | 43 | 90 | 42 | S3 (access S5) |
| `5B 86` | IndexField | 19.6.63 | [L1898](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1898) | 67 | 33 | 67 | 0 | S3 (access S5) |
| `5B 87` | BankField | 19.6.7 | [L1903](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1903) | 19 | 15 | 19 | 0 | S3 (access S5) |
| `5B 88` | DataTableRegion | 19.6.25 | [L1908](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1908) | 0 | 0 | 0 | 0 | decoded only |
| `0x8D` | CreateBitField | 19.6.18 | [L2163](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2163) | 68 | 41 | 5 | 0 | S3 |
| `0x8C` | CreateByteField | 19.6.19 | [L2158](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2158) | 88 | 42 | 62 | 0 | S3 |
| `0x8B` | CreateWordField | 19.6.23 | [L2153](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2153) | 89 | 42 | 61 | 0 | S3 |
| `0x8A` | CreateDWordField | 19.6.20 | [L2148](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2148) | 90 | 43 | 59 | 42 | S3 |
| `0x8F` | CreateQWordField | 19.6.22 | [L2173](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2173) | 61 | 42 | 6 | 0 | S3 |
| `5B 13` | CreateField | 19.6.21 | [L1783](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1783) | 85 | 42 | 9 | 2 | S3 |

**Control flow, locals and arguments (S4)**

| Encoding | Opcode | ASL § | 20.3 row | Machines /90 | FADT ≥ 6 /43 | Table level /90 | QEMU /42 | Runs from |
|---|---|---|---|---:|---:|---:|---:|---|
| `0xA0` | If | 19.6.59 | [L2268](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2268) | 90 | 43 | 69 | 42 | S4 |
| `0xA1` | Else | 19.6.38 | [L2273](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2273) | 90 | 43 | 22 | 32 | S4 |
| `0xA2` | While | 19.6.148 | [L2278](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2278) | 88 | 43 | 1 | 42 | S4 |
| `0xA5` | Break | 19.6.8 | [L2293](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2293) | 74 | 43 | 1 | 42 | S4 |
| `0x9F` | Continue | 19.6.16 | [L2263](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2263) | 36 | 27 | 0 | 0 | S4 |
| `0xA4` | Return | 19.6.118 | [L2288](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2288) | 90 | 43 | 0 | 42 | S4 |
| `0xA3` | Noop | 19.6.91 | [L2283](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2283) | 24 | 15 | 17 | 0 | S4 |
| `0x60` | Local0 | 19.6.78 | [L1938](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1938) | 90 | 43 | 1 | 42 | S4 |
| `0x67` | Local7 | 19.6.78 | [L1973](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1973) | 71 | 41 | 0 | 2 | S4 |
| `0x68` | Arg0 | 19.6.6 | [L1978](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1978) | 90 | 43 | 0 | 42 | S4 |
| `0x6E` | Arg6 | 19.6.6 | [L2008](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2008) | 47 | 38 | 0 | 0 | S4 |

**Expressions (S4; the table-level column shows what S3 counts and skips)**

| Encoding | Opcode | ASL § | 20.3 row | Machines /90 | FADT ≥ 6 /43 | Table level /90 | QEMU /42 | Runs from |
|---|---|---|---|---:|---:|---:|---:|---|
| `0x70` | Store | 19.6.130 | [L2018](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2018) | 90 | 43 | 40 | 42 | S4 |
| `0x9D` | CopyObject | 19.6.17 | [L2253](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2253) | 13 | 5 | 0 | 0 | S4 |
| `0x71` | RefOf | 19.6.113 | [L2023](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2023) | 49 | 29 | 0 | 0 | S4 |
| `5B 12` | CondRefOf | 19.6.14 | [L1778](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1778) | 87 | 43 | 47 | 0 | S4 |
| `0x83` | DerefOf | 19.6.30 | [L2113](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2113) | 89 | 43 | 20 | 42 | S4 |
| `0x88` | Index | 19.6.62 | [L2138](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2138) | 90 | 43 | 30 | 42 | S4 |
| `0x87` | SizeOf | 19.6.124 | [L2133](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2133) | 90 | 43 | 1 | 42 | S4 |
| `0x8E` | ObjectType | 19.6.96 | [L2168](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2168) | 35 | 25 | 0 | 2 | S4 |
| `0x89` | Match | 19.6.80 | [L2143](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2143) | 55 | 35 | 0 | 0 | S4 |
| `0x72` | Add | 19.6.3 | [L2028](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2028) | 90 | 43 | 63 | 10 | S4 |
| `0x74` | Subtract | 19.6.131 | [L2038](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2038) | 90 | 43 | 0 | 42 | S4 |
| `0x77` | Multiply | 19.6.87 | [L2053](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2053) | 90 | 43 | 0 | 0 | S4 |
| `0x78` | Divide | 19.6.32 | [L2058](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2058) | 84 | 43 | 0 | 0 | S4 |
| `0x85` | Mod | 19.6.86 | [L2123](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2123) | 35 | 25 | 0 | 0 | S4 |
| `0x75` | Increment | 19.6.61 | [L2043](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2043) | 87 | 43 | 0 | 42 | S4 |
| `0x76` | Decrement | 19.6.27 | [L2048](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2048) | 90 | 43 | 0 | 0 | S4 |
| `0x79` | ShiftLeft | 19.6.121 | [L2063](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2063) | 90 | 43 | 64 | 40 | S4 |
| `0x7A` | ShiftRight | 19.6.122 | [L2068](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2068) | 88 | 43 | 1 | 40 | S4 |
| `0x7B` | And | 19.6.5 | [L2073](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2073) | 90 | 43 | 26 | 42 | S4 |
| `0x7C` | Nand | 19.6.90 | [L2078](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2078) | 2 | 2 | 0 | 0 | S4 |
| `0x7D` | Or | 19.6.100 | [L2083](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2083) | 90 | 43 | 21 | 42 | S4 |
| `0x7E` | Nor | 19.6.92 | [L2088](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2088) | 0 | 0 | 0 | 0 | decoded only |
| `0x7F` | Xor | 19.6.152 | [L2093](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2093) | 72 | 39 | 0 | 0 | S4 |
| `0x80` | Not | 19.6.93 | [L2098](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2098) | 82 | 43 | 0 | 0 | S4 |
| `0x81` | FindSetLeftBit | 19.6.48 | [L2103](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2103) | 68 | 41 | 0 | 0 | S4 |
| `0x82` | FindSetRightBit | 19.6.49 | [L2108](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2108) | 80 | 35 | 0 | 0 | S4 |
| `5B 28` | FromBCD | 19.6.54 | [L1833](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1833) | 22 | 20 | 0 | 0 | S4 |
| `5B 29` | ToBCD | 19.6.135 | [L1838](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1838) | 26 | 20 | 0 | 0 | S4 |
| `0x90` | LAnd | 19.6.68 | [L2178](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2178) | 89 | 43 | 26 | 42 | S4 |
| `0x91` | LOr | 19.6.79 | [L2183](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2183) | 87 | 43 | 36 | 42 | S4 |
| `0x92` | LNot | 19.6.74 | [L2188](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2188) | 90 | 43 | 39 | 42 | S4 |
| `0x93` | LEqual | 19.6.69 | [L2208](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2208) | 90 | 43 | 43 | 42 | S4 |
| `0x94` | LGreater | 19.6.70 | [L2213](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2213) | 88 | 43 | 19 | 40 | S4 |
| `0x95` | LLess | 19.6.72 | [L2218](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2218) | 88 | 43 | 33 | 42 | S4 |
| `0x73` | Concatenate | 19.6.12 | [L2033](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2033) | 73 | 32 | 7 | 2 | S4 |
| `0x84` | ConcatenateResTemplate | 19.6.13 | [L2118](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2118) | 39 | 27 | 0 | 0 | S4 |
| `0x9E` | Mid | 19.6.85 | [L2258](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2258) | 21 | 16 | 0 | 0 | S4 |
| `0x96` | ToBuffer | 19.6.136 | [L2223](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2223) | 53 | 36 | 0 | 42 | S4 |
| `0x97` | ToDecimalString | 19.6.137 | [L2228](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2228) | 47 | 33 | 0 | 0 | S4 |
| `0x98` | ToHexString | 19.6.138 | [L2233](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2233) | 50 | 38 | 7 | 42 | S4 |
| `0x99` | ToInteger | 19.6.139 | [L2238](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2238) | 76 | 43 | 1 | 8 | S4 |
| `0x9C` | ToString | 19.6.141 | [L2248](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2248) | 32 | 23 | 0 | 0 | S4 |

**System, synchronisation and events (S4)**

| Encoding | Opcode | ASL § | 20.3 row | Machines /90 | FADT ≥ 6 /43 | Table level /90 | QEMU /42 | Runs from |
|---|---|---|---|---:|---:|---:|---:|---|
| `0x86` | Notify | 19.6.94 | [L2128](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2128) | 90 | 43 | 1 | 42 | S4 (queued) |
| `5B 22` | Sleep | 19.6.125 | [L1803](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1803) | 86 | 43 | 0 | 0 | S4 |
| `5B 21` | Stall | 19.6.127 | [L1798](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1798) | 81 | 42 | 0 | 0 | S4 |
| `5B 33` | Timer | 19.6.134 | [L1863](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1863) | 34 | 19 | 5 | 0 | S4 |
| `5B 23` | Acquire | 19.6.2 | [L1808](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1808) | 78 | 41 | 0 | 42 | S4 |
| `5B 27` | Release | 19.6.115 | [L1828](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1828) | 78 | 41 | 0 | 42 | S4 |
| `5B 24` | Signal | 19.6.123 | [L1813](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1813) | 21 | 13 | 0 | 0 | S4 |
| `5B 25` | Wait | 19.6.147 | [L1818](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1818) | 16 | 8 | 0 | 0 | S4 |
| `5B 26` | Reset | 19.6.116 | [L1823](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1823) | 17 | 12 | 0 | 0 | S4 |
| `5B 32` | Fatal | 19.6.46 | [L1858](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1858) | 4 | 3 | 0 | 0 | S4 (logged, counted) |
| `0xCC` | BreakPoint | 19.6.9 | [L2303](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2303) | 37 | 23 | 0 | 0 | S4 (no-op, counted) |

**Definition blocks at run time (S6)**

| Encoding | Opcode | ASL § | 20.3 row | Machines /90 | FADT ≥ 6 /43 | Table level /90 | QEMU /42 | Runs from |
|---|---|---|---|---:|---:|---:|---:|---|
| `5B 20` | Load | 19.6.76 | [L1793](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1793) | 55 | 20 | 0 | 0 | S6 |
| `5B 1F` | LoadTable | 19.6.77 | [L1788](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1788) | 4 | 2 | 0 | 0 | S6 |
| `5B 2A` | Unload | reserved | [L1843](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1843) | 1 | 0 | 0 | 0 | decoded, refused |


**By slice.**

- **S3** decodes every row and executes the constants, the data objects and every declaration (the first two
  tables). It counts all table-level code and skips its construct: on the 42 clients, none of the 29 table-level
  `If` predicates found is a constant or a bare `CondRefOf`, and all 29 read a field unit, so an S3 evaluator of
  constant predicates would run nothing (`scripts/vc_table_level_if.py`, a lower bound) [measured].
- **S4** executes the rows marked S4 that the needs-path closure reaches, method invocation, and table-level code.
- **S5** gives `OperationRegion`, `Field`, `IndexField` and `BankField` their access semantics (§4.4).
- **S6** executes `Load` and `LoadTable` in the forms the corpus uses (§1.4); `Unload` is decoded and refused.
- **Never executed**: `DataTableRegion`, `Nor`, `Revision` (fail loudly, §4.5) and the bytes that are not ACPI
  opcodes (below).

**Composite comparisons.** `LNotEqual`, `LLessEqual` and `LGreaterEqual` are `LNot` followed by `LEqual`,
`LGreater` or `LLess` ([L2188-L2207][s20-lnot-rows]); the decoder fuses the pair into one operator (§7, O7).

**Method invocation.** A NameString in a TermArg position that resolves to a method: 90/90 machines, 1,457 static
call sites on the median machine, at definition-block level on 53/90 (real-firmware §2.2, §2.4). Runs from S4;
S3 counts the table-level ones and skips their construct.

**Never executed:** the obsolete `5B 10`/`5B 11` and ACPICA's internal comment opcode `0xA9`, which are not ACPI
(ACPICA [amlcode.h L110](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/amlcode.h#L110),
[L130-L131](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/amlcode.h#L130-L131)):
they decode as `invalid_opcode`. None appears in the 90 dumps (real-firmware §3).

### 4.4 Fields and region spaces

| Item | Machines /90 | Implemented | Slice | Source of the rule |
|---|---:|---|---|---|
| SystemMemory regions | 90 | yes, through the mapping cache | S5 | [§5.5.2.4.1][s05-spaces] |
| SystemIO regions | 90 | yes, through `port_read`/`port_write`, with NT port protection (§5) | S5 | uACPI [default_handlers.c L654-L761](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/default_handlers.c#L654-L761) |
| PCI_Config regions | 84 | yes; address from `_SEG`/`_BBN`/`_ADR` through the bridges, resolved once at attach; header write protection. Offsets from 0x100 need ECAM (#58): until it lands they fail with `address_unreachable`, counted. 117 regions on 11 of 42 clients reach past 0xFF (`scripts/vc_pci_extended.py`) [measured] | S5 | uACPI T10 ([default_handlers.c L192-L266](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/default_handlers.c#L192-L266)); ACPICA [evrgnini.c L193-L320](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/events/evrgnini.c#L193-L320) |
| EmbeddedControl | 41 | no handler: access fails, counted; `_REG(3, 1)` is never called, so firmware whose `_REG` sets an EC guard flag skips its own accesses (§1.4) | later (N18), or S6 if S5's needs-path count is non-zero | |
| SystemCMOS | 23 | no handler | later (N23) | ACPICA's built-in handler is an empty stub ([exregion.c L456-L508](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exregion.c#L456-L508)) [code] |
| GenericSerialBus, GeneralPurposeIo, IPMI, PlatformRtMechanism, OEM 0x80-0xFF | 9, 5, 2, 1, 5 | no handler | later | |
| SMBus, PciBarTarget, PCC, FFixedHW | 0 | no handler | never without a target | |
| AccessType ByteAcc, WordAcc, DWordAcc, QWordAcc | 90, 83, 87, 34 | yes, per datum of that width; on i686 a 64-bit SystemMemory datum is two 32-bit accesses, low then high, through an `asmutils` routine (§5.3) | S5 | ACPICA [exfldio.c L587-L1018](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exfldio.c#L587-L1018) |
| AnyAcc | 88 | yes, as byte access | S5 | ACPICA [exprep.c L213-L292](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exprep.c#L213-L292) [code]; uACPI: "NT always accesses AnyAcc fields one byte at a time, no matter their size or alignment" ([interpreter.c L1847-L1852](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L1847-L1852)) [comment], and its code does so (L1853-L1857) [code]; LAI picks a power-of-two width instead ([opregion.c L65-L82](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/opregion.c#L65-L82)) [code] |
| BufferAcc | 12 | on the three spaces, as byte access; the protocol buffers of GSBus, SMBus and IPMI wait for their handlers | S5 | ACPICA, same file |
| UpdateRule Preserve, WriteAsZeros, WriteAsOnes | 90, 54, 0 | all three | S5 | [§19.6.47][s19-field] |
| LockRule `Lock` | 81 | until the global lock lands, an access fails with `global_lock_unavailable`, counted: never made without the lock, since firmware in SMM may hold it at that moment. The lock lands with the EC unless S5's needs-path count is non-zero (§9) | S5 / later | [§19.6.47 L6836-L6842][s19-field-lock] |
| `AccessAs` (`0x01`) | 13 | changes the width of the following units | S5 | |
| `Connection` (`0x02`), `ExtendedAccessAs` (`0x03`) | 9, 9 | decoded; their accesses fail with "space unsupported" | S3 decode | |
| IndexField, BankField | 67, 19 | yes: the index or bank register written, then the data accessed, through an internal loop, not C recursion (uACPI recurses in C, [io.c L274-L340](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/io.c#L274-L340) [code]) | S5 | ACPICA [exfldio.c L453-L541](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exfldio.c#L453-L541) |
| Units spanning two accesses | 90 | yes, the split path; one aligned access takes the fast path | S5 | |
| Units wider than 64 bits (read as Buffer) | 76 | yes, up to the object size cap of §6 | S5 | [§19.6.47][s19-field] |

Machine counts: `scripts/synthesis/real90-speccensus.json` [measured] and real-firmware.md §2.5.

### 4.5 What an unimplemented opcode, space or object does

| Case | Behaviour | Counter |
|---|---|---|
| An opcode decoded but not executed in this build (an earlier slice, outside the needs-path closure, `DataTableRegion`, `Nor`, `Revision`) | in a method: the evaluation fails with `unimplemented_operator`; at definition-block level: the innermost enclosing PkgLength construct is skipped and the load goes on | `unimplemented_operator[opcode]` |
| `Load` from a Buffer; `LoadTable` with a non-empty RootPath, ParameterPath or ParameterData | fails as above | `load_form_unsupported` |
| A `Lock` field access or `Acquire (\_GL)` before the global lock lands | fails as above | `global_lock_unavailable` |
| A byte that is not an opcode | as above, with `invalid_opcode`; at table level without an enclosing construct, the rest of that table is abandoned and the next table loads | `invalid_opcode` |
| `Unload` | its operand is evaluated and nothing else happens (uACPI's choice) | `unload_refused` |
| A region space without a handler | the field access fails with `space_unsupported`; `_REG` is never called for that space | `space_unsupported[space]` |
| `Connection`/`BufferAcc` protocol on an unsupported space | same | same |

"Loudly" means: one log line with table, offset, opcode and method path, the counter in the `[LPLTLM] acpi …` boot
line, and a typed error to the caller. Never a panic, never a silent no-op (LAI's missing `default` makes an
unknown space a silent no-op, [opregion.c L193-L277](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/opregion.c#L193-L277) [code]).

---

## 5. Semantics that bite

Each row is a decision. "Credit" names where the rule or its evidence comes from. Every row becomes at least one
test (§8).

### 5.1 Names and loading

| Topic | Rule implemented | Credit and evidence |
|---|---|---|
| Upward search | Only a single NameSeg with no prefix searches the current scope, then each parent up to the root; `\`, `^`, Dual and Multi names never search. | [§5.3][s05-search]; ACPICA [nsaccess.c L507-L531](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nsaccess.c#L507-L531); uACPI [interpreter.c L609-L617](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L609-L617) [code]. 88.7 % of references are single segments; 14 % climb five levels or more (real-firmware §2.2, §5.3). A name defined in a device and at the root resolves to the nearer one; `\_SB.PC02.PCID` resolves to itself (real-firmware §5.3). |
| Creating a name | Only the last segment is created; the others must exist. | [§5.3][s05-create] |
| Duplicate at load | The first definition stays; the duplicate is skipped and counted. | The spec calls a collision "fatal" ([§5.3][s05-collision]); ACPICA turns it into `AE_OK` at module level ([psloop.c L312-L327](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psloop.c#L312-L327)), uACPI warns ([interpreter.c L5995-L6008](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L5995-L6008)); rust-osdev silently replaces, which we avoid ([namespace.rs L169-L184](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/namespace.rs#L169-L184)) [code]. Duplicate SSDTs on 4/90 machines (real-firmware §5.2). |
| A checksum that fails | A DSDT or SSDT whose header and length are sane is loaded anyway, logged and counted (`checksum_bad_loaded`); the static tables follow Q20. | Both references load such a table by default: ACPICA warns and returns `AE_OK` because `ACPI_CHECKSUM_ABORT` is `FALSE` ([utcksum.c L68-L81](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utcksum.c#L68-L81), [acconfig.h L54-L60](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acconfig.h#L54-L60)); uACPI warns unless `UACPI_FLAG_BAD_CSUM_FATAL` is set ([uacpi.h L71-L75](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/uacpi.h#L71-L75), [tables.c L426-L452](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/tables.c#L426-L452)) [code]. 2/90 machines ship a bad checksum, one of them on the SSDT named `CST` that holds the machine's `_CST` (real-firmware §2.1). Slice 1's lookup returns only valid tables ([acpi.c L477 at `a1f838b`](https://github.com/MasterLaplace/LplKernel/blob/a1f838b3e63a35e8cabbfbf596f0d04b8d7f54cc/kernel/arch/i386/cpu/acpi.c#L477)), amended in S1. |
| Table-level code | Runs in place, in order, in a frame with locals (§3.6). Single pass: a call or field at table level sees only what is declared before it. S3 counts and skips it; S4 runs it. | [§5.4.2][s05-dblevel]: "all term objects … are allowed at the Definition Block level". `If` at table level on 69/90, calls on 53/90. ACPICA "Some AML code depends on this behavior" ([nsparse.c L36-L46](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nsparse.c#L36-L46)) [comment]. |
| Region and field operands | Evaluated when the declaration runs, as ACPICA's code does ([dswexec.c L673-L686](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dswexec.c#L673-L686)) [code]; its guide still describes a deferred evaluation (`documents/acpica-reference.pdf` §4.2.7 "Just-in-time Operation Region Initialization", pp. 46-47) [comment, stale]. Operands such as `(PMBS + 0x80)` or `((\_SB.PCI0.MHBR << 0x0F) + 0x5000)` are common: `Add` at table level on 63/90, `ShiftLeft` on 64/90 [measured]. | S3 records them as pending and counts them; S4 evaluates them. |
| The dead first-method rule | Not implemented. | "The first object present in a Definition Block must be a named control method" ([§5.4.2][s05-firstmethod]); no table measured does this (specification report §2.2). |
| Integer width | Fixed once from the DSDT revision: below 2, every integer of every table is 32 bits; results masked; `Ones` is `0xFFFFFFFF`; Integer→Buffer gives 4 bytes. | [§19.6.29][s19-intwidth]; ACPICA [tbinstal.c L76-L81](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/tables/tbinstal.c#L76-L81) [code]; uACPI: "we mimic the NT implementation: treat all other definition blocks as if they were the same revision as DSDT" ([context.h L78-L83](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/internal/context.h#L78-L83)) [comment], done at [tables.c L737-L738](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/tables.c#L737-L738) [code]. 21/90 DSDTs are revision 1, none with FADT ≥ 6.0; QEMU `pc` and `q35` are revision 1, `microvm` revision 2. |
| `External` | Creates nothing at run time. | uACPI [interpreter.c L5978-L5984](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L5978-L5984) [code]; [§19.6.45][s19-external]. ACPICA differs: `External` is a named opcode ([psopcode.c L310](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psopcode.c#L310)), sent to its load pass ([dswexec.c L300-L313](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dswexec.c#L300-L313)), which enters the name ([dswload2.c L284-L328](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dswload2.c#L284-L328)); executed, it is ignored with an error log, "Executed External Op" ([exoparg3.c L241-L252](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exoparg3.c#L241-L252)) [inferred, not run]. The `acpiexec` namespace oracle drops those nodes before comparing (§8.4). |
| Unresolved names | An error only when executed: inside methods on 68/90 machines they sit behind `CondRefOf` or capability bits (real-firmware §2.2). | ACPICA tolerates NotFound in load passes, `CondRefOf` and packages ([psargs.c L372-L399](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psargs.c#L372-L399)) [code] |
| Names inside packages | Kept as a lazy path with its scope, resolved when the element is read, at most 8 indirections, then `name_resolution_loop`. | uACPI T8 ([interpreter.c L859-L901](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L859-L901)), LAI ([exec.c L2009-L2019](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec.c#L2009-L2019)), rust-osdev with the bound and the reason: resolving at creation "makes resolution depend on the order tables are loaded in" ([mod.rs L2990-L2996](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L2990-L2996)) [comment]. 22.4 k such elements on 90/90 machines (real-firmware §2.3). |
| Package size mismatches | Fewer initialisers than `NumElements`: padded with Uninitialized; more: truncated; both counted. | ACPICA: "some BIOS code modifies the AML on the fly to adjust the NumElements" ([dspkginit.c L49-L59](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dspkginit.c#L49-L59)) [comment]; the orphan packages left at table level on 7/90 machines fit that story (real-firmware §5.2). |
| Orphan data and `Noop` at table level | Evaluated and dropped. | 7/90 and 17/90 machines (real-firmware §5.2); the spec forbids unnamed objects at the root ([L14051-L14052](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L14051-L14052)). |
| Objects a method creates | Temporary, removed at its exit even when created with a full path elsewhere; a second creation of the same name in the same frame (a `Name` inside a `While` body) is an error. | [§5.5.2.3][s05-locals]; [§19.6.148][s19-while] warns about names in loop bodies. 117 k such objects across the sample; a `Method` inside a method on 46/90 (real-firmware §2.2). |

### 5.2 Methods and values

| Topic | Rule implemented | Credit and evidence |
|---|---|---|
| Calling convention | Up to 7 arguments, 8 locals starting Uninitialized; arguments are shared, not copied, and become writable only through a reference. A host-initiated evaluation must pass exactly `argc` arguments. | [§5.5.2.1][s05-args], [§5.5.2.2][s05-callconv]; ACPICA checks counts strictly ([dsmethod.c L523-L536](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dsmethod.c#L523-L536)) [code] |
| Implicit return | A method that ends without `Return`, whose caller uses the result, returns Integer 0; counted. See Q4. | 30.4 % of methods have no `Return` (real-firmware §2.4). LAI returns 0 ("ACPI does an implicit Return(0)", [exec.c L1416-L1428](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec.c#L1416-L1428)) [comment]; ACPICA's slack mode returns the last value ([dsutils.c L76-L140](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dsutils.c#L76-L140)) [code], else 0: "Start method evaluation with an implicit return of zero. This is done for Windows compatibility" ([psxface.c L196-L210](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psxface.c#L196-L210)) [comment], and zero for a nested call that returns nothing ([psparse.c L631-L642](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psparse.c#L631-L642)) [code]; uACPI gives nothing ([interpreter.c L6550-L6560](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L6550-L6560)); rust-osdev gives nothing to a nested caller, which then takes the next term as operand ([mod.rs L1132-L1134](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L1132-L1134)) [inferred]. The spec: "undefined" ([§19.6.118][s19-return]). |
| Uninitialized operands | Reading an uninitialized `LocalX` or `ArgX` as an operand gives Integer 0, counted (ACPICA's slack behaviour, [dsmthdat.c L418-L436](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dsmthdat.c#L418-L436)). An uninitialized package element is an error. See Q5. | |
| `Serialized` | A sync-level check: calling a Serialized method whose SyncLevel is below the current level fails with `mutex_order`; the same evaluation may re-enter it. No mutex object exists, because one owner runs AML (§3.7). | [§19.6.84 L8690-L8696][s19-method-serialized]; uACPI's check ([interpreter.c L4840-L4847](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L4840-L4847)); ACPICA creates the method mutex lazily, "to minimize the object count" ([dsmethod.c L352-L366](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dsmethod.c#L352-L366)) [code]. 37.6 % of methods, on 87/90 machines; LAI and rust-osdev ignore the flag. |
| Mutexes | `Acquire` returns 0 on success, Ones on timeout ([§19.6.2][s19-acquire]); nesting by the owner allowed; an `Acquire` below the current sync level is ignored and counted; a `Release` out of order at the same level is accepted; everything held is released when the evaluation ends. | [§19.6.88 L8845-L8863][s19-mutex-rules]; uACPI's tolerances ([interpreter.c L3967-L3974](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L3967-L3974), [L198-L236](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L198-L236)) [code] |
| Events | When the needs-path closure reaches them (`needs.md` §3 saw no need for them): `Signal` counts up, `Reset` clears, `Wait` consumes a pending signal or reports a timeout at once, since nothing else can signal while the only AML owner waits. | §3.7; `Wait` on 16/90, none of the 7 newest |
| Implicit source conversion | Operands are converted to the type the operator needs: Buffer→Integer from its first 4 or 8 bytes, String→Integer as hexadecimal up to the first non-hex character. An impossible conversion fails the evaluation. | [§19.3.5.4][s19-source-conv], [§19.3.5.7][s19-conv-detail]. ACPICA also accepts a `0x` prefix and gives 0 for an empty string ([utstrtoul64.c L176-L262](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utstrtoul64.c#L176-L262)); we follow ACPICA, the host oracle, and count the cases the spec forbids. uACPI converts nothing for arithmetic ([interpreter.c L5150-L5156](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L5150-L5156)) [code]. |
| Mixed-type comparisons and `Concatenate` | The second operand is converted to the first's type. | The spec ([§19.3.5.4][s19-source-conv]) and ACPICA ([exmisc.c L464-L486](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exmisc.c#L464-L486)); uACPI requires the same type ([interpreter.c L3627-L3635](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L3627-L3635)), "TODO: typecheck at parse time" [code]. Which one NT does is not established; the counter `mixed_type_comparison` on the corpus says whether it matters. |
| Storing | `Store` to a named object of fixed type converts to that type; to a Buffer it copies bytes into the existing buffer, keeping its size (NT's "implicit cast"); `LocalX` is overwritten; an `ArgX` that holds a reference writes through it; fields, buffer fields and buffer indices are always written through. `CopyObject` replaces the target's type, except for fields. A store to a constant does nothing. | [§19.3.5.8][s19-store-rules]; uACPI's NT rules with their tests ([interpreter.c L1009-L1092](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L1009-L1092), [L2488-L2704](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L2488-L2704)); rust-osdev's write-up of the same cases ([mod.rs L2443-L2516](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L2443-L2516)) [code] |
| References | `Index` on a package refers to the slot, not to a copy; `DerefOf` follows the chain to its bottom; `Increment`/`Decrement` work in place through references and fields; a `RefOf (LocalX)` cannot outlive its frame (the return path copies). | uACPI §5.3 items 3 and 8 ([interpreter.c L2902-L2925](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L2902-L2925), [L4194-L4201](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L4194-L4201), [L4773-L4780](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L4773-L4780)) [code]. Intel's `_CST` writes `DerefOf (C6TM [Zero]) [0x07] = C6MW`, a byte of a buffer inside a named package (`needs.md` N12). LAI's `RefOf (LocalX)` dangles after return ([exec.c L916-L931](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec.c#L916-L931)) [inferred]. |
| Logical results | True is Ones (masked to the width), false is 0, for every logical operator; a predicate is true when its integer is non-zero. | ACPICA ([exoparg2.c L585-L592](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exoparg2.c#L585-L592)); LAI returns 1 for `LAnd`/`LOr`/`LNot` and Ones for comparisons ([exec.c L635-L713](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec.c#L635-L713)) [code], a mismatch to avoid. |
| `LAnd`/`LOr` on buffers | Read the first 4 bytes. | NT, per uACPI ([interpreter.c L3645-L3647](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L3645-L3647)) [code] and rust-osdev ("uACPI and NT only compare the first 4 bytes", [mod.rs L2039-L2040](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L2039-L2040)) [comment] |
| Shifts and division | A shift by the integer width or more gives 0; division or modulo by zero fails the evaluation. | ACPICA for the shift ([exmisc.c L315-L323](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exmisc.c#L315-L323)) and the zero divisor (`AE_AML_DIVIDE_BY_ZERO`, [utmath.c L390-L397](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utmath.c#L390-L397); the `ACPI_USE_NATIVE_DIVIDE` variant at L521-L586), uACPI ([interpreter.c L2724-L2797](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L2724-L2797)) [code]. LAI leaves both to C undefined behaviour (`#DE` in ring 0); rust-osdev masks the shift count and panics on a zero divisor (lai-and-rust-acpi.md A5, B3). |
| `CreateField` reads | A field made by `CreateField` reads as a Buffer; the fixed-width `Create*Field` read as Integers. A field of length 0 is valid. | ACPICA "to match other AML interpreter implementations" ([exfield.c L170-L211](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exfield.c#L170-L211)) [code]; uACPI on NT for length 0 ([interpreter.c L4358-L4371](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L4358-L4371)). Bounds are computed in 64 bits (ACPICA's 32-bit check wraps, acpica.md §5.3 item 2 [reading]). |
| `Revision` | Decoded only: fails with `unimplemented_operator`. | 0/90 machines read it. If a count ever asks, the value to choose between: uACPI's 1 ([opcodes.h L1288-L1296](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/internal/opcodes.h#L1288-L1296)) and ACPICA's release date ([dsobject.c L436-L438](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dsobject.c#L436-L438), `0x20260930` at [acpixf.h L19](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acpixf.h#L19)) [code]. |
| `Fatal`, `BreakPoint`, `Debug` | `Fatal` logs and counts, then execution continues; `BreakPoint` counts; stores to `Debug` are logged at debug level and compiled out of release images. | uACPI ([interpreter.c L4084-L4109](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L4084-L4109)) [code]. `Fatal` on 4/90, in an EC query and a GPE handler (real-firmware §2.6). |

### 5.3 Regions and fields

| Topic | Rule implemented | Credit and evidence |
|---|---|---|
| Field datum engine | Per datum of the access width; read-modify-write by UpdateRule; a unit no wider than the integer width reads as an Integer, a wider one as a Buffer. | [§19.6.47][s19-field]; ACPICA [exfldio.c L587-L1018](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exfldio.c#L587-L1018) [code] |
| Strings and buffers stored to a field | Written in field-sized chunks, as NT does. | uACPI ([io.c L746-L755](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/io.c#L746-L755), [interpreter.c L969-L1007](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L969-L1007)) [code] |
| Region bounds | An access must fit the region; rounding the region length up to the access width is allowed and counted. | ACPICA, slack mode ([exfldio.c L132-L195](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exfldio.c#L132-L195)) [code]; "WordAcc cannot read the last byte of an odd-length operation region" ([§19.6.47 L6829-L6831][s19-field-wordacc]) |
| Region lifetime | Attached at the first access, not at declaration: handler chosen, PCI address resolved, mapping taken from the cache. | ACPICA ([evregion.c L170-L230](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/events/evregion.c#L170-L230)), uACPI T9 ([opregion.c L240-L354](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/opregion.c#L240-L354)) [code] |
| `_REG` | `_REG(PCI_Config, 1)` during initialisation (§3.8 step 3), for the scopes of a table loaded at run time once its loading evaluation ends, and at once for a PCI_Config region a method creates after initialisation; none for SystemMemory and SystemIO (§1.4); orphan `_REG` for EC and GPIO comes with their handlers. | [§6.5.4][s06-reg], with its exceptions ([L8188-L8201][s06-reg-exceptions]); ACPICA's orphan `_REG`: "presumably the Windows ACPI implementation always calls the _REG method" ([evregion.c L860-L946](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/events/evregion.c#L860-L946)) [comment] |
| Port protection | Reads of protected ports return 0 and writes are dropped, counted, never an error. Always: PIC `0x20-0x21`, `0xA0-0xA1`, ELCR `0x4D0-0x4D1`, PCI mechanism #1 `0xCF8-0xCFF`. Once AML has asked `_OSI` for Windows XP or newer: DMA, PIT, RTC/CMOS, page and arbitration ports. Only the first port of an access is checked. Port `0xB2` (SMI command) stays allowed. | uACPI's table, "These ranges match the NT ACPI driver 1:1, verified via a traced QEMU run with injected AML" ([default_handlers.c L654-L761](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/default_handlers.c#L654-L761)) [comment for the NT claim, code for the table]. ACPICA's conditional rule fires only when the newest Windows asked is exactly XP, so never on modern firmware: every conditional port is tagged `ACPI_OSI_WIN_XP` ([hwvalid.c L61-L77](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/hardware/hwvalid.c#L61-L77)) and compared with `==` ([L163-L167](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/hardware/hwvalid.c#L163-L167)) [code]. The kernel drives the PIT, the PIC and PCI itself; AML must not race it. |
| PCI header protection | Writes below the subsystem vendor ID (`0x2C`) and from the expansion ROM to the interrupt line (`0x30-0x3B`) of a type-0 header, and to the whole header of other types, are dropped and counted. | uACPI ([default_handlers.c L438-L461](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/default_handlers.c#L438-L461)) [code] |
| Addresses and mappings | A region above 4 GiB fails on i686, never truncated. An access maps only the pages it touches, as one entry of the mapping cache, never the declared region: 114 SystemMemory regions longer than 1 MiB on 40 of 42 clients (`scripts/verify_completeness_probe.py`) [measured]. The host's `memory_type` decides the kind: ACPI NVS or reclaim memory is mapped cached read-write, anything else uncached (Q16). | `needs.md` §5 item 13 (literal bases above 4 GiB on 2/62 dumps, both servers); ACPICA keeps physical addresses 64-bit even on 32-bit hosts because of PAE wrap (Linux bug 87971, [actypes.h L170-L201](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/actypes.h#L170-L201)) [comment] |
| 64-bit data on i686 | A `QWordAcc` SystemMemory datum (34/90 machines, 25/43 modern, real-firmware §2.5) is two 32-bit accesses, low then high, through an `asmutils` routine documented in `asmutils.h`, since inline assembly is forbidden; one access on x86-64. The simulated host sees the same order. Counted: `split_qword_accesses`. | ACPICA leaves the split to the compiler: a C 64-bit load or store through `ACPI_GET64`/`ACPI_SET64` ([exregion.c L236-L238](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exregion.c#L236-L238), [L268-L270](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exregion.c#L268-L270), [acmacros.h L30](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acmacros.h#L30)) [code], whose order on a 32-bit build C does not fix. Low then high is what 32-bit Linux does for ACPI memory reads [claimed, not pinned]. |

### 5.4 The Windows identity real firmware expects

| Object | Answer | Credit and evidence |
|---|---|---|
| `\_OSI` | A native method. TRUE for each Windows string of a data table, from "Windows 2000" to the newest Microsoft defines (ACPICA's list ends at "Windows 2022", [utosi.c L65-L87](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utosi.c#L65-L87)); FALSE for "Linux", "Darwin", "FreeBSD", "HP-UX", "OpenVMS", vendor `Linux-*` strings and every feature string, "Processor Aggregator Device" and "3.0 Thermal Model" included. The newest Windows string asked is recorded (it drives port protection). | ACPICA's policy: "New BIOSs tend to test only the case where the host responds TRUE to the latest version of Windows" ([utosi.c L22-L46](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utosi.c#L22-L46)), "Linux" removed in 2009 ([L53-L56](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utosi.c#L53-L56)) [comment]; [§5.7.2][s05-osi] notes that non-Windows implementations "often reply TRUE to all Windows _OSI requests" ([L17919-L17926][s05-osi-windows]). 85/90 machines call `_OSI` (median 11 calls); "Windows 2015" on 41/43 modern machines; "Linux" on 42/90 (real-firmware §0 items 4 and 6, §5.5); a 2023 notebook drops its OS year from 2015 to 1000 when told "Linux" and loads less (`needs.md` N11). |
| `\_OS` | `"Microsoft Windows NT"` | ACPICA: it "usually guarantees that the ASL will execute down the most tested code path" ([acconfig.h L36-L43](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acconfig.h#L36-L43)) [comment]; referenced on 41/90, and `_OSI` ladders fall back on it (real-firmware §2.10). This overrides `needs.md` N11's "leave out `\_OS`". |
| `\_REV` | 2 | "other ACPI implementations permanently return 2" ([utglobal.c L90-L97](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utglobal.c#L90-L97)) [comment]; [§5.7.4][s05-rev] |
| `\_GL` | A predefined mutex. Until the FACS global lock lands, `Acquire (\_GL)` fails with `global_lock_unavailable`, counted. When the FACS is absent (4/90 machines, real-firmware B.2) or the platform is hardware-reduced, which has no global lock ([§3.11.1 L1350][s03-hwreduced]), acquiring `\_GL` takes only the AML mutex. | [§5.7.1][s05-gl-mutex]; the lock itself is a FACS field ([§5.2.10.1][s05-gl]); `Acquire (\_GL)` on 2/16 clients (lai-and-rust-acpi.md C6) |
| Predefined scopes | `\`, `\_GPE`, `\_PR_`, `\_SB_`, `\_SI_`, `\_TZ_` | [§5.3.1][s05-rootns]; `\_PR_` is deprecated but servers still reference `\_PR_.Pxxx` (specification report §5.3) |

`_OSI` is called through `CondRefOf (\_OSI)` on 85/90 machines first: `\_OSI` must exist as a node, never only as
a special case in the call path. Idea: `_OSI`, `_OS`, `_REV` as namespace objects with a native body (LAI,
[ns.c L450-L473](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/ns.c#L450-L473);
uACPI, [namespace.c L108-L116](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/namespace.c#L108-L116)) [code].

### 5.5 Time

| Topic | Rule implemented | Credit and evidence |
|---|---|---|
| `Sleep` | Gives up the processor for the requested milliseconds (at boot, a halt until a tick deadline, §3.3), without the 2 s cap of ACPICA and uACPI; the request is charged to the evaluation's time budget first, and a request that would exceed it fails the evaluation before sleeping. See Q3. | [§19.6.125][s19-sleep]; ACPICA caps at 2 s ([exsystem.c L195-L215](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exsystem.c#L195-L215)), uACPI copies the cap ([interpreter.c L3464-L3474](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L3464-L3474)) [code]. `Sleep (0x0BB8)`, 3 s, on 11/39 sampled machines (uacpi.md §5.6). `ASRT`'s `While (One)` sleeps 60 s per turn on 8/90 machines, 3 of the 7 newest (real-firmware §5.1). |
| `Stall` | Busy-waits up to 255 µs; above, it becomes a sleep of the rounded-up milliseconds; both counted beyond 100 µs. | [§19.6.127][s19-stall] limits it to 100 µs; ACPICA warns above 100 and fails above 255 ([exsystem.c L144-L175](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exsystem.c#L144-L175)) [code]; `Stall (0xFF)` on 3/90 machines (real-firmware §4). At boot the kernel's spin rounds up to a whole tick (§3.3); the ticks waited are counted next to the microseconds asked, so the boot time lost shows. |
| `Timer` | 100 ns units, monotonic: the kernel tick in ring 0; in parity runs the virtual clock, which also advances by a fixed quantum on each read (§2). | [§19.6.134][s19-timer] |

---

## 6. Robustness

### 6.1 Limits

Every limit is a counter compared against a cap, reported in the `[LPLTLM] acpi` line with its peak, and has one
test that hits it. Values are [estimate] with their basis; the corpus run of S4 confirms or raises them.

| Limit | Cap | Basis |
|---|---|---|
| Frames per evaluation (internal frames included) | 64 | longest static call chain 15 (`real90-speccensus.json`, `method_static_call_depth_max`) [measured], recursion on 10/90 machines, `\_SB.PC02.PCID` calls itself unconditionally on 3 (real-firmware §2.4); uACPI 256 ([config.h L47-L55](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/platform/config.h#L47-L55)), rust-osdev 1,000 ([mod.rs L147-L153](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L147-L153)), ACPICA none globally, 255 per method ([dsmethod.c L340-L346](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dsmethod.c#L340-L346)); an ASLTS note saw a Windows stack overflow at "about 10-12 method calls depth" ([msfail.asl L30-L66](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/tests/aslts/src/runtime/collections/Identity2MS/abbu/MsFail/msfail.asl#L30-L66)) [comment, undated] |
| Blocks per evaluation | 512 | block depth median 19.5, maximum 86 (a 169-arm `ElseIf` ladder, real-firmware §2.4) |
| Pending operators | 256 | expression depth at most 11 (real-firmware §5.1); term nesting 42 on 12 servers (specification report §4 item 7), 90 at most on the 90 machines (`real90-speccensus.json`, `term_nesting_depth_max`) [measured] |
| Values | 1,024 | 7 arguments + 8 locals per frame, plus operands |
| Temporary nodes per evaluation | 1,024 | objects created inside one method: buffer fields of a `_CRS`, `_T_x`, regions |
| Internal (host-triggered) evaluations nested | 2 | `_ADR` inside a PCI attach inside a `_REG` |
| Loop iterations per evaluation (every `While` back-edge) | 2^20 | a `Stall (10)` poll gets 10 s of hardware time; 83/90 machines poll in loops with `Sleep` or `Stall` (real-firmware §2.4) |
| Time requested per evaluation (`Sleep` ms + `Stall` µs) | 30 s | the 3 s server sleeps pass; `ASRT`'s 60 s sleep fails before sleeping; ACPICA's loop timeout is 30 s of wall time ([acconfig.h L119-L121](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acconfig.h#L119-L121)), ours counts requests instead |
| Package elements | 255 for `Package`, 4,096 for `VarPackage` | `NumElements` is a byte; 255 elements for `Package` ([§19.3.5][s19-pkg255]); `VarPackage` used once in 90 machines (`opcode_levels.md`); largest package 253 elements (`real90-speccensus.json`, `package_elements_max`) [measured] |
| Buffer, string, field-as-buffer | 64 KiB | largest buffer literal 21.8 KiB, string 4,095 characters, the common wide fields 32 KiB; the 4 MiB `BUFF` of one server is refused and counted (real-firmware §2.3) |
| Name indirections in packages | 8 | rust-osdev ([mod.rs L141-L144](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L141-L144)) |
| Namespace nodes | 65,536 server, 32,768 other profiles | 33.6 k on the largest server, 12 k on the largest client (real-firmware §2.2, `needs.md` §6) |
| Namespace arena | 8 MiB server, 4 MiB other profiles | §3.4 footprint |
| Loaded tables; bytes copied by `Load` | 128; 256 KiB | tables in one dump: median 31, maximum 51 (`needs.md` N2, §6); `Load` copies 26 KB at most per machine (eight `PmRef` tables), while `LoadTable` copies nothing (§3.5; `scripts/vc_dynamic_sizes.py`) [measured] |
| Held mutexes per evaluation | 32 | mutexes per namespace: median 7, maximum 25 on the 90 machines (real-firmware §2.6) |
| Notify ring | 64 entries; overflow dropped and counted | median 87 `Notify` sites per machine (real-firmware §2.6), few per evaluation |
| Region mapping cache | 64 entries, each the pages one access touched | 114 SystemMemory regions longer than 1 MiB on 40 of 42 clients (§5.3), so no entry ever maps a whole region |

The node and arena caps depend on the profile, and probe counts follow the hash capacity. A parity run passes
the target's caps to both sides and prints them in the record header (§8.7).

### 6.2 What happens on bad AML

The kernel never panics, traps or meets C undefined behaviour because of AML. LAI has 147 `lai_panic` calls and 170
`LAI_ENSURE` sites, several reachable from AML; rust-osdev keeps panics on division by zero, short buffers and bad
PkgLengths (lai-and-rust-acpi.md A5, B5) [measured, code].

| Fault | In a method | At definition-block level | Status |
|---|---|---|---|
| PkgLength crossing its parent | the evaluation fails | the rest of the table is abandoned, its objects kept, the next table loads | `pkglength_out_of_parent` |
| Truncated operand, bad NameString | fails | the innermost enclosing construct is skipped | `truncated` |
| Invalid or unimplemented opcode | fails | construct skipped | `invalid_opcode`, `unimplemented_operator` |
| Name not found when executed | fails | construct skipped | `name_not_found` |
| Wrong type, impossible conversion | fails ("abort the running control method", [§19.3.5.4][s19-source-conv]) | construct skipped | `type_mismatch` |
| Division by zero | fails | construct skipped | `divide_by_zero` |
| Index or buffer field out of range | fails (LAI panics, [exec.c L722-L749](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec.c#L722-L749)) | construct skipped | `index_out_of_range` |
| Reference cycle | fails | construct skipped | `reference_cycle` |
| A limit of §6.1 | fails with the limit's name | construct skipped | `limit_<name>` |
| Region without handler, out of bounds, unreachable (above 4 GiB on i686, PCI offset from 0x100 before #58) | fails | construct skipped | `space_unsupported`, `region_bounds`, `address_unreachable` |
| `Lock` field or `\_GL` before the global lock lands | fails | construct skipped | `global_lock_unavailable` |
| A `Load` or `LoadTable` form the corpus never uses | fails | construct skipped | `load_form_unsupported` |
| Protected port or PCI header | not a fault: zero read, write dropped | same | counted only |
| Arena exhausted | fails | the table load stops | `out_of_memory` |

Every failure unwinds the evaluation completely: temporaries removed, mutexes released, values dropped, the
context reset. The caller receives the typed status and decides; for `_CST`, a failure leaves that CPU on the
states the kernel already enters without ACPI (HLT, MWAIT C1).

### 6.3 Defects of the other implementations, turned into our tests

Each was found by reading, not by running ([reading]/[inferred] in the reports). They are not reported upstream
from here; each becomes a test that keeps the same mistake out of ours.

| Defect seen in | What goes wrong | Our test |
|---|---|---|
| uACPI ([interpreter.c L736](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L736), [L5610-L5621](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L5610-L5621)) | `If (Zero)` whose PkgLength passes the end of its method sends the reader outside the buffer | a method ending in such an `If` fails with `pkglength_out_of_parent`, and the next evaluation runs |
| uACPI ([interpreter.c L4918-L4939](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L4918-L4939)) | a method truncated mid-operator leaks its pending operands | after a truncated method, the object heap holds what it held before |
| ACPICA ([dspkginit.c L128-L140](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dspkginit.c#L128-L140)) | `VarPackage (0xFFFFFFFF)` wraps the element array size on i386 | `limit_package_elements`, on i686 and on the host |
| ACPICA ([dsopcode.c L93-L217](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dsopcode.c#L93-L217)) | `CreateField (B, 0xFFFFFFF0, 0x20, F)` passes a 32-bit bounds check | `index_out_of_range` |
| ACPICA ([acobject.h L55](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acobject.h#L55), [utdelete.c L420-L423](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utdelete.c#L420-L423)) | a 16-bit reference count wraps | a count driven past `0xFFFFFFFF` saturates and the object is never freed |
| ACPICA ([psargs.c L575-L578](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psargs.c#L575-L578)) | an unterminated string makes the parser write into the table | an unterminated string fails with `truncated`; the firmware window stays read-only, so any write would fault the test |
| LAI ([exec-operand.c L434-L443](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec-operand.c#L434-L443)) | a buffer field read ignores the bit offset inside a byte | bit-exhaustive buffer-field reads and writes on the host, a stratified sample in ring 0 (§8.2) |
| LAI ([exec.c L916-L931](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec.c#L916-L931)) | `Return (RefOf (Local0))` hands out a dangling reference | the caller receives a usable value |
| rust-osdev ([mod.rs L1751-L1755](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L1751-L1755), [L1976-L1978](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L1976-L1978)) | `Not`, `FindSetLeftBit`, `FindSetRightBit` always fail, untested (on 82, 68 and 80 of 90 machines) | one test per operator, with a target and without |
| rust-osdev ([mod.rs L1021-L1036](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L1021-L1036), [L1129-L1131](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L1129-L1131)) | objects of a method that ends with `Return` outlive it | after `Return`, the method's `Name` no longer resolves |
| rust-osdev ([mod.rs L2464-L2499](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L2464-L2499), the lock and the field write; [L2872-L2892](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L2872-L2892), `_SEG`/`_BBN`/`_ADR` evaluated for the address) [reading] | a global spinlock held while a PCI field write evaluates `_ADR` | `_ADR` as a method that stores, reached from a PCI_Config write |


---

## 7. Performance

### 7.1 Where the cost is

AML never runs on the idle path or per tick: `_CST` is evaluated once and again only on Notify 0x81, and the idle
path reads a table in kernel memory (`needs.md` §4). The costs to cut are therefore:

- **the load**: median 155 KiB of AML per machine, 1.72 MiB at most, 3.2 k named objects on the median machine,
  33.6 k at most (real-firmware §2.2, §6);
- **the boot evaluations**: `_INI`/`_STA` over thousands of devices, `_OSC`, `_CST` per CPU (224 CPUs on the
  largest server, `needs.md` N3);
- **the heap and the kernel stack**, which the World shares.

### 7.2 Optimisations adopted

Each is cheap to write and has a counter that proves it holds. "Gain" is [estimate] unless measured.

| # | What | Idea from | Need served | Expected gain | Proof (deterministic counter or window) |
|---|---|---|---|---|---|
| O1 | Execute in place from the firmware window; never copy a method body | LAI ([exec.c L2681-L2706](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/exec.c#L2681-L2706)), ACPICA ([nsparse.c L76-L109](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nsparse.c#L76-L109)) | N10 | no copy of 37-81 % of the AML (method bodies, specification report §4 item 1); uACPI copies every body and rust-osdev every call | `allocations` at load excludes methods: a method declaration allocates 0 |
| O2 | Skip method bodies at load by PkgLength | ACPICA ([psloop.c L146-L162](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/parser/psloop.c#L146-L162)), uACPI | N10 | load decodes declarations and table-level code only | `bytes_decoded_at_load` / `aml_bytes` |
| O3 | Integers by value; constants, `LocalX`, `ArgX` pushed without an operator record | LAI ([internal-exec.h L144-L155](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/include/lai/internal-exec.h#L144-L155)); real-firmware §4 item 1 | all | constants are about 30 % of terms; uACPI and ACPICA allocate an object per literal ([interpreter.c L5668-L5682](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L5668-L5682), [dsutils.c L671-L689](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dsutils.c#L671-L689)) [code] | `allocations` = 0 for an integer-only method (test) |
| O4 | One evaluation context, allocated once, never zeroed per evaluation | contrast with uACPI's per-evaluation 7.5 KB zeroed context ([interpreter.c L6487](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L6487)) | all | no allocation and no 5-7 KB clear per `_STA` | `allocations` per evaluation |
| O5 | One hash for the whole namespace, O(1) per probe; upward search ≤ depth probes | contrast with linear sibling scans (ACPICA [nssearch.c L98-L131](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nssearch.c#L98-L131), uACPI [namespace.c L453-L473](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/namespace.c#L453-L473)) and LAI's hash, whose miss probes up to the bucket capacity ([util-hash.h L101-L114](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/util-hash.h#L101-L114)) [code] | all | root holds a median 511 children, 3,346 at most (real-firmware §0 item 10): a scan averages hundreds of compares there, a probe about one | `lookup_probes` / `lookups` |
| O6 | Direct-indexed decode tables, operand kinds packed in a `uint32_t`, one `switch` of handlers | ACPICA, uACPI, LAI, rust-osdev (§3.6) | all | one table load per opcode, no decode-program layer | W4 |
| O7 | `LNot` + comparison fused at decode | the composite rows of [§20.3][s20-lnot-rows]; real-firmware §4 item 2 | all | one operator instead of two for every `!=`, `<=`, `>=` (`LNot` on 90/90 machines, 74 k occurrences) | `fused_comparisons` |
| O8 | `DerefOf (Index (x, i))` read without building a reference | real-firmware §4 item 5 | N12, N14 | no reference object for the most common package read (`Index` 278 k, `DerefOf` 146 k occurrences) | `references_built` |
| O10 | One allocation per string, buffer or package (header + payload) | contrast with two or three elsewhere (uacpi.md §6, LAI A6) | all | half the allocations | `allocations` |
| O11 | Lazy region attach, mapping cache keyed by physical page range | ACPICA's per-region page list ([exregion.c L105-L191](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exregion.c#L105-L191)); contrast with LAI's map on every access ([opregion.c L201-L205](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/opregion.c#L201-L205)) and uACPI's whole-region map ("FIXME", [default_handlers.c L536](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/default_handlers.c#L536)) [code] | N11, N12 | 39 % of regions are created inside methods (5,832 of 14,919, real-firmware §2.5), often over the same NVS: one map per distinct page range per boot | `map_calls`, `mapping_cache_hits` |
| O12 | Field fast path for a unit inside one aligned access | uACPI ([io.c L590-L615](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/io.c#L590-L615)) | N11, N12 | 1,055 of QEMU's 1,069 units, 11,333 of the servers' 14,038 (specification report §4 item 5) | `field_fast_path` / `field_accesses` |
| O13 | PCI address resolved once, at attach. It assumes the kernel never renumbers bridges; hotplug or a bus reassignment would break it, which is why uACPI re-reads bus numbers (uacpi.md T10), and either brings the per-access path back | contrast with LAI ([opregion.c L247-L251](https://github.com/managarm/lai/blob/b481a42d2af53d9bd7d41195d354bb703b40ab07/core/opregion.c#L247-L251)) and rust-osdev ([mod.rs L2872-L2892](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/src/aml/mod.rs#L2872-L2892), "not ideal to do these reads for every native access"), which re-evaluate `_SEG`/`_BBN`/`_ADR` per access, and uACPI, which re-reads bus numbers per access ([default_handlers.c L203-L206](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/default_handlers.c#L203-L206)) [code] | N11 | no method evaluation per PCI field access | `internal_evaluations` |
| O14 | Skip `_STA` in subtrees without `_INI` | ACPICA ([nsinit.c L492-L547](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/namespace/nsinit.c#L492-L547)) | N11 | `_STA` only where an `_INI` may run | `status_evaluations` |
| O15 | No locks, no atomics in the interpreter | §3.7; contrast with uACPI's atomic counts ([shareable.h L40-L58](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/include/uacpi/internal/shareable.h#L40-L58)) and ACPICA's spinlock per count update ([utdelete.c L391-L493](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utdelete.c#L391-L493)) | all | no `LOCK`-prefixed instruction per reference | a CI check that the release objects of `kernel/kernel/acpi/` hold no `lock`-prefixed instruction (`objdump -d`), and W1 on the release kernel |
| O16 | Log calls compiled out below the build's level | contrast with uACPI's runtime level test per decode op ([interpreter.c L5665-L5666](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/interpreter.c#L5665-L5666)) | all | no branch per decoded term in release | a CI check that the release objects call no log function below the release level (`nm`), and W1 on the release kernel; a debug-kernel window cannot show it, since its logs are compiled in |

### 7.3 Candidates, adopted only when a count asks for them

| # | What | Idea from | Adopt when |
|---|---|---|---|
| C1 | Cache each call site's resolved node under a namespace generation number | real-firmware §8 TAKE 4; uACPI A3 | the lookup function's share of W2, as `instructions.sh` lists it per function, exceeds 10 % |
| C2 | Keep a `Name`'s constant package or buffer as AML until first read | specification report §4 item 4 (QEMU's load is "mostly building packages of constants") | the package-building function's share of W1 exceeds 25 % on a corpus machine |
| C3 | Pre-decode the body of a method that loops | — | a `While` body's functions dominate a window |
| C4 | Build a package of constants without the general evaluator | specification report §4 item 4 | as C2; needs a differential test against the general path |
| C5 | Zero-copy string and buffer literals: kept in the table until written, materialised first when a buffer field, an `Index` or `RefOf` reference or a store target is made over one (§3.5) | ACPICA ([dsobject.c L474-L483](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/dispatcher/dsobject.c#L474-L483)) [code]; 109,559 string and 69,458 buffer literals on the 90 machines (`real90-speccensus.json`, `summary.sum.opcodes`) [measured] | the needs-path run shows that most literals are never materialised, and the copy function's share of W1 or W2 exceeds 5 % |

### 7.4 Measurement windows and budgets

Counts come from `tools/instructions.sh` on QEMU's TCG with `-icount`: the same on every run and every machine
(CONTRIBUTING). Budgets follow `tools/server-tick.budget`'s format, one file per window and build. Two builds,
because a `KERNEL_TEST` exists only in a debug kernel (§2):

- **release** (`SMOKE_TESTS=0 ./build.sh --server`): the windows that run at boot, on QEMU's live tables, today's
  bench;
- **debug** (the poison build): the windows inside a `KERNEL_TEST`, among them the ring-0 corpus test on three
  corpus machines chosen in S3 (a revision-2 Intel client with `Load`, an AMD client, a server), so that real
  firmware sets the budgets next to QEMU. Their files end in `.debug.budget` and say which build recorded them.

| Window | Command | Budget files | First target [estimate] |
|---|---|---|---|
| W1 load of the boot tables | release: `tools/instructions.sh --inside advanced_configuration_and_power_interface_machine_language_load_definition_block --times <tables> kernel/lpl.kernel` on `q35`; debug: the same `--inside` during the ring-0 corpus test | `tools/acpi-load.budget`; `tools/acpi-load-<machine>.debug.budget` | ≤ 300 instructions per named object created; 0 allocations per method declared |
| W2 initialisation (§3.8 steps 0-9) | release: `--inside advanced_configuration_and_power_interface_initialize_namespace --times 1` on `q35`; debug: the same during the ring-0 corpus test | `tools/acpi-initialization.budget`; `tools/acpi-initialization-<machine>.debug.budget` | recorded at S6 |
| W3 `_CST`: two made-up processors (Intel's pattern, a Serialized method patching a package of buffers from NVS fields; AMD's static package) and the three corpus machines | debug: `--inside kernel_test_body_<test>`; release: through `-acpitable` once verified (Q18) | `tools/acpi-cst.debug.budget` | recorded at S7; QEMU's DSDT has no `_CST` |
| W4 microbenchmarks: `While (LLess (Local0, 0x03E8)) { Increment (Local0) }`; 1,000 lookups in a 3,346-child scope; the same arithmetic loop under a revision-1 DSDT | debug: `--inside kernel_test_body_<test>` | `tools/acpi-microbenchmark.debug.budget` | ≤ 200 instructions per loop iteration, 0 allocations |

`_PRT` gets no window before N14 gives it a boot consumer.

The first measurement of each window becomes its budget, with a tolerance of 1 %, as `tools/server-tick.budget`
uses ([L11](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/tools/server-tick.budget#L11));
a slice that moves a count past it either explains the gain it bought or is red. Counters (allocations, lookups,
probes, bytes decoded, opcodes executed, map calls) are deterministic, so they also enter the parity fold (§8.7):
host and ring 0 must do the same work, not only reach the same result.

---

## 8. Testing and oracles

### 8.1 The layers

| Layer | What it checks | Where it runs | Oracle |
|---|---|---|---|
| Made-up AML | each rule of §4-§6, each limit, each defect of §6.3 | ring 0 (`KERNEL_TEST`) and the host harness, same sources | the test's own expected values; parity between the two |
| Real firmware on the host | the 114 machines (§8.4) | host | ACPICA `acpiexec`; the census counts; `iasl` disassembly counts; the decoded `[xxxh]` table lines for S2 |
| Real firmware in ring 0 | one corpus machine per boot, its tables given as a Multiboot module, run through the simulated host | ring 0, debug kernel | the host harness's records on the same bytes, compared by `tools/parity.sh` |
| QEMU's live tables | the boot path on today's bench: ACPI mode, load, initialisation | ring 0 on `pc`, `q35` | status and counters only, not folded: live evaluations read emulated hardware the simulated host does not model (§8.7); the host harness loads the same bytes (dumped to serial, §8.3) |
| QEMU's reference tables | 42 `pc`/`q35` configurations and 5 `microvm` | host | ACPICA `acpiexec` |
| uACPI's 105 cases | NT behaviours and edge cases | host | each case's `// Expect:` line |
| Real machines | the full path to `_CST` | ring 0 | the same machine's tables run through the host harness and `acpiexec`; Linux's cpuidle states |

### 8.2 Made-up tables

- Written in ASL by us (MIT), compiled with `iasl` from the pinned ACPICA in a sandbox, committed as the `.asl`
  source plus a generated C array; `tools/acpi/regenerate-fixtures.sh` recompiles and fails on a byte difference.
- Raw-byte cases for what `iasl` will not emit: a crossing PkgLength, orphan `Package` terms at the root, `Noop`
  runs, a 4-byte PkgLength, both forms of `CondRefOf` and `Load`, an unterminated string. Idea: rust-osdev's
  raw-opcode tests ([aml-test-tools lib.rs L347-L400](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/tools/aml-test-tools/src/lib.rs#L347-L400)).
- Region tests build their AML at run time with the physical address of a frame the test owns patched into the
  region's base, in the style of slice 1's `MadeUpFirmware` helper (`kernel/arch/i386/tests/cpu/acpi.c`).
- Bit-exhaustive field tests: every bit offset 0-63 and length 1-64 for buffer fields and for each access width of
  a SystemMemory field, read and write, about 20 k evaluations, **on the host only**. uACPI generates such cases
  over offsets 0-63 and ends 1-64 ([buffer_field.py L40-L59](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/tests/generated_test_cases/buffer_field.py#L40-L59)),
  with `acpiexec -b "execute MAIN"` as the oracle ([L77-L102](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/tests/generated_test_cases/buffer_field.py#L77-L102)),
  2,080 reads and 2,080 writes ([L140-L141](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/tests/generated_test_cases/buffer_field.py#L140-L141)),
  corrected for NT's chunked stores ([L163-L172](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/tests/generated_test_cases/buffer_field.py#L163-L172)) [code];
  ours computes expectations with an independent bit model in the host harness and checks that model once
  against `acpiexec`. Ring 0 runs a fixed stratified sample (each width, the boundary offsets, a few hundred
  cases) that the host folds too, because every debug boot under TCG pays for every test; its instruction count
  is recorded before merging (CONTRIBUTING).
- Behaviour tables modelled on real firmware without copying its bytes: an `_OSI` ladder that sets an OS year
  (`needs.md` N11), an Intel-style `GCAP` that `Load`s its HWP table from an NVS region only when platform `_OSC`
  grants bit 6, an Intel-style `_CST` that patches a package of buffers, an AMD-style static `_CST`, an AMD-style
  `_PIC` that rewrites "PIRQ" fields when its argument is 1, a `_STA` that divides by zero, a loaded table that
  declares a PCI_Config region with a `_REG` in its scope, a FADT whose SCI_EN starts clear, a self-recursive
  `PCID`, an `ASRT`-style endless loop, an 86-deep `ElseIf` ladder, a root `Switch` on `Local0`.

### 8.3 QEMU

- **Live boot.** `./qemu.sh --server` with `-machine q35` and `pc`: the kernel switches to ACPI mode, loads what
  QEMU generates, runs the initialisation and prints `[LPLTLM] acpi` counters and statuses. A debug kernel can also
  print each table as hex lines on COM1; `tools/acpi/host` reads them back from `serial.log`, so the host harness
  loads exactly the bytes ring 0 saw (no QEMU byte is committed: QEMU as a whole is GPL-2.0, `acpi-build.c` is
  GPL-2.0-or-later and the test tables carry no licence notice, which makes them GPL-2.0-or-later,
  [LICENSE L1-L16](https://github.com/qemu/qemu/blob/v10.2.1/LICENSE#L1-L16)).
- **Reference tables** (`tests/data/acpi/x86` at v10.2.1): 42 configurations, 499,288 bytes of AML, plus 5
  `microvm` (revision-2 DSDT, hardware-reduced FADT, `_EVT`). Their FADTs carry zeroed pointers
  (specification report §1.2), so they test the namespace, not the table walk.
- **What QEMU cannot test**: `_CST`, `_PSS`, `_CPC`, `_LPI`, thermal zones, `_OSI`, `Sleep`/`Stall`/`Timer`,
  `Load`/`LoadTable`, table-level code, the EC, 64-bit integers on `pc`/`q35` (specification report §7).
  Every one of these is tested on the corpus and the made-up tables instead.

### 8.4 Real firmware: linuxhw/ACPI (CC-BY-4.0)

- `tools/acpi-corpus.sh fetch` clones [linuxhw/ACPI](https://github.com/linuxhw/ACPI/tree/d6d30c6ef6f45506567f25a302c64f009d8f5f4e)
  blobless at the pinned commit and checks out only the listed reports into `build/acpi-corpus/` (git-ignored).
  It prints the attribution: "ACPI dumps collected by hw-probe users at linux-hardware.org, published by linuxhw
  under CC-BY-4.0; changed: converted from acpidump text to binary tables". No dump byte is committed.
- Three lists: **quick** (the 9 machines real-firmware.md §7 picked for their features: Dell Precision 5680, MSI
  MS-7E16, ASUS NUC14RVH-B, HP Pavilion x2 Detachable, Microsoft Surface Pro, Lenovo IdeaPad Z470, Valve Jupiter,
  Intel ArcherCity, HP ProLiant DL360 G5), run on every pull request that touches the interpreter; **slice** (the
  114 distinct machines with a `.bin`, which `tools/acpi-corpus.sh` derives from the tree listing by
  deduplicating real-firmware.md appendix A and `needs.md` appendix A), run to close a slice; **full** (the 661
  reports with a `.bin`), run on demand. Before the slice list is committed, it is checked against the project's
  own hardware, and any match is replaced by another report of the same class: no list names a machine the
  project works on.
- The harness reads the acpidump text itself (`SIG @ 0x…` then `offset: 16 hex bytes`, real-firmware §1.1) and
  for each machine prints: decode errors, the namespace dump, its fold, the census counts, from S3 the needs-path
  closure, and from S5 the needs-path run (§3.8 steps 0-9) with the opcodes, spaces and access types it executed.
- **Static tables (S2)**: the parsed FADT, MADT, MCFG and HPET fields of every machine must equal the decoded
  `[xxxh]` lines that ship with each dump: 90 real FADTs of twelve revisions, 1.0 to 6.5 (real-firmware §2.1),
  x2APIC-only MADTs and 224-entry ones among them.
- **Ring 0**: `tools/acpi-corpus.sh module <machine>` writes that machine's tables as one Multiboot module into
  `build/acpi-corpus/` (the kernel already reads modules, [boot_module.c L59](https://github.com/MasterLaplace/LplKernel/blob/76a2db35f5ea6ba37eafefc79332e2a812c0b212/kernel/arch/i386/boot/boot_module.c#L59)).
  A debug kernel booted with it runs a `KERNEL_TEST` that loads the tables through the simulated host and prints
  the same records as the host harness; without a module the test is skipped, and the CI job that passes one
  names it on `lpl.test.require`. Modules never enter a release or site ISO, which are published, so the
  attribution CC-BY-4.0 asks for never has to travel with them. From S3 on, the quick list runs this way, and
  the 64-bit integer paths through libgcc, the 4-byte-pointer layouts and the arena limits inside the 512 MiB
  window meet real AML in ring 0, which QEMU's revision-1 tables never give them.
- **Census oracle**: per-table declaration counts must equal those of `aml_census.py` and of the `iasl`
  disassembly shipped with each dump (`While` and `Name` plus the disassembler's `Switch` count, real-firmware
  §1.3).
- **ACPICA oracle**: `acpixtract -a` splits the same dump; `acpiexec` loads it with the same `-fv` fill as our
  simulated regions and the same `_OSI` answers (its list equals ours up to "Windows 2022"); both namespace dumps
  are normalised to `path type value` lines in declaration order and compared, after dropping the nodes ACPICA
  creates for `External` (§5.1).
- **`Load` on the corpus**: dumps hold the dynamic SSDTs but zero every address (real-firmware §1.1). The harness
  finds the packages of (8-character OEM table ID, address, length) triples that Intel's `PmRef` code reads (the
  `SSDT` package of `needs.md` N10), places each matching table at that address in simulated memory and leaves it
  out of the static load; `acpiexec` gets those tables statically. The two namespaces are then compared as sorted
  path sets. Values firmware writes to NVS at boot (`CFGD`, `PF00`) are absent from dumps; a preset file in
  `acpiexec`'s `-fi` format ("`<path> <value>`", [aeinitfile.c L66-L75](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/tools/acpiexec/aeinitfile.c#L66-L75)) feeds both oracles the same values, so the `_CST` branches run.

### 8.5 uACPI's test cases (MIT)

- 105 ASL files at [c0edece/tests/test-cases](https://github.com/uACPI/uACPI/tree/c0edecef62bcac6ce3f783540099190bafadadda/tests/test-cases),
  fetched by commit, compiled with `iasl` in the sandbox, never committed.
- Each starts with `// Name:` and `// Expect: <int|str> => <value>` ([run_tests.py L177-L186](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/tests/run_tests.py#L177-L186));
  the harness evaluates `\MAIN` and compares, speaking the same protocol as rust-osdev's `aml-tester`
  ([main.rs L84-L118](https://github.com/rust-osdev/acpi/blob/a69a4dfc3ec55c582901565aef1da7a2a4f623e0/tools/aml-tester/src/main.rs#L84-L118)).
- `check-*` expectations select uACPI API tests ([test_runner.c L35-L63](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/tests/runner/test_runner.c#L35-L63)): skipped and listed.
- Cases where this spec chose differently from uACPI (implicit return 0, mixed-type comparison, no `Sleep` cap,
  `Notify` on a SuperName) are listed in the harness as expected divergences, each with its section here. Cases
  that need an opcode outside the needs-path closure are listed as skipped, with the opcode.
- ASLTS (BSD-3, 1,288 `.asl` files) is a later option: its exception tests need `acpiexec`'s `\_ERR` protocol and
  ACPICA's error codes ([aeexception.c L96-L147](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/tools/acpiexec/aeexception.c#L96-L147)).

### 8.6 ACPICA on the host

From acpica.md §7.3; to run in a sandbox from a clean checkout at `e85aa3b`, never from the shared clone, never
yet run here.

```sh
make iasl acpiexec acpixtract                  # binaries in generate/unix/bin/

acpixtract -a dump.txt                         # acpidump text -> one .dat per table
acpiexec -l -di -b "namespace" dsdt.dat ssdt*.dat          # load only: no _STA/_INI
acpiexec -b "namespace" dsdt.dat ssdt*.dat                 # after _REG/_STA/_INI
acpiexec -dr -fv 0 -b "evaluate \_SB.PCI0._STA" dsdt.dat ssdt*.dat   # raw results, zero-filled regions
acpiexec -dr -fi presets.txt -b "evaluate \_PR.CPU0._CST" dsdt.dat ssdt*.dat        # NVS presets
acpiexec -es ...                               # slack mode: implicit return, uninitialised locals = 0
acpiexec -to 5 -te ...                         # loop timeout 5 s, exit the loop instead of aborting
iasl -oa -r 1 test.asl                         # no AML optimisation; revision 1 = 32-bit integers
iasl -e ssdt*.dat -d dsdt.dat                  # disassemble with external argument counts
```

Running with and without `-es` on the corpus answers Q4 and Q5: a needs-path result that differs between the two
is a machine that relies on slack behaviour. `Timer` and Notify threads make `acpiexec` traces non-deterministic
(acpica.md §7.3); methods that read `Timer` are compared on status only.

### 8.7 The parity record

Host and ring 0 run the same made-up tables and the same corpus bytes through the same simulated host (§3.3)
and print the same records, which `tools/parity.sh` compares (`# <suite>.<test>: key=value`, as the harness of
LplPlugin, LplAssistant and LplKnowledge does). Each record header prints the target parameters both sides used
(physical address width, profile caps), so that a region above 4 GiB or a probe count that depends on the hash
capacity differs only when the parameters do. QEMU live boots are not folded: they read emulated hardware that
the simulated host does not model, so they report statuses and counters only. FNV-1a, 32 bits, offset
`0x811C9DC5`, prime `0x01000193`, over little-endian bytes:

| Record | Folds, in order |
|---|---|
| `namespace` | a depth-first walk in declaration order; per node: depth `u8`, name `u32`, type `u8`, then Integer `u64`; String or Buffer `u32` length and bytes; Package `u32` count and each element the same way (a lazy name as its path bytes); Method flags `u8` and body length `u32`; Region space `u8`, offset `u64`, length `u64`; field unit kind, bit offset `u32`, bit length `u32`, access, update `u8`; Processor id `u8`, block address `u32`, block length `u8` |
| `evaluations` | per evaluated path in a fixed list: status `u16`, then the result as above |
| `work` | opcodes executed per opcode (`u32` each, in descriptor order), lookups, probes, allocations, bytes decoded, map calls |

Never folded: node indices, addresses, arena occupancy, page counts, anything `size_t` (CONTRIBUTING). Controls
that must come out different, each a test: the same table under the other integer width; the `_OSI` ladder with
"Linux" answered TRUE; a `_CST` table with one latency changed by one.

### 8.8 Real machines

- Boot a release server and a debug kernel on each class of available hardware (desktop, notebook, mini PC,
  server); print `[LPLTLM] acpi` and the per-CPU `_CST` table. Most recent PCs boot UEFI only and may have no RSDP
  below 1 MiB (#279), so this step needs N1 (#278, #279) first (§9, S0).
- Watch what changes once the platform is in ACPI mode (§3.8 step 0): what firmware stops doing in SMM after the
  switch, besides the power button, is not established by the pinned sources.
- Under Linux on the same machine take `acpidump`; run it through the host harness and through `acpiexec`; the
  `_CST` packages must match ours in structure (state count, register kinds, types). Latency and power come from
  NVS values a dump does not hold, so they are compared with what the running machine reports (next item).
- #298's criterion compares with `/sys/devices/system/cpu/cpu0/cpuidle`; that comparison means something only
  when Linux's idle driver took its states from ACPI, which `/sys/devices/system/cpu/cpuidle/current_driver`
  says.
- The machines are named in no public document.

### 8.9 Failure first

Each slice lists mutants: a deliberately wrong build that its tests must turn red before the slice is trusted
(CONTRIBUTING: "a control that must come out different"; `needs.md` §7: a check seen red before it is trusted). A mutant that stays green is a test
to write, not a pass.


---

## 9. Slices

Each slice is one pull request or more, each with its sub-issue of #57: S2 is #532, #533 and #534; S3 is #535, #536
and #537; S4 is #538, #539 and #540; S5 is #541 and #542; S6 is #543, #544, #545 and #546; S7 closes #57. Three
parsers listed in S2 move to their first reader, since no capability ships without a caller: the normalised FADT
and the ACPI-mode switch to #543, MCFG to #58, HPET to #60 or #298. Each is proven on both targets: tests in ring 0, the same tests on the
host, the parity records compared, the mutants seen red, the counts recorded. "Corpus" means the host harness on
the lists of §8.4 and, from S3, the ring-0 corpus test on the quick list.

### S0. Prerequisites and decisions outside the interpreter

| Item | Why | Done when |
|---|---|---|
| #528: on machines with more than 256 MiB of RAM (seen in the 512 MiB run), windows mapped before the PMM's second pass read other bytes | the namespace keeps firmware windows for the whole boot (`needs.md` N10) | #528 closed with its validation (#529) |
| New issue: the MADT ignores x2APIC entries and stops at 32 CPUs | 3 of 21 Intel clients (2024) list CPUs only as x2APIC; 7 servers exceed 32 (`needs.md` N3); `_CST` binds through it | opened: #534 |
| N1: #278 (Multiboot2 ACPI tags), #279 (EFI system table) | UEFI-only machines may have no RSDP below 1 MiB; S7's real-machine step and long mode (#96) depend on it | both closed before S7's real-machine step |
| #58: the ECAM accessor | 117 PCI_Config regions on 11 of 42 clients reach past offset 0xFF (§4.4) | before S5 closes; until then those accesses fail with `address_unreachable`, counted |
| N14 (#59): PCI INTx through the I/O APIC | the condition for calling `\_PIC(1)` (§3.8 step 5) | stated in #59 |
| #273 (the ACPI worker), #272 (the request ring) | evaluation after boot: N13's `_PPC` on Notify 0x80, N16's `_PTS(5)` at shutdown, later `_CST` changes | listed as prerequisites of "after #57" |
| Comment on #57: the corrections of §1.3 | a decision goes into an issue | [posted](https://github.com/MasterLaplace/LplKernel/issues/57#issuecomment-6376041081) |
| Comment on #298 (`needs.md` §8.3 items 3 and 5): `_CST` carries no target residency, so #298 names its source (a rule per profile, a table per model, or AMD's per-core `_LPI` read only for it); and the satellite's deepest hint has no ARAT check, which N6's HPET or ARAT must guard | decisions `needs.md` raised that belong to the consumer | [posted](https://github.com/MasterLaplace/LplKernel/issues/298#issuecomment-6376041450) |
| New issue (`needs.md` §8.3 item 6): battery, AC, thermal and lid, which the project's battery clients imply | no issue holds N19-N21 | opened: #548, after the event core of #547 |
| Comment on #295 (`needs.md` §8.3 item 7): 10 of 42 recent clients have S0ix and no S3, so "enters S3" cannot be met on them | the criterion needs an alternative | [posted](https://github.com/MasterLaplace/LplKernel/issues/295#issuecomment-6376041813) |
| Comment on #57 for slice 1 (`needs.md` §8.3 item 8): the directory is at 80 % of its 64 entries on the densest dumps (51); dynamic tables go to the loaded-table registry instead (§3.4) | capacity | posted |

### S1. The table directory (#530)

- Amendment, made in #530: prefer the 32-bit `FIRMWARE_CTRL` when non-zero (§1.3 item 4, Q9). Test: a made-up FADT
  with both FACS fields set to different tables; the 32-bit one is chosen. Mutant: prefer `X_FIRMWARE_CTRL`
  again; the test turns red.
- Amendment, made in #530: the lookup hands a DSDT or SSDT whose checksum fails, but whose header and length are sane,
  to the loader, with its `checksum_valid` flag (§5.1); the static tables follow Q20. Test: a made-up SSDT holding
  a `_CST`, with a wrong checksum, is found and counted. Mutant: refuse the table; the made-up `_CST` disappears.

### S2. The static tables, arch-free, and the host harness

| | |
|---|---|
| Goal | N3, N4, N5, N6 from architecture-free code: the normalised FADT, the complete MADT, MCFG, HPET, the ACPI-mode switch; checked on every corpus machine, not only on made-up and QEMU tables |
| Files | `kernel/kernel/acpi/tables.c`, `kernel/include/kernel/acpi/tables.h` (slice 1's directory walk moves here; `kernel/arch/i386/cpu/acpi.c` keeps the BIOS-area RSDP scan); `kernel/arch/i386/cpu/acpi_host.c` (`map`, `memory_type`, ports); `kernel/tests/acpi/simulated_host.c`, `kernel/tests/acpi/tables.c`; the host harness `tools/acpi/host/` and its xmake target (Q1); `tools/acpi-corpus.sh` |
| Content | FADT: revision recomputed from its length (uACPI's `fadt_ensure_correct_revision`, [tables.c L1364-L1398](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/tables.c#L1364-L1398); uACPI T13, [L1364-L1589](https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/tables.c#L1364-L1589)), a zeroed local copy of at most the known length so the reserved fields of a short FADT read 0 (ACPICA `AcpiTbCreateLocalFadt`, [tbfadt.c L388-L430](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/tables/tbfadt.c#L388-L430)), legacy blocks converted once to GAS (`AcpiTbConvertFadt`, [L479-L665](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/tables/tbfadt.c#L479-L665)) [code]; PM timer, RESET_REG, SCI_INT, SMI_CMD, ACPI_ENABLE, CST_CNT, PSTATE_CNT, PM1 and GPE blocks, sleep control and status registers, flags (`needs.md` N4). The ACPI-mode switch of §3.8 step 0 as a routine (SCI_EN read, enables cleared, ACPI_ENABLE written, bounded poll), called in S6. MADT types 0, 1, 2, 4, 5, 9, 0xA; a CPU slot only for an entry with Enabled set; Online Capable entries counted and unused, since there is no hotplug; an entry with both clear ignored, as "OSPM shall ignore the contents" ([§5.2.12.2 L3155-L3190][s05-madt-flags]; ACPICA [actbl2.h L2043-L2044](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/actbl2.h#L2043-L2044)); unknown types skipped by their length and counted (ACPICA marks type 16 revised in 6.6, [actbl2.h L1768](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/actbl2.h#L1768)); no CPU cap below the profile's; ACPI Processor UID kept per CPU slot. MCFG segments; HPET base |
| Tests | made-up FADTs of revisions 1 (116 bytes), 3, 5.0 with and without the hardware-reduced flag (5.0 is the most common real revision, 24/90, real-firmware §2.1), 6.5; the ACPI-mode switch against the simulated host (SCI_EN clear, then set; already set; SMI_CMD 0; hardware-reduced); an x2APIC-only MADT with 48 entries; a 224-CPU MADT; a MADT mixing enabled, disabled and online-capable entries with one unknown type; NMI entries; MCFG with two segments; QEMU live `q35` (MCFG present) and `pc` |
| Corpus | the parsed fields of the 114 machines equal their decoded `[xxxh]` lines (§8.4) |
| Mutants | x2APIC entries skipped; a disabled entry becomes a slot; the UID taken from the APIC ID; a revision-1 FADT read past its length; RESET_REG_SUP ignored; ACPI_ENABLE written without clearing the enables first |
| Done | every test green on both targets and on the corpus; the counts of the QEMU live boot printed; no interpreter code yet |

### S3. Decoder, namespace, declarations

| | |
|---|---|
| Goal | N10 without evaluation: every definition block decoded, every declaration in the namespace; the needs-path closure computed; #57's slice 3 |
| Files | `machine_language.h`, `machine_language_host.h`, `machine_language_decoder.c`, `machine_language_namespace.c`, `machine_language_load.c`, `machine_language_objects.c` (values only), `kernel/tests/acpi/decoder.c`, `kernel/tests/acpi/namespace.c`, `kernel/tests/acpi/corpus.c` (the ring-0 corpus test); the closure walker in `tools/acpi/host/` |
| Content | the decoder of §4.2 for every opcode; PkgLength bounds at decode; NameString and the upward search; the hash and the node arena; declarations of every named-object opcode (§4.3 groups 1-2); all table-level code (`If`, `Else`, `Noop`, stores, calls) counted (`deferred_table_code`) and its construct skipped; region and field operands that are not constants recorded as pending; the static closure of the needs path (§4.1) printed per machine and in total |
| Tests | PkgLength of 1-4 bytes and crossing its parent; names `\`, `^^`, Dual, Multi with 255 segments, count 0; the upward search 0-6 levels; duplicates; an SSDT of 36 bytes; a bad checksum loaded and counted; `Noop` runs; orphan packages; `Processor`; all five field-list elements; both forms of `CondRefOf` and `Load`; hash insert and delete under collisions; the namespace fold of QEMU's live `q35` tables on both targets |
| Corpus | 0 decode errors on the 47 QEMU configurations and the 114 machines; per-table declaration counts equal `aml_census.py` and the `iasl` disassembly; the namespace dump equals `acpiexec -l -di` on QEMU's configurations and on every corpus machine without table-level code; the ring-0 corpus test folds equal to the host on the quick list; the closure table published |
| Mutants | the PkgLength end computed after its own bytes; the upward search applied to a Dual name; `CondRefOf`'s operand treated as a call (the corpus loses sync, decode errors > 0); deletion leaving a tombstone that breaks a later lookup; a NameSeg compared as a C string |
| Measured | W1 on `q35` (release) and on the three corpus machines (debug), declarations only; peak nodes, arena, hash load |
| Done | all above green |

### S4. The evaluator

| | |
|---|---|
| Goal | N10 complete for static tables, N11's first half: table-level code runs; methods evaluate; `_OSI`, `_OS`, `_REV` answer; over the opcodes of the needs-path closure only |
| Files | `machine_language_interpreter.c`, `machine_language_objects.c` (heap, references, conversions), `machine_language_services.c`, `kernel/tests/acpi/evaluation.c`, `kernel/tests/acpi/limits.c`, `kernel/tests/acpi/windows_identity.c` |
| Content | §3.6 in full; the S4 rows of §4.3 that S3's closure reaches, every other one failing with `unimplemented_operator`; §5.1-§5.2, §5.4, §5.5; the limits and faults of §6; the Notify ring; table-level calls and pending operands evaluated in order; a field read, which needs S5, fails with `field_access_deferred`, counted |
| Tests | one test per row of §5.2 and §6.3 whose opcode ships; the limits of §6.1 each hit once (a 65-deep recursion, an 86-deep `ElseIf` ladder passes and a 600-deep one fails, a 2^20-iteration loop, `ASRT`'s 60 s sleep refused before sleeping); the Intel-style package-of-buffers mutation; implicit return; 32-bit masking under a revision-1 DSDT; the `_OSI` ladder; a `Timer` poll loop ends the same way on both targets; W4 |
| Corpus | uACPI's 105 cases pass except the listed divergences and the cases that need an opcode outside the closure; the namespace after load equals `acpiexec -l -di` on the machines whose table-level code reads no field unit; on the others (at least 17 of 42 clients read NVS fields such as `OSYS` and `TBTS` at table level, `scripts/vc_table_level_if.py`), `field_access_deferred` is printed and the comparison waits for S5 |
| Mutants | implicit return gives nothing; `Ones` unmasked in 32-bit mode; a shift by 64 computed in C; division by zero not caught; `Index` on a package returns a copy; temporaries kept after `Return`; the time budget fed by the clock (parity fold differs between runs) |
| Measured | W1 again on the corpus machines, now with table-level code; W4; `allocations` = 0 for integer-only methods |
| Done | all above green and folded equal on both targets |

### S5. Regions and fields

| | |
|---|---|
| Goal | N11's second half and N12's prerequisite: AML reaches memory, I/O ports and PCI configuration space; the needs path runs end to end on the corpus |
| Files | `machine_language_regions.c`, `acpi_host.c` (windows, ports, PCI), the 64-bit `asmutils` routine, `kernel/tests/acpi/regions.c`, `kernel/tests/acpi/fields.c`; a cached read-write firmware window in `vmm` (Q16) |
| Content | §4.4 and §5.3: attach, the per-page mapping cache with `memory_type`, the field engine with its fast path, the 64-bit split on i686, IndexField, BankField, port and PCI-header protection, PCI offsets from 0x100 refused until #58, `_REG(PCI_Config, 1)`, Serialized and mutex rules exercised against real accesses; `Lock` fields and `\_GL` failing with `global_lock_unavailable`; EC accesses counted |
| Tests | the stratified field sample in ring 0 and the bit-exhaustive suite on the host (§8.2); every access width and update rule; an odd-length region read with WordAcc; a `QWordAcc` field on i686 written low then high; a 16 MiB region accessed at its last dword maps one page; IndexField and BankField over a test page; a protected port read and write; a PCI header write; a PCI_Config access at offset 0x100 fails with `address_unreachable`; a region above 4 GiB on i686; a `Lock` field access fails with `global_lock_unavailable` |
| Corpus | the needs-path run (§3.8 steps 0-9, with `Load` placed as in §8.4) with regions simulated, on the host and in ring 0; its executed-opcode table, which re-measures the subset (§4.1 item 5); the namespace equality S4 deferred; per machine, the counts of `space_unsupported`, `global_lock_unavailable`, EC accesses and `address_unreachable` on the needs path |
| Mutants | AnyAcc as dword; Preserve behaving as WriteAsZeros; the index register written after the data; protection disabled; a 64-bit base truncated on i686; a `Lock` field accessed without the lock and the counter left at 0; a 64-bit datum written high then low |
| Decision point | if any needs-path run on the corpus counts `global_lock_unavailable`, the FACS global lock (acquire with a bounded spin, pending bit, release, through an `asmutils` compare-exchange) moves into S6; otherwise it waits for N18. EC accesses on the needs path bring the EC forward under the same rule |
| Done | all green |

### S6. Initialisation, handshake, dynamic tables

| | |
|---|---|
| Goal | N11 complete and N10's dynamic half: the sequence of §3.8 steps 0-7, `Load`, `LoadTable` |
| Files | `initialization.c`, `machine_language_load.c` (`Load`, `LoadTable`, the loaded-table registry), `kernel/tests/acpi/initialization.c`, `kernel/tests/acpi/dynamic_tables.c` |
| Content | the ACPI-mode switch (step 0); `\_INI`, `\_SB._INI`, `_REG`, the `_STA`/`_INI` walk with the pre-scan; `\_PIC` not called, counted (step 5); platform `_OSC` query then commit with Q19's bits; processor `_OSC` or `_PDC` with the capability bits of Q7; `Load` from a region or a field, copied and checked, duplicates refused, objects relative to the root (ACPICA, [exconfig.c L655-L668](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exconfig.c#L655-L668)); `LoadTable` by signature and OEM identifiers ([§19.6.77][s19-loadtable]) with empty RootPath, ParameterPath and ParameterData only, run in place from the firmware window; the `_REG` and `_STA`/`_INI` of a table loaded at run time; the failure table of §3.8 |
| Tests | one per row of §3.8's failure table, among them a `_STA` that divides by zero; the made-up `_OSI` ladder sets its OS year to 2015, and to 1000 under the "Linux" control; a `GCAP`-style `_OSC` loads a table from a test page only when bit 6 is granted, and loading it twice returns False; a loaded table's PCI_Config `_REG` runs once its loading evaluation ends; `LoadTable` of a made-up `OEM1` runs in place; `LoadTable` with a non-empty RootPath and `Load` from a Buffer fail with `load_form_unsupported`; the `_STA` cases (absent, present, functioning only, neither); `_INI` order traced and folded; a made-up `_PIC` that rewrites "PIRQ" fields is never called; the ACPI-mode switch happens before the first `_INI` |
| Corpus | on every machine with `Load` (55/90), the dynamic SSDTs load through `_OSC` with the triple placement of §8.4; the namespace then equals `acpiexec` given those tables statically, as sorted path sets; the sequence completes on the 114 machines, on the host and, for the quick list, in ring 0 |
| Mutants | `_OSI ("Linux")` answered TRUE; `_REG` run before `\_SB._INI`; a duplicate `Load` accepted; "present but not functioning" children skipped; no ACPI-mode switch; `\_PIC(1)` called while the 8259 path is active; platform `_OSC` claiming bit 6 |
| Measured | W2 recorded, on `q35` (release) and the three corpus machines (debug) |
| Done | all green; on QEMU live boots the sequence completes with zero failures counted; in ring 0, the corpus test completes it on the quick list with the counts the host prints |

### S7. `_CST` for every CPU (closes #57)

| | |
|---|---|
| Goal | N12: #57's done criterion, "evaluates `_CST` for every CPU, matching acpidump" |
| Files | `resources.c` (zero-copy iterator: Generic Register, EndTag, skip by length; ACPICA's validation, [utresrc.c L154-L298](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utresrc.c#L154-L298), and its EndTag tolerance, [L283-L297](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utresrc.c#L283-L297)), `processor_idle.c`, `kernel/tests/acpi/processor_idle.c` |
| Content | CST_CNT written to SMI_CMD when non-zero, after the ACPI-mode switch (§3.8 step 8; 8/20 servers, `needs.md` N4, N12); `_CST` per processor object ([§8.4.1.1][s08-cst]): count, then per state the Register descriptor ([§6.4.3.7][s06-gas]; FFH with the MWAIT hint, SystemIO port read, SystemMemory), type, latency, power; binding to the CPU slot of an Enabled MADT entry through its ACPI Processor UID, `_UID` of an `ACPI0007` device or the `ProcessorID` of `Processor` ([§8.4 L444-L452](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/08_Processor_Configuration_and_Control.rst?plain=1#L444-L452)); the Notify ring drained (§3.8 step 9); the table published per CPU; one `[LPLTLM] acpi.cst` line per CPU |
| Tests | the Intel-style and AMD-style made-up processors; a `_CST` returning a malformed descriptor; an unbound processor counted; a processor whose MADT entry is disabled gets no table; a Notify 0x81 queued after step 8 re-evaluates that processor's `_CST`; W3 |
| Corpus | on every machine with `_CST` (79/90, real-firmware §2.7, and the others of the 114), our packages equal `acpiexec`'s with the same presets |
| Real machines | §8.8, once #278 and #279 have landed |
| Mutants | bit width and bit offset of the Register descriptor swapped; latency taken from the power element; UID binding by position; CST_CNT not written; CST_CNT written before the ACPI-mode switch |
| Done | #57's criterion met; the W3 budget recorded; the consumer (#298) is a separate pull request |

### After #57

In the order of `needs.md` §8.1-§8.2, each after #272 and #273 when it evaluates after boot:
`_PSS`/`_PCT`/`_PPC`/`_PSD`/`_CPC` (N13, #302), with a second platform `_OSC` claiming the CPPC bits (Q19);
`\_S5` with `_PTS(5)` and the reset register (N16); `_PRT` with link devices and the IRQ descriptors, and
`\_PIC(1)` with it (N14, #59); then the event core, the EC and the global lock (N17, N18) when a notebook target or
the server power button asks. Each adds the closure of its entry points (§4.1), reuses the layers above and adds
its own slice with the same table.

---

## 10. Open questions, and their answers

Answered on 2026-10-24: every recommendation below stands.

| # | Question | Options | Recommendation |
|---|---|---|---|
| Q1 | Where does the host harness live? | (a) an xmake target in LplKernel (`test-acpi`) compiling `kernel/kernel/acpi/*.c` and the simulated host with a host shim of `test.h`; (b) a new sibling repository | (a), from S2: the code is kernel code, `tools/parity.sh` already takes extra host logs, and `tools/unbuilt-branches.declared` sees the target |
| Q2 | How long is the identifier prefix? | (a) externs `advanced_configuration_and_power_interface_machine_language_…`, statics short as slice 1 does; (b) a module word without acronym (`firmware_bytecode_…`) | (a): consistent with slice 1 and the naming rule; statics stay readable |
| Q3 | `Sleep` longer than 2 s | (a) honour it within the evaluation's 30 s budget; (b) cap at 2 s like ACPICA and uACPI | (a): firmware asks for 3 s on 11/39 sampled machines, and what NT does is not established by these sources; the boot cost is counted |
| Q4 | Implicit return | (a) Integer 0, counted; (b) the last result, as ACPICA's slack mode | (a), then run the corpus with `acpiexec` and `acpiexec -es`: a needs-path difference brings (b) back |
| Q5 | Reading an uninitialised `LocalX`/`ArgX` | (a) Integer 0, counted; (b) an error | (a) with the counter; the same `-es` comparison decides |
| Q6 | Where AML runs after boot on a one-core machine (some satellites) | (a) between ticks, in the idle slack, with a budget per slot; (b) only at boot and at shutdown | (b) until N17 brings events; (a) needs a measured per-slot cost |
| Q7 | The processor `_OSC` capability bits (UUID `4077A616-…`) | (a) set the bits the corpus gates on (`PF00 & 0x18` for `_CST`, `0x200` for MWAIT hints, `(PF00 & 0x09) == 0x09` for P-states, `needs.md` N11); (b) pin Intel's vendor document first | (b) then (a): the bits' meaning is [claimed] until Intel's document is pinned |
| Q8 | Type 3 C-states (cache flush or ARB_DIS, [§8.1.4 L240-L308](https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/08_Processor_Configuration_and_Control.rst?plain=1#L240-L308)) | (a) publish them, let #298 decide; (b) publish types 1-2 only | (a): the interpreter reports what firmware says; the entry policy belongs to #298, which `needs.md` §8.3 already flags |
| Q9 | The FACS address | (a) 32-bit `FIRMWARE_CTRL` when non-zero (ACPICA, uACPI, Linux bug 74021); (b) keep slice 1's 64-bit first | (a) |
| Q10 | `_OSI ("Extended Address Space Descriptor")` | (a) FALSE: no decoder for that descriptor; (b) TRUE like ACPICA and uACPI | (a) until a resource consumer decodes it; 0 such descriptors in 90 machines (real-firmware §2.8) |
| Q11 | The limits of §6.1 | accept, or change any value | accept; each is a counter, the corpus runs report the peaks |
| Q12 | Test data under other licences | (a) fetch by pinned commit into `build/`, never committed, never in a published image; (b) vendor with notices | (a): QEMU's tables are GPL-2.0-or-later, linuxhw's dumps CC-BY-4.0; our own ASL stays MIT |
| Q13 | The global lock's timing | (a) with the EC (N18); (b) in S6 if the needs path counts `global_lock_unavailable` | the S5 decision point decides, on counts; until then the access fails loudly, never unlocked |
| Q14 | ACPI 6.6 numbering | re-check every section and line against 6.6 once uefi.org answers | do it before the spec is merged as a document |
| Q15 | Objects of `Load` relative to the root | (a) the root, as ACPICA does "for compatibility" ([exconfig.c L655-L668](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/executer/exconfig.c#L655-L668)); (b) the spec's reading | (a), checked against the corpus' `PmRef` tables, whose scopes are absolute |
| Q16 | Mapping attribute of SystemMemory regions | (a) cached read-write inside ACPI NVS or reclaim memory, as `memory_type` reports it, uncached elsewhere (needs a new firmware read-write window in `vmm`); (b) uncached everywhere | (a): an uncached alias of a page the firmware window maps cached is a memory-type conflict |
| Q17 | Mixed-type `LEqual`/`LLess`/`LGreater` | (a) convert the second operand (spec, ACPICA); (b) require the same type (uACPI) | (a); the `mixed_type_comparison` counter on the corpus shows whether it ever matters |
| Q18 | A release-kernel window for `_CST` on QEMU | (a) pass a made-up SSDT with `-acpitable`; (b) measure in a debug kernel only | try (a); whether QEMU 10.2.1 adds such a table where the kernel finds it is unverified (specification report §7) |
| Q19 | The platform `_OSC` DWORD-2 bits ([§6.2.11.2][s06-osc-platform]) at S6 | (a) claim none: bit 0 Processor Aggregator, 1 `_PPC` `_OST`, 2 `_PR3`, 3 insertion `_OST`, 4 APEI, 5 CPPC, 6 CPPC 2, 7-8 low-power idle, 9 `_TFP`, 10 more than 16 P-states, 11 GED, 12 diverse CPPC highest, 13 Interrupt ResourceSource, 14 flexible CPPC registers, 15 GHES_ASSIST, 16 multi-PCC CPPC, 17 Generic Initiator, 18 USB4, 19 battery charge limiting, 20 PCI BAR target, 21 PRM, 22 FFixedHW regions: the kernel exercises none of them at S6; (b) claim CPPC (5, 6, 10) now so that Intel's `GCAP` loads its HWP table | (a). Claiming a control the kernel does not exercise hands it nothing it can use; N13 (#302) claims 5, 6 and 10 in a second evaluation. The spec allows the OS to evaluate `_OSC` "an arbitrary number of times" for host bridges ([§6.2.11.1.3 L2998-L3001][s06-osc-sequence]) and says nothing for the platform object, so whether firmware loads its HWP table on a second call is checked on the corpus when N13 starts; if it does not, N13 forces the decision at S6 |
| Q20 | A static table (FADT, MADT, MCFG, HPET) whose checksum fails | (a) parse it and count `checksum_bad_parsed`, as both references do by default (§5.1); (b) refuse it, as slice 1 does now | (a): a refused FADT or MADT leaves the machine without ACPI or without its other CPUs, and every field is already bounds-checked against the table's length; the counter shows how often it happens on the corpus |

---

## 11. Credits

| Source | Licence | What this spec learned from it |
|---|---|---|
| **uACPI**, Daniil Tatianin and contributors, [c0edece](https://github.com/uACPI/uACPI/tree/c0edecef62bcac6ce3f783540099190bafadadda) (7.0.0) | MIT | table load as a frame; explicit frames for calls and `Load`; the opcode table; lazy package names; lazy region attach and orphan `_REG`; the `_REG` spaces the spec exempts; PCI addresses through bridges; NT's port and PCI-header protection; the catalogue of NT behaviours with tests (store rules, implicit cast, AnyAcc, chunked field stores, `LAnd` on 4 bytes, `Unload` refused); the cycle check at store time; the saturating count; the ACPI-mode switch before the namespace loads; a bad checksum warned and loaded; the test format and its 105 cases; its own TODOs, which pointed at decode-time bounds |
| **ACPICA**, © 1999-2026 Intel Corporation and contributors ([LICENSE.BSD-3-Clause L1](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/LICENSE.BSD-3-Clause#L1)), [e85aa3b](https://github.com/acpica/acpica/tree/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf) (20260930) | BSD-3-Clause or GPL-2.0-only (14 files keep the older Intel licence) | two direct-indexed decode tables; packed argument kinds; single-pass load running module-level code; module-level error tolerance; method bodies skipped at load; integer width from the DSDT; the `_OSI`/`_OS`/`_REV` policy and its history; the ACPI-mode switch and its bound; the initialisation order and its tolerance of `_STA` and `_INI` failures; the field datum engine; zero-copy literals (candidate C5); the `_INI` pre-scan; the FACS preference; a bad checksum warned and loaded; `acpiexec` and `iasl` as oracles; ASLTS; and, by its defects, the 64-bit size rule and the read-only table rule |
| **LAI**, the lai authors (managarm), [b481a42](https://github.com/managarm/lai/tree/b481a42d2af53d9bd7d41195d354bb703b40ab07) | MIT | integers by value and inline stacks; bodies run in place; operand modes as a table; the PC committed per whole opcode; `_OSI` as a native node; per-method object lists; and, by its panics, the rule that AML never stops the kernel |
| **rust-osdev/acpi**, Isaac Woods and contributors, [a69a4df](https://github.com/rust-osdev/acpi/tree/a69a4dfc3ec55c582901565aef1da7a2a4f623e0) | MIT or Apache-2.0 (`LICENCE-MIT`, `LICENCE-APACHE`) | a named bound with a test for every unbounded thing; the indirection limit for package names and its rationale; typed errors; raw-byte tests; a harness that speaks uACPI's protocol; the store-rule write-up; and, by its gaps, the list of opcodes a test suite must not skip |
| **ACPI Specification 6.5**, UEFI Forum, [UEFI/ACPI-Specification-Release@5dabe6f](https://github.com/UEFI/ACPI-Specification-Release/tree/5dabe6f37fbe753d90292a2a419105be5d55a778) | © UEFI Forum, published "for reference only"; quoted, never copied | the grammar, the semantics, the objects; 6.6 could not be read (HTTP 403) |
| **QEMU** 10.2.1, its contributors, [tests/data/acpi](https://github.com/qemu/qemu/tree/v10.2.1/tests/data/acpi) and `hw/i386/acpi-build.c` | GPL-2.0 for QEMU as a whole; `acpi-build.c` GPL-2.0-or-later; the test tables, which carry no notice, GPL-2.0-or-later ([LICENSE L1-L16](https://github.com/qemu/qemu/blob/v10.2.1/LICENSE#L1-L16)) | today's test bench: what it emits, and the long list of what it does not |
| **linuxhw/ACPI**, published by linuxhw from dumps that hw-probe users uploaded to linux-hardware.org, [d6d30c6](https://github.com/linuxhw/ACPI/tree/d6d30c6ef6f45506567f25a302c64f009d8f5f4e) | CC-BY-4.0 | 114 real machines: every prevalence figure in this spec. Changes made: parsed and counted only; no dump byte reproduced beyond short quotations in the reports and the line ranges cited here |
| **Intel's processor vendor-specific ACPI document** | not pinned | the meaning of the processor `_OSC` bits and of the FFH C-state descriptor: [claimed], Q7 |
| **Linux bug 74021**, as cited by ACPICA and uACPI | — | the FACS 32-bit preference (not read directly) |

---

## Review notes

Two checks were run on the first draft: one of every citation (66 problems, all applied), one of completeness and
coherence (38 problems in 34 items). This appendix records where the revision departs from a finding, and what it
added on its own.

**Findings not taken as written.**

- *Completeness item 8 (bad checksums).* It says ACPICA refuses such a table by default. It does not:
  `AcpiUtVerifyChecksum` warns and returns `AE_OK` unless `ACPI_CHECKSUM_ABORT`, which is `FALSE` by default
  ([utcksum.c L68-L81](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/components/utilities/utcksum.c#L68-L81),
  [acconfig.h L54-L60](https://github.com/acpica/acpica/blob/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf/source/include/acconfig.h#L54-L60)).
  The fix it asked for (load AML tables, count them, amend slice 1) is kept. Its proposal to refuse static tables
  became Q20, since neither reference refuses them.
- *Item 1 (ACPI mode): "poll SCI_EN with a bounded count, never timed".* A hardware wait has a floor in time, as
  the kernel's own deadline API says (irq.h L68-L74), and the switch is not on a compared path. The poll is
  bounded by ACPICA's 3,000 × 1 ms and its polls are counted.
- *Item 6 (S4 needs fields).* Of its two fixes, the second was taken: S4's corpus criterion is restated, and the
  needs-path run moves to S5. Pulling the SystemMemory datum engine into S4 would have grown the slice that item
  18 already found too large.
- *Item 14 (SRAT).* `numa_policy` already reads the SRAT through slice 1's directory, x2APIC affinity (type 2)
  included, so S2 does not move it.
- *Item 23 (tables loaded at run time).* It asked for `_REG` "of the three spaces"; the revision runs only
  PCI_Config's, after the finding below.
- *Item 34 (presence as a tie-breaker).* The static closure is a superset of what runs, so no tie arises; presence
  only orders the work inside S4.
- *Item 3 (feeding corpus bytes to ring 0).* Of "a Multiboot module or a generated object", the module was
  chosen: the kernel already reads modules, and nothing generated from CC-BY-4.0 data is linked into an image.

**Added during the revision.**

- *`_REG` for SystemMemory and SystemIO.* The draft cited [§6.5.4][s06-reg] for running them; that section
  exempts them ([L8188-L8201][s06-reg-exceptions]). uACPI follows the exemption, ACPICA does not. Measured on the
  42 clients: 1 `_REG` of 416 tests SystemMemory, none SystemIO, 359 test PCI_Config
  (`scripts/vr_reg_spaces.py`, output `scripts/vr_reg_spaces.txt`). The spec now runs PCI_Config's only (§1.4,
  §3.8 step 3).
- *Windows renumbered.* The old W3 (`_PRT` on `q35`) was dropped (completeness item 26); W4 and W5 became W3
  and W4. Citation finding A13 (QEMU's `_PRT` reads `PICF`, not `PICM`) is resolved by that removal: both
  sentences it corrected are gone.
- *The verification scripts* are in `scripts/verify/` (citations) and `scripts/vc_*.py`,
  `scripts/verify_completeness_probe.py` (completeness); the revision's own checks are
  `scripts/check_permalinks_final.py` (every line-anchored permalink lands inside its file at the pinned commit)
  and `scripts/check_tables.py` (every table row has its header's cell count). Each runs with `python3 -I`.

---

## Reference links

ACPI 6.5 source at `5dabe6f` (UEFI Forum):

[s03-hwreduced]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/03_ACPI_Concepts.rst?plain=1#L1346-L1350
[s04-override]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/04_ACPI_Hardware_Specification.rst?plain=1#L1425-L1433
[s05-smi-cmd]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L1302-L1311
[s05-cst-cnt]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L1402-L1405
[s05-madt-flags]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L3155-L3190
[s05-collision]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L13799
[s05-create]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L13866-L13876
[s05-gl-mutex]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L17878-L17886
[s06-osc-sequence]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/06_Device_Configuration.rst?plain=1#L2998-L3001
[s06-ini-load]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/06_Device_Configuration.rst?plain=1#L8015
[s06-reg-exceptions]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/06_Device_Configuration.rst?plain=1#L8188-L8201
[s19-field-wordacc]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L6829-L6831
[s20-data]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L342-L359
[s05-pkglen]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L13995-L14029
[s05-search]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L13822-L13852
[s05-rootns]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L13902-L13955
[s05-firstmethod]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L14043-L14044
[s05-dblevel]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L14085-L14140
[s05-args]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L14244-L14271
[s05-callconv]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L14272-L14322
[s05-locals]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L14323-L14369
[s05-spaces]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L14395-L14470
[s05-gl]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L2371-L2507
[s05-osi]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L17891-L18090
[s05-osi-windows]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L17919-L17926
[s05-rev]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/05_ACPI_Software_Programming_Model.rst?plain=1#L18110-L18134
[s06-osc-rules]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/06_Device_Configuration.rst?plain=1#L2949-L3015
[s06-osc-platform]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/06_Device_Configuration.rst?plain=1#L3016-L3126
[s06-sta]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/06_Device_Configuration.rst?plain=1#L4505-L4571
[s06-gas]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/06_Device_Configuration.rst?plain=1#L6503-L6602
[s06-ini]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/06_Device_Configuration.rst?plain=1#L8010-L8089
[s06-reg]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/06_Device_Configuration.rst?plain=1#L8148-L8266
[s08-cst]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/08_Processor_Configuration_and_Control.rst?plain=1#L511-L669
[s19-pkg255]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L3290-L3292
[s19-source-conv]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L3433-L3492
[s19-conv-detail]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L3606-L3685
[s19-store-rules]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L3686-L3964
[s19-acquire]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L4767-L4796
[s19-intwidth]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L5709-L5715
[s19-external]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L6731-L6792
[s19-field]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L6813-L7002
[s19-field-lock]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L6836-L6842
[s19-loadtable]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L8277-L8341
[s19-method-serialized]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L8690-L8696
[s19-mutex-rules]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L8845-L8863
[s19-return]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L10679-L10702
[s19-sleep]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L10879-L10898
[s19-stall]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L10979-L10999
[s19-timer]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L11283-L11324
[s19-while]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/19_ASL_Reference.rst?plain=1#L11965-L11989
[s20-names]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L181-L275
[s20-namestring]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L223-L275
[s20-pkglen]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L396-L428
[s20-terms]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L430-L458
[s20-named]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L494-L827
[s20-condrefof]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1084-L1090
[s20-derefof]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1108-L1120
[s20-load]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1260-L1266
[s20-package]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1378-L1410
[s20-table]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1617-L2321
[s20-condrefof-row]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1778-L1782
[s20-load-row]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1793-L1797
[s20-unload-reserved]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1843-L1847
[s20-processor-reserved]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L1883-L1887
[s20-lnot-rows]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/20_AML_Specification.rst?plain=1#L2188-L2207
[sC-pdc]: https://github.com/UEFI/ACPI-Specification-Release/blob/5dabe6f37fbe753d90292a2a419105be5d55a778/source/Apx_C_Deprecated_Content.rst?plain=1#L53-L101
