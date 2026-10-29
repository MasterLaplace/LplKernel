# LplKernel's ACPI layer and AML interpreter: specification

This document specifies LplKernel's ACPI layer and its AML interpreter: what they read, what they execute, how they
fail, how fast they must be and how they are tested. The work is tracked in #57 and its sub-issues.

**In one sentence.** LplKernel has its own AML interpreter, written from scratch. It executes the firmware's bytes
in place from a read-only window, on fixed explicit stacks, with integers held by value and a hashed namespace of
32-bit indices. It implements what real firmware needs on its path to `_CST`, `_OSI`, `_INI`, `_OSC` and `Load`
included, and fails loudly and counts everything else. It is judged by instruction counts and by differential
oracles.

**Reference.** The ACPI Specification 6.6 (UEFI Forum, May 2025). Every `§` below is a section of that release and
links to its page in the PDF.

---

## 1. Scope

### 1.1 Principles

1. **From scratch.** No line of another implementation is copied or ported. uACPI, ACPICA, LAI and rust-osdev/acpi
   are differential oracles on the host (§8), and §9 credits the ideas taken from them.
2. **Real firmware is the target.** The interpreter is written for the firmware of modern desktops, notebooks,
   servers, mini PCs and tablets, firmware written and tested against Windows. The corpus of §8.4, 114 real
   machines, decides what is implemented. QEMU is a test bench and never decides alone.
3. **Only what the kernel needs.** Every opcode is decoded, so the decoder never loses its place. Only the opcodes
   on the needs path are executed (§4.1); everything else fails loudly and is counted.
4. **Modern, with the legacy real machines still carry:** the RSDT and the revision-0 RSDP, the 116-byte FADT of
   revision 1, the deprecated `Processor` opcode, 32-bit integers, and `_PDC` as a fallback.
5. **Measured, never timed.** Speed is judged by instruction counts in ring 0 (`tools/instructions.sh`) and by
   deterministic counters: allocations, opcodes executed, lookups.
6. **AML never stops the kernel.** No panic, trap or undefined behaviour comes from AML (§6.2).

### 1.2 What it delivers

| Need | What the ACPI layer delivers | Issues |
|---|---|---|
| N1 RSDP from every boot path | the pointer from Multiboot2, the EFI system table or PVH, the BIOS scan kept; outside the interpreter, and required before UEFI-only machines | #278, #279, #96, #458 |
| N2 table directory | the RSDT or XSDT, FADT, DSDT and FACS, through read-only windows | #530 |
| N3 complete MADT | x2APIC (type 9), NMI (types 4 and 0xA), no 32-CPU cap, the Enabled and Online Capable flags, the ACPI Processor UID kept | #534, #62, #258 |
| N4 FADT fixed hardware | PM timer, RESET_REG, SCI_INT, SMI_CMD, ACPI_ENABLE, CST_CNT, PSTATE_CNT, sleep registers, flags | #543, #60, #294 |
| N5, N6 MCFG, HPET | the ECAM bases; the second reference clock | #58, #60, #298 |
| N7 SRAT | read by `numa_policy` through the table directory; not part of this layer | #125 |
| N10 namespace | the DSDT and SSDTs, then `Load` and `LoadTable` at run time | #535 to #539, #546 |
| N11 firmware handshake | ACPI mode, `\_OSI`, `\_REV`, `_REG`, `_STA` and `_INI`, `\_SB._OSC`, processor `_OSC` or `_PDC` | #540 to #545 |
| N12 `_CST` for every CPU | entry register, type, latency and power, bound to a CPU slot | #57, #298, #135 |

The later needs reuse the same layers, each adding its own entry points to the needs path (§4.1): P-states and
CPPC (#302), PCI INTx routing (#59), reset and soft-off (#294), events and the embedded controller (#547), battery, AC, thermal and lid (#548), sleep states (#295).

### 1.3 The needs path

The needs path is the set of evaluations the kernel makes at boot, and every method they call:

- the table-level code of every definition block;
- `\_INI`, `\_SB._INI`, `_REG`, `_STA` and `_INI` (§3.8);
- `\_SB._OSC`, and processor `_OSC` or `_PDC`;
- `_CST`, and the `_UID` that binds a processor to its CPU (§3.10);
- `_ADR`, `_SEG`, `_BBN`, `_HID` and `_CID`, which a PCI_Config region evaluates as it attaches (§3.6);
- the tables that `Load` and `LoadTable` bring in.

It decides which opcodes are executed (§4.1). It is computed on every corpus machine twice: as a static closure,
and as a run (§8.4).

### 1.4 Out of scope

Each item is left out on purpose. "Brings it back" names what reverses the decision.

| Left out | Why | Brings it back |
|---|---|---|
| `DataTableRegion`, `Nor` and `Revision` execution | no corpus machine uses them | a needs path that runs one; each is decoded already |
| `Unload` execution | reserved by ACPI, and absent from modern firmware | a target that relies on it; decoded and refused meanwhile (§4.5) |
| The event core: SCI, fixed events, GPE `_Lxx` and `_Exx` | no need asks for it yet. Two costs follow: once §3.8 step 0 puts the platform in ACPI mode, only the four-second power button override remains ([§4.8.2.2.1.3]); and the published `_CST` is the one for the power source present at boot | #547; `Notify` is queued, drained once at boot (§3.8 step 9) and counted meanwhile |
| The embedded controller handler and `_Qxx` | `_REG(EmbeddedControl, 1)` is never called, so firmware keeps the guard flag its `_REG` sets and skips its own EC accesses, as it does before any OS driver starts | #547 and #548; or a run on the corpus that counts EC accesses, as the count of `Lock` fields brought in the global lock (#589) |
| Region spaces other than SystemMemory, SystemIO and PCI_Config | each needs the driver of the device behind the space | that driver: the EC first, CMOS for RTC wake, GenericSerialBus and GPIO for I2C input |
| `BufferAcc` protocols and `Connection()` execution | same | the same drivers; the field-list elements are decoded so the parser never loses its place |
| Repair of return values | results stay raw, so they compare with `acpiexec -dr` | a consumer that meets a broken object repairs it itself |
| Implicit return of the last value | §5.2 returns Integer 0 | a needs-path result that differs between `acpiexec` and `acpiexec -es` (§8.6) |
| Auto-serialisation of methods that create names | two evaluations never interleave (§3.7) | concurrent AML execution |
| PSDT and OSDT loading | absent from the corpus | a machine that ships one |
| `LoadTable` with a RootPath, ParameterPath or ParameterData, `Load` from a Buffer, either run while a table is declared | every call site on the corpus passes `""`, `""`, `Zero`, and loads from a region or a field, in a method; another form fails with `load_form_unsupported` | a needs-path call site with another form |
| `_REG` for SystemMemory and SystemIO | ACPI declares both always available, and OSPM runs no `_REG` for them ([§6.5.4]) | a needs-path `_REG` that tests either space |
| Opcodes outside the needs path | not reached on the path to `_CST` | the needs path of a later need, or a counted `unimplemented_operator` |
| T-states, S1 and S2, `_GTS`, `_BFS`, `_TTS`, hotplug, WMI, video extensions, APEI, active cooling | no need asks for them | the issue that names one |
| A parse tree, method-body copies, a disassembler or a debugger in ring 0 | execution runs from the bytes (§3.6) | none |
| Resource conversion into native structures | a zero-copy iterator over the descriptors suffices (§3.10) | none |

---

## 2. Ground rules

| Rule | What it means here |
|---|---|
| Determinism | AML is integer-only and fixed-width, so an evaluation is a pure function of the AML bytes, the simulated regions, the `_OSI` answers, the clock source and the target's parameters (physical address width, profile caps). Its results fold equal on the host and on i686 when both run through the same simulated host (§8.7). Nothing ACPI reads is authoritative: a platform reading that ever steers the World enters as a journaled input. |
| No wall clock on a compared path | Loop and sleep budgets count work: iterations, and the milliseconds `Sleep` and `Stall` *request*, never elapsed time. In parity runs `Timer` is a virtual clock, advanced by those requests and by a fixed quantum of 100 µs on every read, so a loop that polls `Timer` ends the same way on both targets. A wait on hardware (the switch to ACPI mode, §3.8 step 0) is bounded in time and its polls counted; it is never on a compared path. |
| Fixed-width folds | Folds see `uint32_t` and `uint64_t` fields and node indices only: never a pointer, a `size_t` or an occupancy. |
| Integers | Every AML integer is a `uint64_t` on both targets, masked to 32 bits when the DSDT's revision is below 2 ([§19.6.29]). 64-bit division on i686 goes through libgcc. |
| No inline assembly | Port I/O is an `asmutils` routine; the global lock's compare-exchange is the compiler's atomic builtin, which the shared rules prefer to assembly; a 64-bit memory-mapped datum of i686 is two 32-bit accesses the field engine makes (§5.3). |
| Naming | Externs `advanced_configuration_and_power_interface_…` for the tables and `advanced_configuration_and_power_interface_machine_language_…` for the interpreter, types `AdvancedConfigurationAndPowerInterface…_t`, static helpers `acpi_…`. |
| Tests | `KERNEL_TEST`s in `kernel/tests/acpi/` for architecture-free code, in `kernel/arch/i386/tests/acpi/` otherwise. A host tool's own tests, such as the needs-path closure's, are `host_*` suites in `tools/acpi/host/`: ring 0 never runs them, and `tools/parity.sh` leaves them out. |
| Unbuilt branches | The host harness and every compile-time switch of the interpreter have a gate profile that builds them, or a line in `tools/unbuilt-branches.declared`. `kernel/kernel/acpi/` builds only where `KERNEL_ARCH_HAS_FIRMWARE_TABLES` is 1. |
| i686 now, long mode later (#96) | No pointer is stored in the namespace or in an object, with two exceptions that are never folded: the loaded-table registry's address of a table's bytes, and a frame's transient code pointer (§3.4). Physical addresses are `uint64_t` in every interface. CI also compiles `kernel/kernel/acpi/*.c` with the host's gcc and `-ffreestanding -m64 -mno-red-zone -mcmodel=kernel -nostdinc` against libk's headers and `kernel/include/kernel/acpi/` alone (`tools/acpi/check-portable.sh`). On i686 an address above 4 GiB fails loudly and is never truncated. |
| Firmware memory is read-only | AML is read through the cached read-only firmware window, and the interpreter never writes to it. |

---

## 3. Architecture

### 3.1 Layers

```
L0 tables        RSDP -> RSDT/XSDT -> FADT (normalised), FACS, MADT, MCFG, HPET; the switch to ACPI mode
L1 decoder       PkgLength, NameString, opcode descriptors, operand kinds
L2 namespace     nodes, hash, resolution, definition-block load (DSDT, SSDTs, Load, LoadTable)
L3 interpreter   values, objects, frames, control flow, conversions, budgets
L4 regions       SystemMemory, SystemIO and PCI_Config handlers, fields, mapping cache
L5 OS services   _OSI, _OS, _REV, Notify queue, Sleep, Stall, Timer, host interface
L6 consumers     namespace initialisation, _OSC, _CST; later _PIC with _PRT, _PSS and _CPC, _S5
```

Each layer calls only the ones above it. L6 is ordinary kernel code: it asks the interpreter to evaluate objects
and reads plain results, never interpreter structures. Only the BIOS-area RSDP scan depends on the architecture.

### 3.2 Files

The interpreter and the table parsers live outside `arch/`, where long mode (#96) reuses them unchanged; the
architecture supplies the host interface.

| Path | Holds |
|---|---|
| `kernel/arch/i386/cpu/acpi.c`, `kernel/include/kernel/cpu/acpi.h` | the BIOS-area RSDP scan |
| `kernel/kernel/acpi/tables.c`, `kernel/include/kernel/acpi/tables.h` | the table directory, the normalised FADT, the MADT, MCFG, HPET, the switch to ACPI mode and the power management timer (§3.9), all behind the host interface |
| `kernel/include/kernel/acpi/machine_language.h` | the public interface: load, evaluate by path, read results, counters |
| `kernel/include/kernel/acpi/machine_language_decoder.h`, `kernel/include/kernel/acpi/machine_language_namespace.h`, `kernel/include/kernel/acpi/machine_language_objects.h`, `kernel/include/kernel/acpi/machine_language_interpreter.h`, `kernel/include/kernel/acpi/machine_language_regions.h`, `kernel/include/kernel/acpi/machine_language_services.h` | the decoder's, the namespace's, the objects', the interpreter's, the regions' and the services' interfaces, for the layers above them and the tests |
| `kernel/include/kernel/acpi/host.h` | the host interface the tables and the interpreter call (§3.3) |
| `kernel/kernel/acpi/machine_language_decoder.c` | opcode descriptor tables, PkgLength, NameString, operand kinds |
| `kernel/kernel/acpi/machine_language_namespace.c` | node arena, hash, resolution, temporary nodes |
| `kernel/kernel/acpi/machine_language_load.c` | definition-block load, the loaded-table registry, `Load`, `LoadTable`, the answers of `\_OS` and `\_REV` |
| `kernel/kernel/acpi/machine_language_objects.c` | values, object heap, reference counts, conversions |
| `kernel/kernel/acpi/machine_language_interpreter.c` | the execution loop, frames, control flow, operator handlers |
| `kernel/kernel/acpi/machine_language_regions.c` | region attach, mapping cache, field engine, port and PCI-header protection |
| `kernel/kernel/acpi/machine_language_services.c` | `_OSI`, the Windows version it records and the ports that version protects; `Sleep`, `Stall`, `Timer` and the time budget; the Notify ring; the mutexes, `Signal` and the sync level |
| `kernel/kernel/acpi/initialization.c`, `kernel/include/kernel/acpi/initialization.h` | the initialisation sequence of §3.8 and the boot's load |
| `kernel/kernel/acpi/resources.c` | the zero-copy resource-template iterator |
| `kernel/kernel/acpi/processor_idle.c` | `_CST` per processor, its binding to CPU slots, the published table (§3.10) |
| `kernel/arch/i386/cpu/acpi_host.c` | the host interface in ring 0: windows, memory types, ports, PCI, time, sleep |
| `kernel/arch/i386/lib/asmutils.s`, `kernel/include/kernel/lib/asmutils.h` | the 8-, 16- and 32-bit port accesses of the ring-0 host |
| `kernel/tests/acpi/simulated_host.c`, `simulated_host.h` | the simulated host: sparse memory, zero-filled ports, simulated PCI, virtual clock, target parameters; one source for the host harness and the ring-0 corpus test |
| `kernel/tests/acpi/*.c` | `KERNEL_TEST`s with made-up AML, and the ring-0 corpus test; also compiled into the host harness |
| `tools/acpi/host/` | the host harness, an xmake target of this repository (`test-acpi`): its entry point, the acpidump reader, the corpus runner, the needs-path closure; it compiles the simulated host, `kernel/tests/acpi/` and the kernel's test runner, which prints its KTAP, from `kernel/` |
| `tools/acpi-corpus.sh` | fetches the pinned corpora by commit into `build/acpi-corpus/` and writes the full list; checks each machine's directory (§8.4); builds one Multiboot module per machine; never commits their bytes |
| `tools/acpi/` | the quick list and the corpus (§8.4); the rule that picks 90 machines of the corpus from the tree listing; the census of each dump; the directory each dump must give |

### 3.3 The host interface

Two implementations exist. Ring 0 reaches the machine. The simulated host serves both the host harness and the
ring-0 corpus test, so that a fold compares the interpreter and nothing else (§8.7).

| Function | Ring 0 | Simulated host |
|---|---|---|
| `root_system_description_pointer()` → physical address or 0 | the BIOS-area scan of `kernel/arch/i386/cpu/acpi.c`; the pointer Multiboot2 or the EFI system table gives, once #278 and #279 land | the machine under test's |
| `map_firmware(physical u64, length u64)` → address or NULL: cached, read-only | `kernel_vmm_map_firmware_window` | a sparse byte store keyed by physical address |
| `map_firmware_writable(...)`, `map_device(...)`: cached read-write, uncached | a cached read-write firmware window, `kernel_vmm_map_device_window` | an address that is never read: the store is reached through `read_memory` and `write_memory` |
| `read_memory(address, physical u64, width u8)`, `write_memory(...)`: one access of 1, 2 or 4 bytes to what one of those two mapped | a volatile access through the window | the store: what the placed bytes hold, or the fill byte, until a write takes the page; every access logged in order |
| `compare_exchange(address, physical u64, expected u32, desired u32)` → what the word held: swaps it atomically when it holds `expected` | the compiler's atomic compare-exchange on the window | the store, logged; a simulated firmware may own the word for a number of reads, or ask for the lock once the OS takes it |
| `memory_type(physical u64, length u64)` → RAM, ACPI reclaim, ACPI NVS, reserved, or unknown | the boot memory map the PMM keeps | `reserved`, unless the machine's description says otherwise |
| `unmap_firmware(address, length)`, and one unmap for each map | the matching unmap | matched to its map by address and length, so that a test sees every map unmapped as it was mapped |
| `port_read(port u16, width u8)`, `port_write(...)` | `asmutils` in and out routines | the simulated I/O space, zero-filled but for the ports a test gives a value; every access logged in order |
| `pci_read(segment u16, bus u8, device u8, function u8, offset u16, width u8)`, `pci_write(...)` | the kernel's PCI accessor, one access of the width asked; offsets from 0x100 and segments other than 0 need ECAM (#58) and fail with `address_unreachable` until then | simulated configuration space; absent devices read all ones |
| `clock_100ns()` | the kernel tick, scaled to 100 ns | the virtual clock (§2) |
| `sleep_milliseconds(u32)` → the milliseconds waited, `stall_microseconds(u32)` → the microseconds waited | at boot, `Sleep` halts until a deadline on the tick, rounded up to a whole tick; `Stall` spins on the ACPI power management timer the boot checked (§3.9), or, without one, on the tick, rounded up to a whole tick; each returns what it waited; after boot the worker thread blocks (#273) | advance the virtual clock, by whole ticks when a test gives it one |
| `target()` → physical address width (32 on i686, 52 in long mode), the profile caps of §6.1 | the build's values | given on the command line, printed in every record header |
| `allocate_pages(count)`, `release_pages(address, count)` | `kernel_vmm_alloc_pages`, `kernel_vmm_free_pages` | the same calls, which the host harness serves from `aligned_alloc`; the host counts the pages still out |
| `log(level, text)` | serial, compiled out below the build's level | stdout |

A host is a value, `AdvancedConfigurationAndPowerInterfaceHost_t` in `kernel/include/kernel/acpi/host.h`, with one
function pointer per call, each given the host's context first. The tables and the interpreter are given the host
they read through. Ring 0's is `advanced_configuration_and_power_interface_machine_host`; a test opens a simulated
host beside it.

AML runs at boot only once `clock_initialize` has started the tick. The `[LPLTLM] acpi.services` record reports
the milliseconds `Sleep` and the microseconds `Stall` request at boot, and the time each waited as the tick or the
power management timer counts it, since `instructions.sh` does not see that time.

No callback re-enters the interpreter. The evaluations a region access needs (`_ADR`, `_SEG`, `_BBN`, `_REG`) are
pushed on the same explicit stack (§3.6).

### 3.4 Data structures and their sizes

Every structure is the same size on i686 and x86-64 unless stated, because it holds no pointer. A test prints each
`sizeof`.

| Structure | Fields | Bytes | Notes |
|---|---|---|---|
| **Value** (`…Value_t`) | `kind u8`, `flags u8`, `auxiliary u16`, `handle u32`, `payload u64` | 16 | Integers live in `payload`, with no allocation. Strings, buffers and packages are `handle` = object; a literal becomes one when it is evaluated. A reference is `kind` plus its target: a node; a local or an argument, with its frame's index and serial; or a package, a buffer or a string, which it holds a count on, with an element or byte index. |
| **Node** | `name u32` (the NameSeg as an integer), `parent u32`, `first_child u32`, `next_sibling u32`, `previous_sibling u32` (circular: the first child's previous is the last), `type u8`, `flags u8`, `owner u16` (the table that declared it; a temporary is known by the context's stack), `value Value` | 40 | Declaration order is kept for walks (`_INI` order, dumps); O(1) append and O(1) unlink of temporaries. The object of a Name, Method, buffer field, mutex, Processor or PowerResource fits in `value`, with no second allocation. |
| **Hash slot** | `node u32` (0 is empty) | 4, at a load of at most 1/2, so 8 per node | One open-addressing table for the whole namespace, keyed by (parent, NameSeg): linear probing, backward-shift deletion so no tombstones, a fixed unseeded 32-bit multiplicative mix. |
| **Object header** | `references u32` (saturating), `kind u8`, `flags u8`, `auxiliary u16`, `length u32`, padding | 16 | One allocation per string, buffer or package: the bytes, or the `length` element values, follow the header. |
| **Region object** | `space u8`, `state u8`, `attach u8` (how far a PCI_Config attach went), `attach_flags u8`, `pci_device u32` (segment, bus, device and function, packed: those found so far while it attaches), `offset u64`, `length u64`, `mapping u32`, `attach_node u32` (the node whose object the attach reads next) | 32 | Attached lazily, at the first access. |
| **Field unit object** | `kind u8` (normal, index, bank), `access u8`, `update u8`, `lock u8`, `region u32` (or the index field's node), `data_or_bank u32`, `bank_value u32`, `bit_offset u32`, `bit_length u32` | 24 | A 4 MiB field still costs 24 bytes. |
| **Method** | in the node's `value`: `handle` = table, `payload` = body offset u32 and body length u32, `auxiliary` = MethodFlags | 0 extra | Bodies are never copied. |
| **Buffer field** | in the node's `value`: `handle` = the buffer object, which it holds a count on, `payload` = bit offset u32 and bit length u32, `auxiliary` = access width in bytes, `flags` = reads as a Buffer (`CreateField`) | 0 extra | The object, not the node, as ACPICA and uACPI hold it: a field over a local, an `Index` or a method's buffer has no node, and `CopyObject` to the buffer's name leaves the field on the object it was made over. |
| **Frame** | `code` pointer, `pc u32`, `end u32`, `method u32`, `scope u32`, `arguments[7]` and `locals[8]` (Values), bases into the shared stacks (`u16` × 4), `table u16`, `argument_count u8`, `serial u32` (which frame this is, so that a reference to one of its locals, its arguments or the names it creates dies with it) | 276 on i686, 280 on x86-64 | The only pointer is the transient code pointer; frames are never folded. |
| **Pending operator** | `opcode u16` (a descriptor, or a call), `operands_done u8`, `operand_count u8` (a call's is the method's argument count), `start u32`, `end u32` (its PkgLength end, or the limit it inherits), `node u32` (what its NameString declared or named) | 16 | |
| **Block** | `kind u8` (a table, a TermList, a FieldList, a PackageElementList), `placement u8` (table level, in a method, a method body), `pending_base u16`, `end u32`, `scope u32`, `start u32` (the opcode that opened it) | 16 | |
| **Loaded table** | `bytes` (the kernel-owned copy for `Load`, the firmware window for static tables and `LoadTable`), `physical u64`, `length u32`, `checksum_valid u8`, `origin u8` (static, `Load`, `LoadTable`), `pages u16` (those of the copy), then what decoding found: `terms u32`, `decode_errors u32`, the first error's offset `u32`, opcode `u16` and status `u8` | 36 on i686, 40 on x86-64 | A registry of its own, apart from the table directory; the OEM table ID and the checksum are read in `bytes`. `bytes` is one of the two pointers of §2: printed, never folded. |
| **Evaluation context** | 64 frames, 256 pending operators, 512 blocks, 1,024 values, 1,024 temporary nodes, 32 held mutexes, a 64-entry Notify ring | about 50 KiB, once per worker | Allocated once at initialisation and reused: no allocation and no zeroing per evaluation. |

### 3.5 Memory strategy

| Lifetime | Holds | Allocator | Freed |
|---|---|---|---|
| Permanent | the nodes a table load creates, their objects, the tables `Load` copies | the namespace arena: 64 KiB runs of pages from `kernel_vmm_alloc_pages`, up to a profile cap (§6.1); never the kernel heap, so the World's heap never competes | never (no `Unload`) |
| Temporary | the nodes a method creates (`Name`, `CreateXField`, `OperationRegion`, `Field`, a nested `Method`) | the node arena's free list; their indices are pushed on the context's temporary-node stack | at frame exit, in reverse order: O(1) unlink each, no namespace walk |
| Values | strings, buffers and packages built during an evaluation | the object heap, size classes over the same page runs | when the reference count reaches 0 |
| Evaluation scratch | frames, operator, block and value stacks | the evaluation context | reset at the end of each evaluation |

Rules:

- **Reference counts are plain `uint32_t`, saturating.** One owner runs AML (§3.7), so no atomics. A count that
  reaches `0xFFFFFFFF` sticks there, and the object leaks instead of being freed twice.
- **No reference chain is propagated.** A reference holds one count on its direct target, not on the whole chain.
- **Cycles are refused at store time.** A store that would make an object reach itself fails with
  `reference_cycle`, after an iterative walk with a bounded work list.
- **Literals become objects when evaluated.** Candidate C5 (§7.3) would keep them in the table until written; if it
  is adopted, a buffer field, an `Index` or `RefOf` reference or a store target made over a literal materialises
  it first, and `literal_materialised[cause]` counts why.
- **`Load` copies the loaded table** into the namespace arena and computes its checksum on the copy, because the
  source lies in NVS memory that firmware may rewrite, and on some machines it is corrupted after suspend.
  **`LoadTable` does not copy:** its table is listed in the root table, so it runs in place from its read-only
  firmware window, like a static SSDT.
- **Every size computed from AML is computed in 64 bits** and checked against a cap before any allocation.
- **Occupancy is printed, never folded.** Peak arena, heap and stack use go out on the `[LPLTLM] acpi` line.

### 3.6 Execution model

**One loop, explicit stacks, no C recursion** but the one level `Load` adds until #587 (below). The stacks are fixed
arrays sized by the limits of §6.1, so they are never reallocated and no pointer into them goes stale. A limit reached
is an error, never a panic. Until #587, the declaration pass walks the control flow of table-level code itself and hands
the loop one statement or predicate at a time (below).

**No parse tree.** AML runs straight from the bytes, and an untaken branch is skipped by its PkgLength. Method
bodies are skipped while declarations load, by their PkgLength, and run only when called. Once every table is
declared, the load decodes each body once, running nothing: a body that would lose its place is found at boot rather
than when firmware calls it, and the walk creates no node but the temporaries each body declares while it is
decoded.

**The loop**, in order of precedence at each step:

1. If the top pending operator has all its operands, run its handler, pop it, and hand the result to the operator
   below (as its next operand), or drop it when the operator was a statement.
2. Else, if `pc` reached the end of the innermost block, close the block: a `While` jumps back to its predicate, a
   taken `If` skips a following `Else` by its PkgLength, a `Scope` or `Device` restores the scope, a method end
   performs the implicit return (§5.2), a table end finishes the load frame.
3. Else decode one term at `pc`:
   - a constant (`Zero`, `One`, `Ones`, the integer prefixes, `String`), a `LocalX` or an `ArgX` is a value pushed
     directly, with no operator record;
   - an opcode reads its fixed fields at once (ByteData, WordData, DWordData, PkgLength, a declaration's
     NameString) and pushes a pending operator for its TermArg operands;
   - a NameString in a TermArg position is resolved: a method becomes a call that collects `argc` operands, a data
     object becomes a value, a field unit becomes a field read, and an unresolved name is an error (§6.2).
4. A call whose arguments are complete pushes a frame whose code is the method body inside its table, and moves the
   arguments from the value stack into the frame. `Return` pops frames up to the method's own.

**Operand kinds.** Each opcode descriptor lists its operands as 4-bit kinds packed in one `uint32_t`: TermArg,
SuperName, Target, SimpleName, NameString, ByteData, WordData, DWordData, QWordData, PkgLength, FieldList,
PackageElementList, MatchOpcode.

**Decoding.** Two 256-entry tables (the one-byte opcodes, then the `0x5B`-prefixed ones) map to a compact array of
about 120 descriptors of 8 bytes: handler, operand count, operand kinds, flags (has a PkgLength, opens a scope,
declares a name, yields a value). Handlers are one `switch`, so the compiler can inline the hot ones.

**Bounds at decode time.** Every PkgLength end is checked against the enclosing end before anything reads past it,
and every fixed field and every NameString against the innermost end. A crossing PkgLength is fatal ([§5.4.1]).

**The program counter is committed per whole term.** An error leaves `pc` at the start of the faulty term, never
inside it. The error report names the table, the offset, the opcode and the method path.

**A table load is a frame.** A definition block runs as a pseudo-method whose named objects are permanent and whose
`Local0` to `Local7` exist. Until #587, `Load` and `LoadTable` run the declaration pass of their table from their
handler, one level of C deep, its statements on the stacks of the method that loads it: a `Load` or `LoadTable` that
table-level code runs while a table is declared fails with `load_form_unsupported`, so there is no second level.
Declarations load in one pass and in order: table-level code sees only what was declared before
it. An `External` whose parent no table has declared yet is placed by a pass of its own once every table is
declared; one whose parent exists is placed where it stands.

**The declaration pass keeps the structure of table-level code.** It declares, and hands each statement and each
predicate of an `If` or a `While` to the interpreter, which runs it in the table's frame; an untaken branch is
skipped, and declares nothing. A predicate that fails declares both its branches without running their code, counted
(`declared_not_run`), so that a failure never takes an object out of the namespace. The decoding pass still decodes
the body of a construct the declaration pass did not declare, but counts its faults apart (`undeclared_faults`): its
names resolve against objects that do not exist.

**The evaluations a region needs** (`_ADR`, `_SEG`, `_BBN`, `_HID` and `_CID` while a PCI_Config region attaches;
`_REG` when a method creates a PCI_Config region after step 3 of §3.8) are pushed as frames marked "internal" on
the same stacks. When an attach's frame returns, the access resumes: the name that reached it runs again; when a
`_REG`'s returns, the method that created the region goes on at its next statement. A field unit starts
the attach of its regions when code first names it, whatever the operator, so that a reference made to it reads it
later. They count against the same depth limit. A failure inside an internal frame, a limit of §6.1 apart, makes the
object it evaluates absent, counted, as ACPICA and uACPI take a failed `_ADR`, `_SEG` or `_BBN`: an attach that needs
a field of its own region reads `region_unevaluated` there, and goes on without that object; a `_REG` that fails is
dropped, counted. An attach that a name inside a package (§5.1) or a String path (§4.2) reaches, where no frame can
wait, fails as `region_unevaluated` when it needs a method.

**Errors** carry a typed status (§6.2). In a method, an error unwinds every frame of the evaluation: temporaries
removed, mutexes released, values dropped. At table level, a statement that fails to run is skipped, a predicate
that fails is handled as above, a fault in the declaration pass's own decoding skips the innermost enclosing
PkgLength construct, and the load goes on.

### 3.7 Concurrency

**One owner runs AML, one evaluation at a time, to completion.**

| Phase | Who runs AML | Where | Why |
|---|---|---|---|
| Boot | the bootstrap processor | before any World tick starts, once the PMM is complete and `clock_initialize` has run | ACPI mode, the namespace load, the initialisation, `_OSC` and `_CST` all happen once |
| After boot | the ACPI worker thread (#273), pinned to a core that holds no World tick, fed by a request ring (#272) | "evaluate X" goes in, results come out | AML traps to SMM, sleeps and polls, while a client's budget is 1 ms of latency (#147) and 100 µs of jitter (#349) |
| Interrupts | never | the SCI handler (#547) only acknowledges, disables the GPE and queues work | |

On a machine with a single core, AML runs only at boot and at shutdown until events land (#547).

Consequences:

- **No lock in the interpreter.** A blocking `Sleep` blocks the worker; no other AML runs meanwhile.
- **`Serialized` costs a sync-level check, not a mutex object** (§5.2).
- **`Acquire` of an AML mutex never waits:** it succeeds or nests, because the mutexes still held are released when
  an evaluation ends, unless the sync level has it ignored (§5.2). Only the global lock contends, with firmware,
  through `\_GL` and `Lock` fields.
- **Results are published, not shared.** A consumer gets a plain copy: for `_CST`, a per-CPU table of states that
  the idle path reads without ever calling the interpreter.
- **Notify never runs inside the evaluation that raised it.** It goes to a bounded ring and is delivered after the
  evaluation ends: at boot by step 9 of §3.8, after boot by the worker between requests. Until #547, the only Notify
  acted on is 0x81 on a processor (`_CST` changed).

Concurrent evaluations, on several workers, would bring back a namespace lock and per-method owner checks.

### 3.8 The initialisation sequence

Run once at boot by the bootstrap processor.

0. **Put the platform in ACPI mode,** before any AML runs: firmware written for Windows has only ever run its `_INI`
   and `_OSC` in ACPI mode.
   1. Skip the switch if SCI_EN is already set, if SMI_CMD is 0, if ACPI_ENABLE is 0, which declares no legacy mode
      ([§5.2.9]), or if the FADT is hardware-reduced, which boots in ACPI mode only ([§3.11.1]). The switch reaches
      its registers as I/O ports alone: SMI_CMD or a PM1 or GPE block outside the I/O space, or on a protected port
      (§5.3), skips it too, counted. A block too short to hold an enable register is absent.
   2. Clear every GPE enable bit and every PM1 fixed-event enable bit, and keep the SCI line (SCI_INT) masked in the
      8259 or the I/O APIC, so that no event raises an SCI that nothing handles.
   3. Write ACPI_ENABLE to SMI_CMD from the bootstrap processor, and wait for SCI_EN ([§5.2.9]): polls 1 ms apart on
      the kernel tick, at most 3,000 of them and 3,000 ms as the tick counts them, counted.
1. **Load the DSDT, then every SSDT** of the root table in directory order, each as a table frame. A failing table
   is counted and the next one loads; a table whose checksum fails is loaded and counted (§3.9). The integer width
   is fixed from the DSDT's revision before the first byte runs.
2. **Run `\_INI`, then `\_SB._INI`,** before any `_REG`, as Windows does.
3. **Run `_REG(PCI_Config, 1)`** once in every scope that declares a PCI_Config region; never for SystemMemory and
   SystemIO ([§6.5.4]).
4. **Walk the devices in declaration order** ([§6.3.7], [§6.5.1]), processors and thermal zones among them, as
   ACPICA and uACPI do; `\_SB._INI`, which step 2 ran, is not run again. A device without `_STA` is 0x0F. Present: run
   `_INI` and descend. Not present but functioning: skip `_INI` and descend. Neither: skip the subtree. Bits 0 and 3
   alone decide, as [§6.3.7] and the table of [§6.5.1] say and ACPICA, uACPI and Linux do: a result §6.3.7 calls
   invalid, bit 0 clear with bit 1 set, or bit 0 set with bit 1 clear and bit 3 set, is walked as any other and
   counted (`status_invalid`); whether to use such a device is its consumer's decision. A pre-pass marks the subtrees
   that hold an `_INI`, so most devices need no `_STA` evaluation.
5. **`\_PIC` is not called** while the kernel routes PCI INTx through the 8259; `pic_mode_deferred` counts it.
   Firmware assumes PIC mode until told otherwise ([§5.8.1]), and on AMD clients `\_PIC(1)` can rewrite every PIRQ
   router, which would silence every PCI device the kernel drives through the 8259. It is called once INTx goes
   through the I/O APIC (#59).
6. **Platform `\_SB._OSC`** with UUID `0811B06E-4A27-44F9-8D60-3CBBC22E7B48`, query then commit, claiming none of
   the DWORD 2 capabilities ([§6.2.12.1], [§6.2.12.2]), since the kernel exercises none of them at boot. #302 claims
   CPPC (bits 5, 6 and 10) in a second evaluation.
7. **For each processor object bound to a CPU slot** (§3.10), in declaration order, `_OSC` with Intel's processor
   UUID `4077A616-290C-47BE-9EBD-D87058713953`, a commit without a query, or `_PDC` with a buffer of revision 1 and
   one DWORD when the processor has no `_OSC` ([Appendix C]). The capabilities are bits 3 and 4 of [Intel's processor
   vendor-specific ACPI document][Intel ACPI], independent C1, P- and T-state support, and independent C2 and C3, on
   each processor, which firmware loads `_CST` for, and bits 8 and 9, MWAIT for C1 and for C2 and C3, when the
   processors implement MONITOR/MWAIT, so that `_CST` names MWAIT hints; #302 claims the P-state bits once it drives
   them. A processor object bound to no CPU slot is given
   nothing, and counted. The pre-pass of step 4 also marks the subtrees that hold an `_OSC` or a `_PDC`, so this
   step walks only them. This is where `Load` runs on Intel clients.
8. **Write CST_CNT to SMI_CMD** ([§5.2.9]) when it is not 0, SMI_CMD is a port the kernel may write (§5.3), and
   step 0 left the platform in ACPI mode: switched, found switched, or without a legacy mode; then `_CST` for each
   processor object Linux attaches (§3.10).
9. **Drain the Notify ring.** A Notify 0x81 on a processor, queued during or after its step-8 evaluation, triggers
   one more `_CST` evaluation; every other Notify is counted and dropped.

Steps 5 to 8 follow the order firmware depends on: Intel's `GCAP` reads the platform `_OSC` result, and on Intel
clients `_CST` exists only after step 7.

**When a step fails,** the sequence goes on; each failure is counted per status, and the first one is named in the
`[LPLTLM] acpi.init` record.

| Step | On failure or absence | Then | Counter |
|---|---|---|---|
| 0 ACPI mode | SCI_EN still clear after the bound | continue in legacy mode; step 8 writes no CST_CNT | `acpi_mode_failed` |
| 1 a table | the table's failure (§6.2) | the next table loads | `table_load_failed` |
| 2 `\_INI`, `\_SB._INI` | the evaluation's status | continue | `initialization_failed` |
| 3 a `_REG` | the evaluation's status | the next scope | `region_registration_failed` |
| 4 a `_STA` | the evaluation's status | no `_INI` for that device; its children are still walked | `status_failed` |
| 4 an `_INI` | the evaluation's status | continue and descend | `initialization_failed` |
| 5 `\_PIC` | not called | | `pic_mode_deferred` |
| 6 platform `_OSC` | absent, failed, or an error bit in the returned DWORD 1 | continue as if nothing was granted | `platform_capabilities`: `absent`, `failed` or `refused` |
| 7 processor `_OSC` or `_PDC` | the evaluation's status, or an error bit in `_OSC`'s DWORD 1 | continue; that processor may have no `_CST` | `processor_capabilities_failed`, `processor_capabilities_refused` |
| 8 a `_CST` | the evaluation's status, or a malformed package | that CPU keeps the states the kernel enters without ACPI (HLT, MWAIT C1) | `processor_idle_failed` |

**Tables loaded at run time.** A table loaded before step 3 is initialised by steps 3 and 4 with the static ones,
and step 7 sees its processors. A table loaded later can declare regions and devices: once the evaluation that loaded
it returns, or once step 3 is over for a `_REG`'s, its scopes get the `_REG` of step 3, which step 3 leaves to it. Its
devices that step 4 still reaches, below the device step 4 is at or below a node it has not reached yet, are left to
step 4; the others get the `_STA` and `_INI` walk of step 4 over their subtrees ([§6.5.1]). The devices above them
whose `_STA` step 4 did not read, in subtrees it skipped, have it read first, from the top down, under step 4's rule,
so that the result does not depend on where the loader is declared; nothing below a device found neither present nor
functioning is walked. No `_INI` runs twice. `late_initializations` counts how often this does anything.

### 3.9 The static tables

- **The root table.** The XSDT is walked when the RSDP's revision is 2 or more and the XSDT's checksum holds;
  otherwise the RSDT is walked. An XSDT entry above 4 GiB is counted unreachable, never truncated.
- **The directory.** Each table is read once, mapped through a cached read-only firmware window, and recorded with
  its checksum's validity. A lookup by signature returns the first table whose checksum holds, otherwise the first
  whose checksum fails. Tables loaded at run time go to the loaded-table registry (§3.4).
- **A checksum that fails** does not refuse a table whose header and length are sane: it is read, logged and
  counted (`checksum_bad_loaded` for the DSDT and SSDTs, `checksum_bad_parsed` for the static tables), and every
  field is bounds-checked against the table's length. A refused FADT or MADT would leave the machine without ACPI,
  or without its other CPUs.
- **The DSDT** is the FADT's 64-bit `X_DSDT` when it is not 0 and lies below 4 GiB, otherwise its 32-bit `DSDT`.
- **The FACS** is the 32-bit `FIRMWARE_CTRL` when it is not 0, otherwise `X_FIRMWARE_CTRL` when it lies below
  4 GiB, otherwise none. Some machines fail to resume on the 64-bit address (Linux bug 74021).
- **The FADT is normalised once:** its revision recomputed from its length; a zeroed local copy of at most the known
  length, so that the fields a short FADT lacks read 0; the legacy register blocks converted to Generic Address
  Structures, unless the 64-bit block gives an address. PSTATE_CNT and CST_CNT, which ACPI 1.0 reserved, read 0 up to
  revision 2; RESET_REG reads absent without RESET_REG_SUP; a hardware-reduced FADT reads 0 in every field ACPI tells
  OSPM to ignore ([§5.2.9]). The fields read are those of N4 (§1.2), plus the PM1 and GPE blocks. A GPE block's
  length is GPE0_BLK_LEN or GPE1_BLK_LEN, for the 64-bit block too ([§5.2.9]), and 0 without it, as in ACPICA.
- **A block keeps the width firmware declares,** its legacy length when its Generic Address Structure gives none: a
  PM1 control block may be longer than 2 bytes ([§4.8.1.2]), and SCI_EN is read with a word access
  ([§4.8.3.2.1]). ACPICA instead rewrites each PM block's width to a fixed default.
- **The power management timer** that `Stall` waits on (§3.3) is PM_TMR_BLK, used only when it lies, 4 bytes long, in
  the I/O space and passes the checks Linux makes before it trusts one, with three differences:
  - each read is three reads of the port, the middle one kept once the three are in order, a middle one that differs
    from two equal ones out of order too, at most 16 times; Linux reads so only on the chipsets it lists, unbounded;
  - only its 24 low bits count, whatever TMR_VAL_EXT says; it must move forward ten times, each within 10,000 reads,
    the ten checks back to back, where Linux waits `udelay(100 * j)` between them;
  - from one step of the kernel's clock to the first at least 80 ms later, eight ticks of IRQ0, it must count
    3.579545 MHz within 5 %, in one of two measurements; Linux measures 30 ms of PIT channel 2 by polling it, once.
    An IRQ0 edge seen 2 ms late moves a measurement by 2.5 %: an exact timer is still kept, and one 8 % off still
    refused, where 30 ms or 60 ms would let it through.

  A clock that does not step within 65,536 reads leaves the timer unused (`no-reference`), and so does a timer that
  counts half a second before it steps (`ran-ahead`): a still clock whose reads, with the timer's, take more than
  about 7.6 µs a poll reads as `ran-ahead` too, since these two sources cannot tell it from a timer far too fast.
  Once used, a timer that a wait sees stand still for 10,000 reads, or step more than half its 24-bit range, which
  is going back, is `derailed`: the wait finishes on the tick, and so do the later ones. A pause between two reads of
  a wait reads modulo 4.69 s, the 24-bit range, so one of 2.34 s to 4.69 s demotes a healthy timer for the rest of
  the boot, which is accepted: nothing at boot holds a wait off the processor that long. Otherwise, and on a hardware-reduced platform, `Stall` spins on the tick. The boot prints one
  `[LPLTLM] acpi.timer` line once its AML has run: the state, the port, the rate when it was measured, the reads and
  the time the checks took, the stalls and their reads.
- **The switch to ACPI mode** is a routine of the tables layer: SCI_EN read, enables cleared, ACPI_ENABLE written,
  a bounded poll (§3.8 step 0).
- **The MADT.** Types 0, 1, 2, 4, 5, 9 and 0xA are read. A CPU slot exists only for an entry whose Enabled flag is
  set; an Online Capable entry is counted and unused, since there is no hotplug; an entry with both flags clear is
  ignored ([§5.2.12.2]). Every other type, the RISC-V types and the Multiprocessor Wakeup structure among them, is
  skipped by its length and counted; an entry of a type read but shorter than that type is skipped and counted
  apart, since it loses a processor, an interrupt line or the local APIC address. No CPU cap sits below the
  profile's, and the ACPI Processor UID is kept per CPU slot. A local APIC ID of 0xFF, an x2APIC ID of 0xFFFFFFFF,
  and an x2APIC ID below 0xFF when a local APIC entry whose ID is not 0xFF is Enabled or Online Capable name no
  processor, as
  [Linux](https://github.com/torvalds/linux/blob/e5f0a698b34ed76002dc5cff3804a61c80233a7a/arch/x86/kernel/acpi/boot.c#L162-L272)
  does on firmware of ACPI 6.3 or later. On
  [older firmware](https://github.com/torvalds/linux/blob/e5f0a698b34ed76002dc5cff3804a61c80233a7a/arch/x86/kernel/acpi/boot.c#L150-L154)
  Linux also counts a local APIC entry with both flags clear, which ACPI 6.6 tells OSPM to ignore ([§5.2.12.2]). The
  flags decide first: such an entry is counted invalid when it is Enabled, and with the Online Capable or the
  disabled entries otherwise. The MADT keeps 256 CPU slots, 256 NMI connections, 64 I/O APICs and 16 interrupt
  source overrides, past which it counts what it drops; an NMI entry whose UID is 0xFF, or 0xFFFFFFFF for an x2APIC
  one, applies to every processor. The boot prints one `[LPLTLM] acpi.madt` line with the slot count and every
  counter.
- **MCFG** gives the segment groups and their ECAM bases (#58); **HPET** its base address (#60, #298). Each is
  parsed by its first consumer.

### 3.10 What `_CST` publishes

- **One `_CST` per processor object** ([§8.4.1.1]): the count, then for each state the Register descriptor
  ([§6.4.3.7]) in any space, kept in the entry, FFixedHW with the MWAIT hint or a SystemIO port read most often,
  then the type, the latency and the power. Linux keeps only FFixedHW and SystemIO; which states to enter is
  #298's decision.
- **Resource templates** are read by a zero-copy iterator: Generic Register and EndTag are decoded, every other
  descriptor is skipped by its length.
- **Binding.** Each processor object binds to the CPU slot of an Enabled MADT entry through its ACPI Processor UID:
  the `_UID` of an `ACPI0007` device, or the ProcessorID of `Processor` ([§8.4]). An unbound processor is counted,
  and so is one whose slot an object declared before it took.
- **Presence.** Step 8 evaluates the `_CST` of a processor object Linux attaches (v7.3 `acpi_bus_attach`,
  `acpi_processor_add`): its `_STA` present or functioning, and enabled, and no Device, Processor or ThermalZone
  above it found absent. Step 8 decides the devices above it as the walk of step 4 does, and shares each decision
  with step 4 and the late initialisation, so that a device's `_STA` is read once: one step 4 or a late
  initialisation found absent keeps the processor out unread, and one whose `_STA` fails is walked into, where
  Linux would read it as 0. The processor's own `_STA` reads 0x0F when missing, and keeps it out when it fails,
  counted. A processor kept out takes no slot, and is counted (`processor_idle_absent`).
- **Every state firmware lists is published,** type 3 included ([§8.1.4]), up to the cap of §6.1; which states to
  enter is #298's policy.
- **A faulty state is left out** and counted on its slot, as Linux skips it: a sub-package of other than four
  elements, a Register that is no Buffer holding a Generic Register before its End Tag, or a Type, Latency or Power
  that is no Integer of 32 bits, where Linux keeps the low 32 bits of a wider one.
- **A malformed package publishes no state**, as Linux refuses it: no Integer Count, a Count below 1, or a Count
  other than the number of sub-packages after it.
- **The table is published per CPU,** as plain data the idle path reads, with one `[LPLTLM] acpi.cst` line per CPU.
  A Notify 0x81 re-evaluates it (§3.8 step 9).

---

## 4. The AML subset

### 4.1 The rule

1. **Decode everything.** Every encoding of the byte table ([§20.3]) is decoded with its exact operands, plus
   `Processor` (permanently reserved, [Appendix C]) and `Unload` (reserved). A byte outside the table is
   `invalid_opcode`.
2. **Execute what the needs path reaches.** The host harness computes the static closure of the needs path (§1.3)
   on every corpus machine. Being static, it covers every branch, so it is a superset of what runs. Every opcode the
   closure reaches on any machine is executed; every other fails with `unimplemented_operator`, counted, until a
   count asks for it. A later need adds the closure of its own entry points: `_PSS` and `_CPC`, `_PRT`, `_PTS` and
   `\_S5`.
3. **Three region spaces have a handler:** SystemMemory, SystemIO and PCI_Config. Every other space decodes and
   fails loudly at access (§4.5).
4. **The needs path is run too.** The host harness and the ring-0 corpus test run §3.8 and `_CST` on every corpus
   machine and print the opcodes, spaces and access types they executed. One executed and not implemented is a red
   test.

### 4.2 The decoder's alphabet

| Kind | Encoding | ACPI | Notes |
|---|---|---|---|
| PkgLength | 1 to 4 bytes; bits 7-6 of the lead byte count the follow bytes; a 1-byte length uses bits 5-0, otherwise bits 3-0 then up to 3 bytes; at most 2^28; **the length counts its own bytes** | [§20.2.4], [§5.4.1] | the end is start + PackageLength, with the start taken before the length is read |
| NameSeg | 4 bytes: `A-Z` or `_` first, then `A-Z`, `0-9` or `_`; compared as a `uint32_t` | [§20.2.2] | |
| NameString | `\` or `^`*, then a NameSeg, `0x2E` and 2 segments, `0x2F` and a count (1 to 255) and segments, or `0x00` (NullName) | [§20.2.2] | a count of 0 is a decode error |
| TermArg | any expression, a `LocalX`, an `ArgX`, a constant, or a NameString (data object, field read or method call) | [§20.2.5] | the only context-sensitive point: a call's argument count comes from the namespace |
| SuperName | SimpleName, `Debug`, or a reference-yielding expression (`RefOf`, `DerefOf`, `Index`) | [§20.2.2] | a method there is called; any other opcode that is not a statement is decoded there too, as in ACPICA's parser: firmware ships `Store (X, Add (Local1, 8))` |
| Target | SuperName or NullName | | NullName discards the result |
| SimpleName | NameString, `ArgX`, `LocalX`, and whatever SuperName accepts | | **never a call:** the operand of `CondRefOf` and `RefOf`, `ObjectType`, the destination of `CopyObject`. The grammar allows a call there; following it literally loses sync on almost every real firmware at `CondRefOf (\_OSI, Local0)`. |
| ByteData, WordData, DWordData, QWordData | little-endian fixed fields | [§20.2.3] | |
| FieldList | named, reserved (`0x00`), AccessAs (`0x01`), Connection (`0x02` and a NameString or a Buffer), ExtendedAccessAs (`0x03`) | [§20.2.5.2] | all five decoded |
| PackageElementList | a DataRefObject or a NameString per element | [§20.2.5.4] | an expression as an element is refused |

**Where ACPI contradicts itself,** the decoder accepts both forms, and the test suite holds one case per form:

| Opcode | Grammar ([§20.2.5.4]) | Byte-value table ([§20.3]) | Decoded as |
|---|---|---|---|
| `CondRefOf` | `SuperName Target` | `SuperName SuperName` | SimpleName, then Target |
| `Load` | `NameString Target` | `NameString SuperName` | SuperName, then Target |

`DerefOf` takes a TermArg whose value must be a reference or a string path. `Notify`'s object is a SuperName
([§20.2.5.3]).

### 4.3 The opcodes

An opcode marked "executed" runs when the needs-path closure reaches it (§4.1), and fails with
`unimplemented_operator` otherwise.

**Data objects and constants**: built at load and in methods.

| Encoding | Opcode | ACPI |
|---|---|---|
| `0x00` | Zero | [§19.6.156] |
| `0x01` | One | [§19.6.98] |
| `0xFF` | Ones | [§19.6.99] |
| `0x0A`, `0x0B`, `0x0C`, `0x0E` | ByteConst, WordConst, DWordConst, QWordConst | [§20.2.3] |
| `0x0D` | String | [§19.3.2.2] |
| `0x11` | Buffer | [§19.6.10] |
| `0x12` | Package | [§19.6.102] |
| `0x13` | VarPackage | [§19.6.102] |
| `5B 30` | Revision: decoded only | [§19.6.121] |
| `5B 31` | Debug | [§19.6.26] |

**Namespace modifiers and named objects**: declared at load.

| Encoding | Opcode | ACPI |
|---|---|---|
| `0x08` | Name | [§19.6.90] |
| `0x10` | Scope | [§19.6.122] |
| `0x14` | Method | [§19.6.85] |
| `0x15` | External | [§19.6.46] |
| `0x06` | Alias | [§19.6.4] |
| `5B 82` | Device | [§19.6.31] |
| `5B 83` | Processor | [Appendix C] |
| `5B 84` | PowerResource | [§19.6.108] |
| `5B 85` | ThermalZone | [§19.6.135] |
| `5B 01` | Mutex | [§19.6.89] |
| `5B 02` | Event | [§19.6.42] |
| `5B 80` | OperationRegion: accessed through §4.4 | [§19.6.100] |
| `5B 81` | Field: accessed through §4.4 | [§19.6.48] |
| `5B 86` | IndexField: accessed through §4.4 | [§19.6.64] |
| `5B 87` | BankField: accessed through §4.4 | [§19.6.7] |
| `5B 88` | DataTableRegion: decoded only | [§19.6.25] |
| `0x8D` | CreateBitField | [§19.6.18] |
| `0x8C` | CreateByteField | [§19.6.19] |
| `0x8B` | CreateWordField | [§19.6.23] |
| `0x8A` | CreateDWordField | [§19.6.20] |
| `0x8F` | CreateQWordField | [§19.6.22] |
| `5B 13` | CreateField | [§19.6.21] |

**Control flow, locals and arguments**: executed.

| Encoding | Opcode | ACPI |
|---|---|---|
| `0xA0` | If | [§19.6.60] |
| `0xA1` | Else | [§19.6.39] |
| `0xA2` | While | [§19.6.150] |
| `0xA5` | Break | [§19.6.8] |
| `0x9F` | Continue | [§19.6.16] |
| `0xA4` | Return | [§19.6.120] |
| `0xA3` | Noop | [§19.6.92] |
| `0x60` to `0x67` | Local0 to Local7 | [§19.6.79] |
| `0x68` to `0x6E` | Arg0 to Arg6 | [§19.6.6] |

**Expressions**: executed.

| Encoding | Opcode | ACPI |
|---|---|---|
| `0x70` | Store | [§19.6.132] |
| `0x9D` | CopyObject | [§19.6.17] |
| `0x71` | RefOf | [§19.6.115] |
| `5B 12` | CondRefOf | [§19.6.14] |
| `0x83` | DerefOf | [§19.6.30] |
| `0x88` | Index | [§19.6.63] |
| `0x87` | SizeOf | [§19.6.126] |
| `0x8E` | ObjectType | [§19.6.97] |
| `0x89` | Match | [§19.6.81] |
| `0x72` | Add | [§19.6.3] |
| `0x74` | Subtract | [§19.6.133] |
| `0x77` | Multiply | [§19.6.88] |
| `0x78` | Divide | [§19.6.32] |
| `0x85` | Mod | [§19.6.87] |
| `0x75` | Increment | [§19.6.62] |
| `0x76` | Decrement | [§19.6.27] |
| `0x79` | ShiftLeft | [§19.6.123] |
| `0x7A` | ShiftRight | [§19.6.124] |
| `0x7B` | And | [§19.6.5] |
| `0x7C` | Nand | [§19.6.91] |
| `0x7D` | Or | [§19.6.101] |
| `0x7E` | Nor: decoded only | [§19.6.93] |
| `0x7F` | Xor | [§19.6.155] |
| `0x80` | Not | [§19.6.94] |
| `0x81` | FindSetLeftBit | [§19.6.49] |
| `0x82` | FindSetRightBit | [§19.6.50] |
| `5B 28` | FromBCD | [§19.6.55] |
| `5B 29` | ToBCD | [§19.6.137] |
| `0x90` | LAnd | [§19.6.69] |
| `0x91` | LOr | [§19.6.80] |
| `0x92` | LNot | [§19.6.75] |
| `0x93` | LEqual | [§19.6.70] |
| `0x94` | LGreater | [§19.6.71] |
| `0x95` | LLess | [§19.6.73] |
| `0x73` | Concatenate | [§19.6.12] |
| `0x84` | ConcatenateResTemplate | [§19.6.13] |
| `0x9E` | Mid | [§19.6.86] |
| `0x96` | ToBuffer | [§19.6.138] |
| `0x97` | ToDecimalString | [§19.6.139] |
| `0x98` | ToHexString | [§19.6.140] |
| `0x99` | ToInteger | [§19.6.141] |
| `0x9C` | ToString | [§19.6.143] |

**System, synchronisation and events**: executed.

| Encoding | Opcode | ACPI |
|---|---|---|
| `0x86` | Notify: queued (§3.7) | [§19.6.95] |
| `5B 22` | Sleep | [§19.6.127] |
| `5B 21` | Stall | [§19.6.129] |
| `5B 33` | Timer | [§19.6.136] |
| `5B 23` | Acquire | [§19.6.2] |
| `5B 27` | Release | [§19.6.117] |
| `5B 24` | Signal | [§19.6.125] |
| `5B 25` | Wait | [§19.6.149] |
| `5B 26` | Reset | [§19.6.118] |
| `5B 32` | Fatal: logged and counted | [§19.6.47] |
| `0xCC` | BreakPoint: a counted no-op | [§19.6.9] |

**Definition blocks at run time**: executed in the forms §1.4 allows.

| Encoding | Opcode | ACPI |
|---|---|---|
| `5B 20` | Load | [§19.6.77] |
| `5B 1F` | LoadTable | [§19.6.78] |
| `5B 2A` | Unload: decoded and refused (§4.5) | [§20.3] |

**Composite comparisons.** `LNotEqual`, `LLessEqual` and `LGreaterEqual` are `LNot` followed by `LEqual`,
`LGreater` or `LLess` ([§20.3]); the decoder fuses the pair into one operator (O7, §7.2).

**Method invocation.** A NameString in a TermArg position that resolves to a method is a call, executed.

**Never executed:** the obsolete `5B 10` and `5B 11`, and `0xA9`, which are not ACPI opcodes; they decode as
`invalid_opcode`.

### 4.4 Fields and region spaces

| Item | Behaviour | ACPI |
|---|---|---|
| SystemMemory regions | accessed through the mapping cache (§5.3) | [§5.5.2.4.1] |
| SystemIO regions | accessed through `port_read` and `port_write`, with port protection (§5.3) | [§5.5.2.4.1] |
| PCI_Config regions | the address comes from `_SEG`, `_BBN` and `_ADR` through the bridges, resolved once at attach; the header is write-protected (§5.3). The root bridge is the closest ancestor whose `_HID` or `_CID` is PNP0A03 or PNP0A08; the region belongs to the closest Device at or above it, reached through each Device with an `_ADR` on the way down: a bridge moves the bus to its secondary one, and a function that is not a bridge keeps what lies below it, as uACPI walks it. A function behind an absent bridge or a bridge with no secondary bus, or an `_ADR` that names no device and function a bus holds, reads all ones and drops writes, counted. A datum its width does not divide is made byte by byte, counted. Offsets from 0x100 and segments other than 0 need ECAM (#58) and fail with `address_unreachable`, counted, until then | [§5.5.2.4.1] |
| EmbeddedControl | no handler: an access fails and is counted; `_REG(3, 1)` is never called (§1.4) | |
| SystemCMOS, GenericSerialBus, GeneralPurposeIo, IPMI, PlatformRtMechanism, SMBus, PciBarTarget, PCC, FFixedHW, OEM 0x80-0xFF | no handler (§1.4) | |
| AccessType ByteAcc, WordAcc, DWordAcc, QWordAcc | one access per datum of that width; on i686 a 64-bit SystemMemory datum is two 32-bit accesses, low then high (§5.3); a QWordAcc datum of SystemIO or PCI_Config, wider than any port or configuration access, fails with `access_width_invalid` | [§19.6.48] |
| AccessType 6 to 15, reserved | the access fails with `access_width_invalid`, as ACPICA and uACPI refuse the field | [§19.6.48] |
| AnyAcc | byte access, as Windows does | [§19.6.48] |
| BufferAcc | byte access on the three spaces; the protocol buffers of GenericSerialBus, SMBus and IPMI wait for their handlers | [§19.6.48] |
| UpdateRule Preserve, WriteAsZeros, WriteAsOnes | all three | [§19.6.48] |
| LockRule `Lock` | the access takes the FACS global lock for its whole length, read-modify-write and registers included, and gives it back after: owned when free; pending when the firmware owns it, then up to 65,536 reads waiting for its release. A wait that runs out fails with `global_lock_unavailable`, counted. When the firmware asked for the lock, the release reads the PM1a and PM1b control registers, ORs them, clears their write-only bits, sets `GBL_RLS`, and writes the value to PM1a then PM1b, skipping a protected port. The lock word stays mapped for the whole load. Without a usable FACS (absent, beyond 4 GiB, not aligned on 64 bytes, in usable memory, without its signature, or shorter than 24 bytes), or on a hardware-reduced platform, the access takes no lock, counted (`unlocked_lock_accesses`, and `facs_refused` for a FACS given but refused), as uACPI does. | [§19.6.48], [§5.2.10.1] |
| `AccessAs` (`0x01`) | changes the width of the units that follow | [§19.6.48] |
| `Connection` (`0x02`), `ExtendedAccessAs` (`0x03`) | decoded; their accesses fail with `space_unsupported` | [§19.6.48] |
| IndexField, BankField | the index or bank register is written, then the data accessed, through an internal loop, never C recursion | [§19.6.64], [§19.6.7] |
| A unit spanning two accesses | the split path; a unit inside one aligned access takes the fast path | |
| A unit wider than 64 bits | read as a Buffer, up to the object size cap of §6.1 | [§19.6.48] |

### 4.5 What an unimplemented opcode, space or object does

| Case | Behaviour | Counter |
|---|---|---|
| An opcode decoded but not executed: outside the needs path, `DataTableRegion`, `Nor`, `Revision` | in a method, the evaluation fails with `unimplemented_operator`; at definition-block level, the statement or predicate that holds it fails as in §6.2 and the load goes on | `unimplemented_operator[opcode]` |
| `Load` from a Buffer; `LoadTable` with a non-empty RootPath, ParameterPath or ParameterData; either run while a table is declared | fails as above | `load_form_unsupported` |
| A byte that is not an opcode | fails as above, with `invalid_opcode`; at table level without an enclosing construct, the rest of the table is abandoned and the next table loads | `invalid_opcode` |
| `Unload` | its operand is evaluated, and nothing else happens | `unload_refused` |
| A region space without a handler | the field access fails with `space_unsupported`; `_REG` is never called for that space | `space_unsupported[space]` |
| `Connection` or a `BufferAcc` protocol on an unsupported space | same | same |

"Loudly" means one log line naming the table, the offset, the opcode and the method path; the counter on the
`[LPLTLM] acpi` boot line; and a typed error to the caller. Never a panic, never a silent no-op.

---

## 5. Semantics

Each row is a decision, and has at least one test (§8).

### 5.1 Names and loading

| Topic | Rule | ACPI |
|---|---|---|
| Upward search | Only a single NameSeg without prefix searches the current scope, then each parent up to the root; `\`, `^`, Dual and Multi names never search. | [§5.3] |
| Creating a name | Only the last segment is created; the others must exist. | [§5.3] |
| A duplicate at load | The first definition stays; the duplicate is skipped and counted. ACPI calls a collision fatal, but real firmware ships duplicate SSDTs. | [§5.3] |
| A checksum that fails | A DSDT or SSDT whose header and length are sane is loaded anyway, logged and counted (`checksum_bad_loaded`). | §3.9 |
| Table-level code | Runs in place, in order, in a frame with locals (§3.6). Single pass: a call or a field at table level sees only what is declared before it. | [§5.4.2] |
| Region and field operands | Evaluated when the declaration runs. | [§19.6.100] |
| The first-method rule | Not implemented: no real table puts a named control method first. | [§5.4.2] |
| Integer width | Fixed once from the DSDT's revision, for every table: below 2, every integer is 32 bits, results are masked, `Ones` is `0xFFFFFFFF` and Integer to Buffer gives 4 bytes. | [§19.6.29] |
| `External` | Creates nothing at run time. Its node only gives the decoder a method's argument count, and a declaration of the object replaces it. One whose parent does not exist yet is placed once every table is declared, the missing segments created as Externals: firmware calls such methods behind `CondRefOf`. | [§19.6.46] |
| Unresolved names | An error only when executed: in methods they sit behind `CondRefOf` or capability bits. | |
| Names inside packages | Kept as a lazy path with its scope, resolved when the element is read, at most 8 indirections, then `name_resolution_loop`. Resolving at creation would make the result depend on the order tables load in. | [§19.6.102] |
| Package size mismatches | Fewer initialisers than `NumElements`: padded with Uninitialized. More: truncated. Both counted. | [§19.6.102] |
| Orphan data and `Noop` at table level | Evaluated and dropped. | [§5.4.2] |
| Objects a method creates | Temporary: removed at the method's exit, even when created with a full path elsewhere. A second creation of the same name in the same frame (a `Name` inside a `While` body) is an error. | [§5.5.2.3], [§19.6.150] |
| `Load` | From a SystemMemory region or a field that holds the whole table: the table is copied (§3.5), its checksum computed on the copy, its objects created relative to the root; True goes to its target and is its result. A table already loaded, compared over its whole length as ACPICA does, is refused: where ACPICA fails the evaluation, `Load` gives False. | [§19.6.77] |
| `LoadTable` | Finds the first table of the directory by signature and OEM identifiers, each compared with its field padded with zeros, an empty one matching any, as ACPICA does, with an empty RootPath, ParameterPath and ParameterData only, and runs it in place; True, or False when none matches or it is loaded already. | [§19.6.78] |

### 5.2 Methods and values

| Topic | Rule | ACPI |
|---|---|---|
| Calling convention | Up to 7 arguments, 8 locals that start Uninitialized; arguments are shared, not copied, and become writable only through a reference. A host-initiated evaluation passes exactly `argc` arguments. | [§5.5.2.1], [§5.5.2.2] |
| Implicit return | A method that ends without `Return`, whose caller uses the result, returns Integer 0, counted. ACPI leaves the result undefined; firmware tested on Windows expects 0. | [§19.6.120] |
| Uninitialized operands | Reading an uninitialized `LocalX` or `ArgX` as an operand gives Integer 0, counted. An uninitialized package element is an error. | |
| `Serialized` | A sync-level check: calling a Serialized method whose SyncLevel is below the current level fails with `mutex_order`, and the same evaluation may re-enter it. No mutex object exists, because one owner runs AML (§3.7). | [§19.6.85] |
| Mutexes | `Acquire` returns 0 when the evaluation holds the mutex; the owner may nest; an `Acquire` below the current sync level is ignored, counted, and gives Ones, as uACPI answers it, while an `Acquire (\_GL)` whose global lock the firmware never releases fails (§6.2); a `Release` is accepted out of order when the mutex has the SyncLevel of the one acquired last, and a `Release` of a mutex not held, or of another level, is ignored and counted, as uACPI does; everything held is released when the evaluation ends. | [§19.6.2], [§19.6.89] |
| Events | `Signal` counts up, `Reset` clears, and `Wait` consumes a pending signal or reports a timeout at once, since nothing else can signal while the only AML owner waits. | [§19.6.125], [§19.6.149] |
| Implicit source conversion | Operands are converted to the type the operator needs: Buffer to Integer from its first 4 or 8 bytes, String to Integer as hexadecimal up to the first non-hex character, a `0x` prefix accepted and an empty string giving 0; an Integer that `Mid` takes becomes a Buffer of 4 or 8 bytes, as ACPICA converts it; `SizeOf` refuses an Integer, as uACPI does, where ACPICA gives 4 or 8. An impossible conversion fails the evaluation. The cases ACPI forbids are counted (`forbidden_conversions`). | [§19.3.5.4], [§19.3.5.7] |
| Mixed-type comparisons and `Concatenate` | The second operand is converted to the first's type, as ACPICA converts it: an Integer to a String of 8 or 16 hexadecimal digits, a Buffer to a String of `0x` and two digits per byte separated by spaces, a String to a Buffer with its NUL; `mixed_type_comparisons` counts it. | [§19.3.5.4] |
| Storing | `Store` to a named Integer, String or Buffer copies the source's bytes into it and keeps its size, the rest zeroed and what does not fit dropped, as Windows does: a String takes an Integer's bytes, not its digits. A named Package takes a copy of a Package and refuses any other value, and a named object that holds no data, such as a Device, refuses any store. `LocalX` is overwritten with a copy, unless it holds a reference to data, which then takes the value in place, as Windows does (ACPICA overwrites the reference): a named object as a store to it would, a local, an argument or an element of a computational type keeping its type and size; a reference stored to it, or a value stored while its reference leads, through the names package elements hold, to nothing or to an object that holds no data, replaces the reference, as firmware that stores `CondRefOf` twice to `Local0` expects; an `ArgX` that holds a reference has its target overwritten, unless the target holds no data; fields, buffer fields and buffer indices are always written through, a buffer index taking the last byte of a String or a Buffer, as Windows does (ACPICA takes the first), and a buffer field its own Buffer as it was before the store. `CopyObject` replaces the target's type, except for fields; to a name that holds a reference, it replaces what the reference refers to, but a reference to a package element is itself replaced, as Windows does. A store to a constant does nothing. | [§19.3.5.8] |
| References | `Index` on a package refers to the slot, not to a copy, and a store to it replaces the element, unless the element is a reference, which is followed; `DerefOf` follows the chain to its bottom, and reads the object a String names; `Increment` and `Decrement` work in place through references and fields; `CondRefOf` of a missing name is False and stores nothing. A reference to a local, an argument or a name a method created cannot outlive its frame: the return path copies, and a reference whose frame is gone fails with `name_not_found` when read, and is replaced when stored to (Storing), where ACPICA reads what took the name's place. The locals of a table's code, and the references to them, live from one of its statements to the next. | [§19.6.63], [§19.6.30], [§19.6.14] |
| Logical results | True is Ones, masked to the width, and false is 0, for every logical operator; a predicate is true when its integer is not 0. | [§19.6.69] |
| `LAnd` and `LOr` on buffers | Read the first 4 bytes, as Windows does. | |
| Shifts and division | A shift by the integer width or more gives 0; division or modulo by zero fails the evaluation. | [§19.6.123], [§19.6.32] |
| `CreateField` reads | A field made by `CreateField` reads as a Buffer; the fixed-width `Create*Field` read as Integers. A field of length 0 is valid. Bounds are computed in 64 bits. | [§19.6.21] |
| `Revision` | Decoded only: fails with `unimplemented_operator`. | [§19.6.121] |
| `Fatal`, `BreakPoint`, `Debug` | `Fatal` logs and counts, then execution continues; `BreakPoint` counts; stores to `Debug` are logged at debug level and compiled out of release images. | [§19.6.47], [§19.6.9], [§19.6.26] |

### 5.3 Regions and fields

| Topic | Rule | ACPI |
|---|---|---|
| The field datum engine | One access per datum of the access width; read-modify-write by UpdateRule; a unit no wider than the integer width reads as an Integer, a wider one as a Buffer. | [§19.6.48] |
| Strings and buffers stored to a field | Written in field-sized chunks, as Windows does. | |
| Region bounds | An access must fit its region; rounding the region's length up to the access width is allowed and counted. | [§19.6.48] |
| Region lifetime | Attached at the first access, not at declaration: handler chosen, PCI address resolved, mapping taken from the cache. | |
| `_REG` | `_REG(PCI_Config, 1)` during initialisation (§3.8 step 3), for the scopes of a table loaded at run time once its loading evaluation ends, and at once for a PCI_Config region a method creates once step 3 has run, unless that `_REG` is already running, however it was called (ACPICA and uACPI call it again, and the second creation fails). Only the `_REG` of the region's own scope is called, never one above it, as ACPICA and uACPI find it; one that does not take two arguments is not called, and counted as failed. None for SystemMemory and SystemIO (§1.4). The EC and GPIO get theirs with their handlers. | [§6.5.4] |
| Port protection | Reads of a protected port return 0 and writes are dropped, counted, never an error. Always protected: the PIC (`0x20-0x21`, `0xA0-0xA1`), the ELCR (`0x4D0-0x4D1`) and PCI mechanism #1 (`0xCF8-0xCFF`). Once AML has asked `_OSI` for Windows XP or newer, also the DMA, PIT, RTC and CMOS, page and arbitration ports: the ranges Windows' ACPI driver protects ([uACPI's table][uacpi-ports]). Only the first port of an access is checked; port `0xB2` (SMI command) stays allowed. The kernel drives the PIC and PCI itself, and AML must not race it; the PIT, the RTC and CMOS follow once `_OSI` asks, as on Windows, and the other ports the kernel drives, COM1 and the keyboard controller, are not protected. | |
| PCI header protection | Writes below the subsystem vendor ID (`0x2C`), and from the expansion ROM to the interrupt line (`0x30-0x3B`), of a type-0 header, and to the whole header of other types, are dropped and counted. The header type is read once, at attach, and only the first byte of an access is checked, as Windows does. | |
| Addresses and mappings | A region above 4 GiB fails on i686, never truncated. An access maps only the page it touches, as one entry of the mapping cache, never the declared region; a datum at an address its width does not divide is made byte by byte, counted, so that no access crosses a page or reads unaligned. The host's `memory_type` decides the kind: RAM, ACPI NVS or reclaim memory is mapped cached read-write, anything else uncached, since an uncached alias of a page mapped cached elsewhere, by the kernel's own map of memory or by the firmware window, is a memory-type conflict. That map holds only RAM, but in the first MiB, which it keeps write-back as Linux does: a window there takes that type, and the MTRRs decide. Two windows on one reserved page, a firmware window and an uncached one, can still give it two types (#624). | [§5.5.2.4.1] |
| 64-bit data on i686 | A `QWordAcc` datum is two 32-bit host accesses, low then high, which the field engine makes itself; one access on x86-64. The simulated host sees the same order. `split_qword_accesses` counts them. | |

### 5.4 The Windows identity real firmware expects

| Object | Answer | ACPI |
|---|---|---|
| `\_OSI` | A native method. TRUE for every Windows string, from "Windows 2000" to the newest Microsoft defines. FALSE for "Linux", "Darwin", "FreeBSD", "HP-UX", "OpenVMS", the vendor `Linux-*` strings and every feature string, "Processor Aggregator Device", "3.0 Thermal Model" and "Extended Address Space Descriptor" included. The newest Windows string asked is recorded: it drives port protection (§5.3). Firmware tests mostly the newest Windows path, and loads less when told "Linux". | [§5.7.2] |
| `\_OS` | `"Microsoft Windows NT"`: `_OSI` ladders fall back on it. | [§5.7.3] |
| `\_REV` | 2, as every other implementation answers. | [§5.7.4] |
| `\_GL` | A predefined mutex. `Acquire (\_GL)` takes the AML mutex, then the global lock as a `Lock` field does (§4.4). Without a FACS, or on a hardware-reduced platform, which has no global lock ([§3.11.1]), acquiring `\_GL` takes only the AML mutex. | [§5.7.1], [§5.2.10.1] |
| Predefined scopes | `\`, `\_GPE`, `\_PR_`, `\_SB_`, `\_SI_`, `\_TZ_`; `\_PR_` is deprecated, but servers still reference it. | [§5.3.1] |

`\_OSI` exists as a node, never only as a special case in the call path: firmware reaches it through
`CondRefOf (\_OSI)` first.

### 5.5 Time

| Topic | Rule | ACPI |
|---|---|---|
| `Sleep` | Gives up the processor for the requested milliseconds (at boot, a halt until a tick deadline, §3.3), with no 2 s cap. The request is charged to the evaluation's time budget first (§6.1), and a request that would exceed it fails the evaluation before sleeping. Real firmware asks for 3 s. | [§19.6.127] |
| `Stall` | Busy-waits up to 255 µs, on the power management timer (§3.3); above, it becomes a sleep of the rounded-up milliseconds; both counted beyond 100 µs. The microseconds waited are counted next to those asked, so the boot time lost shows. | [§19.6.129] |
| `Timer` | 100 ns units, monotonic: the kernel tick in ring 0; in parity runs the virtual clock, which also advances by 100 µs on each read (§2). | [§19.6.136] |

---

## 6. Robustness

### 6.1 Limits

Every limit is a counter compared with a cap, reported on the `[LPLTLM] acpi` line with its peak, and has one test
that reaches it. The corpus runs report the peaks.

| Limit | Cap |
|---|---|
| Frames per evaluation, internal frames included | 64 |
| Blocks per evaluation | 512 |
| Pending operators | 256 |
| Values | 1,024 |
| Temporary nodes per evaluation | 1,024 |
| Nested internal (host-triggered) evaluations | 2 |
| Loop iterations per evaluation, every `While` back-edge | 2^20 |
| Field accesses per evaluation: each access the field engine makes to a region (a datum, each half of a QWordAcc datum, each datum of an index or bank register, the read of a read-modify-write; an unaligned datum counts once), and each read of the global lock word while taking it | 2^20 |
| Time requested per evaluation (`Sleep` milliseconds and `Stall` microseconds) | 30 s |
| Package elements | 255 for `Package` ([§19.3.5]), 4,096 for `VarPackage` |
| Buffer, string, field read as a buffer | 64 KiB |
| Name indirections in packages | 8 |
| Packages nested in one data object | 32; a deeper one waits for the interpreter |
| Runs of the object heap | 256: one for each object of 4 KiB or more, the others sharing runs of 64 KiB |
| Namespace nodes | 65,536 on the server profile, 32,768 on the others |
| Namespace arena | 8 MiB on the server profile, 4 MiB on the others |
| Loaded tables; bytes copied by `Load` | 128; 256 KiB |
| Held mutexes per evaluation | 32 |
| Notify ring | 64 entries; an overflow is dropped and counted |
| States one CPU slot publishes | 7, as Linux, whose table of `ACPI_PROCESSOR_MAX_POWER` (8) keeps its first entry for C0; the states after them are counted, not read |
| Region mapping cache | 64 entries, each the pages one access touched |

A table load is one evaluation: its table-level code and the methods it calls share these limits. The counts of
back-edges, field accesses and requested time start again from zero with each evaluation, a table load or one the
kernel asks for (§3.8); a `Load` that a method runs belongs to that method's evaluation, and resets nothing. The node and arena caps depend on the profile, and probe counts
follow the hash capacity. A parity run passes the target's caps to both sides and prints them in the record header
(§8.7).

### 6.2 What happens on bad AML

The kernel never panics, traps or meets C undefined behaviour because of AML.

| Fault | In a method | At definition-block level | Status |
|---|---|---|---|
| PkgLength crossing its parent | the evaluation fails | the rest of the table is abandoned, its objects kept, the next table loads | `pkglength_out_of_parent` |
| Truncated operand, bad NameString | fails | the innermost enclosing construct is skipped | `truncated` |
| Invalid or unimplemented opcode | fails | construct skipped | `invalid_opcode`, `unimplemented_operator` |
| Name not found when executed | fails | construct skipped | `name_not_found` |
| A method creates a name that exists, as a `Name` in a loop | fails | a duplicate: skipped and counted (§5.1) | `name_exists`, `duplicates` |
| `Return` outside a method | | fails, the statement skipped | `invalid_opcode` |
| Wrong type, impossible conversion | fails ([§19.3.5.4]) | construct skipped | `type_mismatch` |
| Division by zero | fails | construct skipped | `divide_by_zero` |
| Index or buffer field out of range, or an index or BankValue its register cannot hold | fails | construct skipped | `index_out_of_range` |
| Reference cycle | fails | construct skipped | `reference_cycle` |
| A limit of §6.1 | fails with the limit's name | construct skipped | `limit_<name>` |
| Region without handler, out of bounds, or unreachable (above 4 GiB on i686, on physical page 0, where the kernel maps no window, a PCI offset from 0x100 or a PCI segment other than 0 before #58) | fails | construct skipped | `space_unsupported`, `region_bounds`, `address_unreachable` |
| A field of a region whose offset and length were never evaluated: declared in a branch that did not run, or its operands failed; or of a PCI_Config region whose attach needs a method where no frame can wait (§3.6) | fails | construct skipped | `region_unevaluated` |
| A reserved AccessType, or a QWordAcc datum of SystemIO or PCI_Config | fails | construct skipped | `access_width_invalid` |
| A `Lock` field or `\_GL` whose global lock the firmware does not release in time | fails | construct skipped | `global_lock_unavailable` |
| A `Load` or `LoadTable` form outside §1.4 | fails | construct skipped | `load_form_unsupported` |
| Protected port or PCI header | not a fault: zero read, write dropped | same | counted only |
| A PCI function the attach cannot reach (§4.4) | not a fault: all ones read, write dropped | same | counted only |
| Arena exhausted | fails | the table load stops | `out_of_memory` |

At definition-block level, "construct skipped" means: a statement that fails to run is skipped, a predicate that
fails declares both branches without running them (§3.6), and a fault in the declaration pass's own decoding skips
the innermost enclosing PkgLength construct.

Every failure unwinds the evaluation completely: temporaries removed, mutexes released, values dropped, the context
reset. The caller receives the typed status and decides.

### 6.3 Hostile cases every build survives

Each is a test:

- an `If (Zero)` whose PkgLength passes the end of its method fails with `pkglength_out_of_parent`, and the next
  evaluation runs;
- after a method truncated in the middle of an operator, the object heap holds what it held before;
- `VarPackage (0xFFFFFFFF)` fails with `limit_package_elements`, on i686 and on the host;
- `CreateField (B, 0xFFFFFFF0, 0x20, F)` fails with `index_out_of_range`;
- a reference count driven past `0xFFFFFFFF` saturates, and the object is never freed;
- an unterminated string fails with `truncated`; the firmware window is read-only, so any write would fault the
  test;
- buffer fields read and write exactly at every bit offset (§8.2);
- `Return (RefOf (Local0))` gives the caller a usable value;
- `Not`, `FindSetLeftBit` and `FindSetRightBit` work with a target and without;
- after `Return`, the method's `Name` no longer resolves;
- an `_ADR` method that stores, reached from a PCI_Config write, completes.

---

## 7. Performance

### 7.1 Where the cost is

AML never runs on the idle path or per tick: `_CST` is evaluated once, then only on Notify 0x81, and the idle path
reads a table in kernel memory. The costs to cut are:

- **the load:** up to 1.7 MiB of AML and 33,600 named objects on the largest machine;
- **the boot evaluations:** `_INI` and `_STA` over thousands of devices, `_OSC`, and `_CST` for up to 224 CPUs;
- **the heap and the kernel stack,** which the World shares.

### 7.2 Optimisations adopted

| # | What | Proof: a deterministic counter or a window |
|---|---|---|
| O1 | Execute in place from the firmware window; never copy a method body | a method declaration allocates 0 (`allocations` at load) |
| O2 | Skip method bodies at load by their PkgLength | `bytes_decoded_at_load` / `aml_bytes` |
| O3 | Integers by value; constants, `LocalX` and `ArgX` pushed without an operator record | `allocations` = 0 for an integer-only method |
| O4 | One evaluation context, allocated once, never zeroed per evaluation | `allocations` per evaluation |
| O5 | One hash for the whole namespace, O(1) per probe; an upward search costs at most its depth in probes | `lookup_probes` / `lookups` |
| O6 | Direct-indexed decode tables, operand kinds packed in a `uint32_t`, one `switch` of handlers | W4 |
| O7 | `LNot` and a comparison fused at decode | `fused_comparisons` |
| O8 | `DerefOf (Index (x, i))` read without building a reference | `references_built` |
| O10 | One allocation per string, buffer or package: header and payload | `allocations` |
| O11 | Lazy region attach, and a mapping cache keyed by physical page range: one map per distinct page range per boot | `map_calls`, `mapping_cache_hits` |
| O12 | A field fast path for a unit inside one aligned access | `field_fast_path` / `field_accesses` |
| O13 | The PCI address resolved once, at attach: no method evaluation per PCI field access. It assumes the kernel never renumbers bridges; hotplug or a bus reassignment brings back a per-access resolution | `internal_evaluations` |
| O14 | No `_STA` in subtrees without `_INI` | `status_evaluations` |
| O15 | No lock and no atomic in the interpreter (§3.7) | CI checks that the release objects of `kernel/kernel/acpi/` hold no `lock`-prefixed instruction (`objdump -d`); W1 on the release kernel |
| O16 | Log calls compiled out below the build's level | CI checks that the release objects call no log function below the release level (`nm`); W1 on the release kernel |

### 7.3 Candidates, adopted only when a count asks for them

| # | What | Adopt when |
|---|---|---|
| C1 | Cache each call site's resolved node under a namespace generation number | the lookup function's share of W2, as `instructions.sh` lists it per function, exceeds 10 % |
| C2 | Keep a `Name`'s constant package or buffer as AML until its first read | the package-building function's share of W1 exceeds 25 % on a corpus machine |
| C3 | Pre-decode the body of a method that loops | the functions of a `While` body dominate a window |
| C4 | Build a package of constants without the general evaluator | as C2, with a differential test against the general path |
| C5 | Zero-copy string and buffer literals, kept in the table until written, materialised first when a buffer field, an `Index` or `RefOf` reference or a store target is made over one (§3.5) | the needs-path run shows that most literals are never materialised, and the copy function's share of W1 or W2 exceeds 5 % |

### 7.4 Measurement windows and budgets

Counts come from `tools/instructions.sh` on QEMU's TCG with `-icount`, the same on every run and every machine.
Budgets follow the format of `tools/server-tick.budget`, one file per window and build. Two builds, because a
`KERNEL_TEST` exists only in a debug kernel:

- **release** (`SMOKE_TESTS=0 ./build.sh --server`): the windows that run at boot, on QEMU's live tables;
- **debug** (the poison build): the windows inside a `KERNEL_TEST`, among them the ring-0 corpus test on three
  corpus machines (a revision-2 Intel client with `Load`, an AMD client, a server), so that real firmware sets
  budgets next to QEMU. Their files end in `.debug.budget`.

| Window | Command | Budget files | First target |
|---|---|---|---|
| W1 load of the boot tables | release: `tools/instructions.sh --inside advanced_configuration_and_power_interface_machine_language_load_directory --times 1 --machine q35 kernel/lpl.kernel`, every pass of the load; debug: the same `--inside` during the ring-0 corpus test | `tools/acpi-load.budget`; `tools/acpi-load-<machine>.debug.budget` | at most 300 instructions per named object created; 0 allocations per method declared |
| W2 initialisation (§3.8, steps 2 to 9; the switch and the load run before it, the load measured by W1) | release: `--inside advanced_configuration_and_power_interface_initialize_namespace --times 1` on `q35`; debug: the same during the ring-0 corpus test | `tools/acpi-initialization.budget`; `tools/acpi-initialization-<machine>.debug.budget` | its first measurement |
| W3 `_CST`: two made-up processors (Intel's pattern, a Serialized method patching a package of buffers from NVS fields; AMD's static package) and the three corpus machines | debug: `--inside kernel_test_body_<test>`; release: through `-acpitable`, if QEMU places the table where the kernel finds it | `tools/acpi-cst.debug.budget` | its first measurement |
| W4 microbenchmarks: `While (LLess (Local0, 0x03E8)) { Increment (Local0) }`; 1,000 lookups in a 3,346-child scope; the same loop under a revision-1 DSDT | debug: `--inside kernel_test_body_<test>` | `tools/acpi-microbenchmark.debug.budget` | at most 200 instructions per loop iteration, 0 allocations |

`_PRT` gets a window once #59 gives it a boot consumer.

The first measurement of each window becomes its budget, with a tolerance of 1 %. A change that moves a count past
it either explains the gain it bought or is red. Counters (allocations, lookups, probes, bytes decoded, opcodes
executed, map calls) are deterministic, so they enter the parity fold too (§8.7): the host and ring 0 must do the
same work, not only reach the same result.

---

## 8. Testing and oracles

### 8.1 The layers

| Layer | What it checks | Where it runs | Oracle |
|---|---|---|---|
| Made-up AML | each rule of §4 to §6, each limit, each case of §6.3 | ring 0 (`KERNEL_TEST`) and the host harness, from the same sources | the test's own expected values; parity between the two |
| Real firmware on the host | the corpus (§8.4) | host | ACPICA's `acpiexec`; the census counts; the `iasl` disassembly; the decoded table lines for the static tables |
| Real firmware in ring 0 | one corpus machine per boot, its tables given as a Multiboot module, run through the simulated host | ring 0, debug kernel | the host harness's records on the same bytes, compared by `tools/parity.sh` |
| QEMU's live tables | the boot path: ACPI mode, load, initialisation | ring 0, on `pc` and `q35` | statuses and counters; the load's folds (namespace, opcodes decoded), against the host's on the printed bytes (§8.7) |
| QEMU's reference tables | 42 `pc` and `q35` configurations, 5 `microvm` | host | `acpiexec` |
| uACPI's test cases | Windows behaviours and edge cases | host | each case's `// Expect:` line |
| Real machines | the full path to `_CST` | ring 0 | the same machine's tables through the host harness and `acpiexec`; Linux's cpuidle states |

Test data under another licence is fetched by pinned commit into `build/`, never committed, and never put in a
published image. The made-up ASL is ours, under MIT.

### 8.2 Made-up tables

- Written in ASL, compiled with the pinned `iasl` in a sandbox, committed as the `.asl` source and a generated C
  array; `tools/acpi/regenerate-fixtures.sh` recompiles them and fails on a byte difference.
- Raw-byte cases for what `iasl` does not emit: a crossing PkgLength, orphan `Package` terms at the root, `Noop`
  runs, a 4-byte PkgLength, both forms of `CondRefOf` and `Load`, an unterminated string.
- Region tests build their AML at run time, with the physical address of a frame the test owns patched into the
  region's base.
- Bit-exact field tests: every bit offset from 0 to 63 and length from 1 to 64, for buffer fields and for each
  access width of a SystemMemory field, read and write, on the host. Their expected values come from an independent
  bit model, checked once against `acpiexec`. Ring 0 runs a fixed stratified sample (each width, the boundary
  offsets, a few hundred cases), which the host folds too.
- Behaviour tables modelled on real firmware without copying its bytes: an `_OSI` ladder that sets an OS year; an
  Intel-style `GCAP` that `Load`s its HWP table from an NVS region only when the platform `_OSC` grants bit 6; an
  Intel-style `_CST` that patches a package of buffers; an AMD-style static `_CST`; an AMD-style `_PIC` that
  rewrites "PIRQ" fields when its argument is 1; a `_STA` that divides by zero; the `_STA` combinations ACPI
  declares invalid; a loaded table that declares a PCI_Config region with a `_REG` in its scope; a FADT whose
  SCI_EN starts clear; a self-recursive method; an endless loop that sleeps 60 s per turn; an 86-deep `ElseIf`
  ladder; a `Switch` on `Local0` at the root.

### 8.3 QEMU

- **Live boot.** `./qemu.sh --server` on `-machine q35` and `pc`: the kernel switches to ACPI mode, loads what QEMU
  generates, runs the initialisation and prints its counters and statuses. A debug kernel can also print each table
  as hex lines on COM1; `tools/acpi/host` reads them back from `serial.log`, so the host harness loads exactly the
  bytes ring 0 saw.
- **Reference tables** (`tests/data/acpi/x86` at v10.2.1): 42 configurations, and 5 `microvm` ones (revision-2
  DSDT, hardware-reduced FADT). They test the namespace, not the table walk.
- **What QEMU cannot test**, and the corpus and the made-up tables test instead: `_CST`, `_PSS`, `_CPC`, `_LPI`,
  thermal zones, `_OSI`, `Sleep`, `Stall`, `Timer`, `Load`, `LoadTable`, table-level code, the EC, and 64-bit
  integers on `pc` and `q35`.
- No QEMU byte is committed: its tables are GPL-2.0-or-later.

### 8.4 Real firmware: linuxhw/ACPI

- `tools/acpi-corpus.sh fetch` clones [linuxhw/ACPI](https://github.com/linuxhw/ACPI/tree/d6d30c6ef6f45506567f25a302c64f009d8f5f4e)
  blobless at the pinned commit, and checks out only the listed reports into `build/acpi-corpus/`. It prints the
  attribution: "ACPI dumps collected by hw-probe users at linux-hardware.org, published by linuxhw under CC-BY-4.0;
  changed: converted from acpidump text to binary tables".
- **Three lists.** *Quick*, 9 machines chosen for their features (Dell Precision 5680, MSI MS-7E16, ASUS
  NUC14RVH-B, HP Pavilion x2 Detachable, Microsoft Surface Pro, Lenovo IdeaPad Z470, Valve Jupiter, Intel
  ArcherCity, HP ProLiant DL360 G5), runs on every pull request that touches the interpreter. *Corpus*, 114
  distinct machines, desktops, notebooks, tablets and servers of every FADT revision from 1.0 to 6.5, runs to close
  an issue. *Full*, every report with a binary dump, runs on demand. No list names a machine the project works on.
- **A dump holds neither the root pointer nor the root tables**, and every address it prints is 0, but the FADT
  keeps the addresses it names. The harness serves the DSDT at each of them, the FACS where the FADT names it, every
  other table at an address of its own, and writes a root pointer and an XSDT that list them in the dump's order, but for the SSDTs
  that `Load` places (below). The directory read from it is compared with one `tools/acpi/expected_directory.py`
  reads from the same dump, apart from the kernel's code.
- **For each machine** the harness prints the decode errors, the namespace dump and its fold, the census counts,
  the needs-path closure, and the needs-path run (§3.8, steps 0 to 9) with the opcodes, spaces and access types it
  executed.
- **The static tables**: the parsed FADT, MADT, MCFG and HPET fields of every machine equal the decoded `[xxxh]`
  lines that ship with each dump. The FADT also equals the local FADT ACPICA builds from the same bytes
  (`tools/acpi/acpica_fadt.c`), with ACPICA's rewrite of PM block widths (`AcpiGbl_UseDefaultRegisterWidths`)
  turned off, since the kernel keeps the declared width (§3.9); the reset register is read only with RESET_REG_SUP,
  as `AcpiReset` does, and a GPE block's length only with its address, as `AcpiEvGpeInitialize` does. Left out are
  the length and the revision, which ACPICA replaces with its structure's size and keeps as written; the GPE block
  widths, which neither reads; and, on a hardware-reduced FADT, the fields ACPI tells OSPM to ignore, which ACPICA
  keeps.
- **Ring 0**: `tools/acpi-corpus.sh module <machine>` writes that machine's tables as one Multiboot module. A debug
  kernel booted with it runs a `KERNEL_TEST` that loads the tables through the simulated host and prints the same
  records as the host harness; without a module the test is skipped, and `tools/acpi-corpus.sh ring0`, which passes
  one, names it on `lpl.test.require`. Modules never enter a release or site ISO. The quick list runs this way, so
  the 64-bit integer paths, the 4-byte-pointer layouts and the arena limits meet real AML in ring 0.
- **The census oracle**: every opcode count of every machine equals that of the census script, the tables loaded
  declaring every branch of table-level code and running none of it, as the census reads them; running it, which
  declares only the branches taken, is checked against `acpiexec` instead, on the machines whose table-level code
  runs without a failure: `namespace corpus` names how many it leaves out. Per table, the declaration counts are
  compared with the `iasl` disassembly shipped with each dump; they differ where `iasl` guessed the argument counts
  of methods it could not resolve, left an object out, or where the report's disassembly is cut short, and each
  such table is listed.
- **The ACPICA oracle**: `acpixtract -a` splits the same dump; `acpiexec` loads it with the same fill as our
  simulated regions (`-fv`) and the same `_OSI` answers; both namespace dumps are normalised to `path type value`
  lines and compared path by path, without the nodes ACPICA creates for `External`.
- **`Load` on the corpus.** Dumps hold the dynamic SSDTs, every table's address printed as 0. In a table that holds a
  `Load`, the harness finds the (label, address, length) triples that Intel's `PmRef` code reads, and the SystemMemory
  regions of constant address and length that a `Load` names; each places at that address the SSDT of that length whose
  OEM table ID the label names, case apart, else the only SSDT of that length (`tools/acpi/loaded_tables.py` computes
  the same apart from the harness). A placed table is left out of the XSDT; the views compared with the census or with
  `acpiexec`'s load declare it statically after the SSDTs, as `acpiexec` does. `tools/acpi-corpus.sh load` gives
  `acpiexec` statically the tables an initialisation ended with, `Load`'s included, and compares the two namespaces as
  sorted path sets. The values firmware writes to NVS at boot, absent from dumps, come from one preset file of the
  harness, `tools/acpi/presets.txt`, so that the `Load` and `_CST` branches run: `CFGD = 0x0076F6FF`, the static `Name
  (CFGD, 0x0076F6FF)` of the IdeaPad Z470's CPU SSDT, on the 44 machines whose `CFGD` is an NVS field. A `Load` whose
  address firmware computes in a method, or whose `PmRef` package names a table the dump lacks, reads no table and fails
  as `region_bounds`, which ends the evaluation that runs it, as ACPICA's does: an artifact of the corpus.

### 8.5 uACPI's test cases

- The 105 ASL files of [uACPI's `tests/test-cases`](https://github.com/uACPI/uACPI/tree/c0edecef62bcac6ce3f783540099190bafadadda/tests/test-cases),
  fetched by commit and compiled with `iasl` in the sandbox.
- Each starts with `// Name:` and `// Expect: <int|str> => <value>`; the harness initialises the namespace (§3.8), as uACPI's runner does, then evaluates `\MAIN` and compares.
  `check-*` expectations, which test uACPI's own API, are skipped and listed.
- `tools/acpi-corpus.sh fetch uacpi` fetches them and `tools/acpi-corpus.sh uacpi` runs them, with
  `test-acpi --evaluate`. A case passes; or diverges, listed in `tools/acpi/uacpi-divergences.list` with the section
  here that decides otherwise; or waits, for an opcode the kernel decodes but does not run yet or a field access,
  named on its line, or for the issue the list names. A listed case that passes fails the run.

### 8.6 ACPICA on the host

Built in a sandbox from a clean checkout at the pinned commit:

```sh
make iasl acpiexec acpixtract                  # binaries in generate/unix/bin/

acpixtract -a dump.txt                         # acpidump text -> one .dat per table
acpiexec -l -di -b "namespace" dsdt.dat ssdt*.dat          # load only: no _STA/_INI
acpiexec -b "namespace" dsdt.dat ssdt*.dat                 # after _REG/_STA/_INI
acpiexec -dr -fv 0 -b "evaluate \_SB.PCI0._STA" dsdt.dat ssdt*.dat   # raw results, zero-filled regions
acpiexec -dr -fi nvs.txt -b "evaluate \_PR.CPU0._CST" dsdt.dat ssdt*.dat   # presets.txt without its # lines
acpiexec -es ...                               # slack mode: implicit return, uninitialised locals = 0
acpiexec -to 5 -te ...                         # loop timeout 5 s, exit the loop instead of aborting
iasl -oa -r 1 test.asl                         # no AML optimisation; revision 1 = 32-bit integers
iasl -e ssdt*.dat -d dsdt.dat                  # disassemble with external argument counts
```

A needs-path result that differs between `acpiexec` and `acpiexec -es` marks a machine that relies on slack
behaviour, and reopens the implicit-return and uninitialised-operand rules of §5.2. Methods that read `Timer` are
compared on status only.

### 8.7 The parity record

The host and ring 0 run the same made-up tables and the same corpus bytes through the same simulated host (§3.3),
and print the same records, which `tools/parity.sh` compares (`# <suite>.<test>: key=value`). Each record header
prints the target parameters both sides used (physical address width, profile caps), so that a region above 4 GiB
or a probe count that depends on the hash capacity differs only when the parameters do. Of a QEMU live boot,
only the load is folded, its namespace and the opcodes it decoded, which the host computes again from the bytes
ring 0 printed (§8.3); its evaluations read emulated hardware the simulated host does not model, and are not
folded. FNV-1a, 32 bits, offset `0x811C9DC5`, prime `0x01000193`, over little-endian bytes:

| Record | Folds, in order |
|---|---|
| `namespace` | a depth-first walk in declaration order; per node: depth `u8`, name `u32`, type `u8`, then Integer `u64`; String or Buffer `u32` length and bytes; Package `u32` count and each element the same way (a lazy name as its path bytes); Method flags `u8` and body length `u32`; Region space `u8`, offset `u64`, length `u64`; field unit kind, bit offset `u32`, bit length `u32`, access, update `u8`; Processor id `u8`, block address `u32`, block length `u8` |
| `evaluations` | per evaluated path in a fixed list: status `u16`, then the result as above |
| `work` | opcodes executed per opcode (`u32` each, in descriptor order), lookups, probes, allocations, bytes decoded, map calls |

Never folded: node indices, addresses, arena occupancy, page counts, anything `size_t`. Controls that must come out
different, each a test: the same table under the other integer width; the `_OSI` ladder with "Linux" answered TRUE;
a `_CST` table with one latency changed by one.

### 8.8 Real machines

- A release server kernel and a debug kernel boot on each class of available hardware (desktop, notebook, mini PC,
  server) and print `[LPLTLM] acpi` and the per-CPU `_CST` table. Recent PCs boot UEFI only and may have no RSDP
  below 1 MiB, so this needs #278 and #279 first.
- Under Linux on the same machine, `acpidump` goes through the host harness and `acpiexec`; the `_CST` packages
  match ours in structure (state count, register kinds, types). Latency and power come from NVS values a dump does
  not hold, so they are compared with `/sys/devices/system/cpu/cpu0/cpuidle`, when
  `/sys/devices/system/cpu/cpuidle/current_driver` says Linux took its states from ACPI.
- The machines are named in no public document.

### 8.9 Failure first

Each issue lists its mutants: deliberately wrong builds that its tests must turn red before it is trusted. A mutant
that stays green is a test to write, not a pass.

---

## 9. Credits

The interpreter copies no code. These projects are its oracles, and the sources of ideas it reimplements.

| Source | Licence | Ideas taken, and use |
|---|---|---|
| **uACPI**, Daniil Tatianin and contributors, [c0edece](https://github.com/uACPI/uACPI/tree/c0edecef62bcac6ce3f783540099190bafadadda) (7.0.0) | MIT | a table load as a frame; explicit frames for calls and `Load`; lazy package names; lazy region attach; the Windows port and PCI-header protection; its catalogue of Windows behaviours (store rules, AnyAcc, chunked field stores, `LAnd` on 4 bytes, `Unload` refused); the cycle check at store time; the saturating count; its 105 test cases |
| **ACPICA**, © 1999-2026 Intel Corporation and contributors, [e85aa3b](https://github.com/acpica/acpica/tree/e85aa3bebaae81ed1e6d163511ac7e1eafe732cf) (20260930) | BSD-3-Clause or GPL-2.0-only | direct-indexed decode tables; packed argument kinds; a single-pass load that runs module-level code; method bodies skipped at load; the integer width from the DSDT; the `_OSI`, `_OS` and `_REV` policy; the bound of the switch to ACPI mode; the initialisation order; the field datum engine; the `_INI` pre-scan; the FACS preference; `acpiexec` and `iasl` as oracles |
| **LAI**, the lai authors (managarm), [b481a42](https://github.com/managarm/lai/tree/b481a42d2af53d9bd7d41195d354bb703b40ab07) | MIT | integers by value; bodies run in place; the program counter committed per whole opcode; `_OSI` as a native node |
| **rust-osdev/acpi**, Isaac Woods and contributors, [a69a4df](https://github.com/rust-osdev/acpi/tree/a69a4dfc3ec55c582901565aef1da7a2a4f623e0) | MIT or Apache-2.0 | a named bound with a test for every unbounded thing; the indirection limit for package names; typed errors; raw-byte tests |
| **ACPI Specification 6.6**, UEFI Forum | © UEFI Forum; quoted, never copied | the grammar, the semantics, the objects |
| **QEMU** 10.2.1, [tests/data/acpi](https://github.com/qemu/qemu/tree/v10.2.1/tests/data/acpi) | GPL-2.0-or-later | the test bench |
| **linuxhw/ACPI**, published by linuxhw from dumps hw-probe users uploaded to linux-hardware.org, [d6d30c6](https://github.com/linuxhw/ACPI/tree/d6d30c6ef6f45506567f25a302c64f009d8f5f4e) | CC-BY-4.0 | the corpus; parsed and counted, never reproduced |

---

## References

The ACPI Specification 6.6: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf. Each section links to
its page.


[§3.11.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=120
[§4.8.1.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=138
[§4.8.2.2.1.3]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=143
[§4.8.3.2.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=153
[§5.2.9]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=182
[§5.2.10.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=200
[§5.2.12.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=207
[§5.3]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=362
[§5.3.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=363
[§5.4.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=364
[§5.4.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=366
[§5.5.2.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=369
[§5.5.2.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=370
[§5.5.2.3]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=370
[§5.5.2.4.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=371
[§5.7.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=424
[§5.7.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=425
[§5.7.3]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=428
[§5.7.4]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=428
[§5.8.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=430
[§6.2.12.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=473
[§6.2.12.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=474
[§6.3.7]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=497
[§6.4.3.7]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=525
[§6.5.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=547
[§6.5.4]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=549
[§8.1.4]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=592
[§8.4]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=594
[§8.4.1.1]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=595
[§19.3.2.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1000
[§19.3.5]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1004
[§19.3.5.4]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1006
[§19.3.5.7]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1008
[§19.3.5.8]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1011
[§19.6.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1023
[§19.6.3]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1023
[§19.6.4]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1023
[§19.6.5]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1024
[§19.6.6]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1024
[§19.6.7]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1024
[§19.6.8]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1025
[§19.6.9]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1026
[§19.6.10]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1026
[§19.6.12]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1027
[§19.6.13]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1029
[§19.6.14]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1029
[§19.6.16]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1030
[§19.6.17]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1030
[§19.6.18]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1031
[§19.6.19]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1031
[§19.6.20]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1031
[§19.6.21]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1032
[§19.6.22]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1032
[§19.6.23]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1032
[§19.6.25]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1033
[§19.6.26]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1034
[§19.6.27]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1034
[§19.6.29]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1035
[§19.6.30]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1035
[§19.6.31]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1036
[§19.6.32]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1037
[§19.6.39]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1043
[§19.6.42]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1045
[§19.6.46]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1049
[§19.6.47]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1050
[§19.6.48]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1050
[§19.6.49]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1052
[§19.6.50]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1053
[§19.6.55]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1055
[§19.6.60]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1059
[§19.6.62]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1060
[§19.6.63]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1061
[§19.6.64]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1063
[§19.6.69]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1067
[§19.6.70]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1068
[§19.6.71]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1068
[§19.6.73]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1069
[§19.6.75]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1069
[§19.6.77]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1070
[§19.6.78]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1071
[§19.6.79]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1071
[§19.6.80]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1072
[§19.6.81]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1072
[§19.6.85]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1075
[§19.6.86]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1077
[§19.6.87]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1077
[§19.6.88]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1077
[§19.6.89]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1078
[§19.6.90]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1078
[§19.6.91]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1079
[§19.6.92]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1079
[§19.6.93]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1079
[§19.6.94]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1079
[§19.6.95]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1080
[§19.6.97]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1080
[§19.6.98]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1081
[§19.6.99]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1081
[§19.6.100]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1082
[§19.6.101]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1083
[§19.6.102]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1083
[§19.6.108]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1096
[§19.6.115]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1102
[§19.6.117]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1104
[§19.6.118]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1104
[§19.6.120]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1105
[§19.6.121]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1105
[§19.6.122]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1105
[§19.6.123]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1106
[§19.6.124]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1107
[§19.6.125]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1107
[§19.6.126]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1107
[§19.6.127]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1108
[§19.6.129]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1109
[§19.6.132]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1110
[§19.6.133]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1111
[§19.6.135]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1113
[§19.6.136]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1113
[§19.6.137]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1114
[§19.6.138]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1114
[§19.6.139]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1115
[§19.6.140]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1115
[§19.6.141]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1115
[§19.6.143]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1118
[§19.6.149]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1121
[§19.6.150]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1122
[§19.6.155]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1126
[§19.6.156]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1127
[§20.2.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1131
[§20.2.3]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1132
[§20.2.4]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1133
[§20.2.5]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1133
[§20.2.5.2]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1134
[§20.2.5.3]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1137
[§20.2.5.4]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1139
[§20.3]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1144
[Appendix C]: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf#page=1200
[Intel ACPI]: https://cdrdv2-public.intel.com/772754/intel-processor-vendor-specific-acpi.pdf

[uacpi-ports]: https://github.com/uACPI/uACPI/blob/c0edecef62bcac6ce3f783540099190bafadadda/source/default_handlers.c#L654-L761
