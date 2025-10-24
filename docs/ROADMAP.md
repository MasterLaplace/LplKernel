# LplKernel Development Roadmap

This roadmap follows the recommended OSDev.org learning path for x86 kernel development, organized by difficulty and logical progression.

> **Legend:** ✅ Complete | 🚧 In Progress | ⏸️ Paused | ❌ Not Started | 🔗 External Resource

---

## 📊 Progress Summary (Updated: 2025)

### 🎯 Current Position: **End of Phase 2** ⬅️ YOU ARE HERE
**Next Goal**: Phase 3 - Implement IDT and exception handlers

### Phase Completion Status:
- ✅ **Phase 0**: Prerequisites & Environment Setup - **90% Complete**
  - Cross-compiler ✅, Makefiles ✅, QEMU ✅, Multiboot ✅
  - Missing: Advanced debugging setup (Bochs, GDB integration)

- ✅ **Phase 1**: Bare Bones Kernel - **100% Complete**
  - VGA text mode ✅, Serial ports ✅, Scrolling ✅, Colors ✅, Multiboot parsing ✅

- 🚧 **Phase 2**: CPU Initialization & Protection - **85% Complete**
  - Higher-half kernel ✅, Paging (boot-time + runtime) ✅, GDT complete ✅
  - Missing: TSS initialization, page frame allocator, Ring 3 transition

- ❌ **Phase 3**: Interrupts & Exceptions - **0% Complete** ⬅️ **START HERE**
  - No IDT, no exception handlers, no PIC initialization

- ❌ **Phase 4**: Memory Management - **0% Complete**
  - No heap allocator (kmalloc/kfree), no page frame allocator

- ❌ **Phase 5**: Device Drivers - **10% Complete**
  - VGA text mode ✅, Serial COM1 ✅
  - Missing: Keyboard, storage, network, USB, PCI

- ❌ **Phase 6**: Multitasking & Scheduling - **0% Complete**
  - No scheduler, processes, threads, or context switching

- ❌ **Phase 7-10**: Advanced Features - **0% Complete**
  - File systems, networking, userspace, 64-bit - all unimplemented

### What We Have:
```
✅ Boot process (GRUB multiboot)
✅ Higher-half kernel at 0xC0000000
✅ Paging enabled (identity + kernel mapping)
✅ Runtime paging API (map/unmap pages, virt→phys translation)
✅ GDT with 6 segments (null, kcode, kdata, ucode, udata, tss)
✅ VGA text mode terminal with colors, scrolling, backspace across lines
✅ Serial output with hex8/16/32/64 and binary formatting
✅ Multiboot info parsing (memory map, boot device, modules)
✅ GDT debugging helpers (terminal + serial output)
✅ Global constructors/destructors
✅ Cross-compilation toolchain (i686-elf)
```

### What We Need Next:
```
🎯 IDT (Interrupt Descriptor Table) structure and loading
🎯 Exception handlers (#DE, #GP, #PF, #DF, etc.)
🎯 PIC initialization and remapping
🎯 IRQ handlers (timer, keyboard)
🎯 Basic exception testing (divide-by-zero)
```

### Known Issues:
```
⚠️ TSS entry in GDT has base=0, limit=0 (not configured)
⚠️ No page frame allocator (can't create new page tables dynamically)
⚠️ No memory allocator (all allocations static)
⚠️ No interrupt handling at all (CPU exceptions will triple fault)
```

---

## 📚 Phase 0: Prerequisites & Environment Setup

### Development Environment
- [x] [GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler) - Build a cross-compiler for i686-elf
- [x] [Build System](https://wiki.osdev.org/Makefile) - Setup Makefiles
- [ ] [CMake Build System](https://wiki.osdev.org/CMake_Build_System) - Alternative build system
- [ ] [OS Specific Toolchain](https://wiki.osdev.org/OS_Specific_Toolchain) - Adapt GCC/Binutils to your platform
- [ ] [VSCode for Debugging](https://wiki.osdev.org/User:TheCool1Kevin/VSCode_Debug) - Debug configuration

### Testing & Debugging
- [x] [QEMU](https://wiki.osdev.org/QEMU) - Primary emulator
- [ ] [Bochs](https://wiki.osdev.org/Bochs) - Alternative emulator with debugging
- [ ] [How Do I Use A Debugger With My OS](https://wiki.osdev.org/How_Do_I_Use_A_Debugger_With_My_OS)
- [ ] [Kernel Debugging](https://wiki.osdev.org/Kernel_Debugging)
- [ ] [Unit Testing](https://wiki.osdev.org/Unit_Testing)

### Knowledge & Resources
- [x] [Required Knowledge](https://wiki.osdev.org/Required_Knowledge)
- [x] [Beginner Mistakes](https://wiki.osdev.org/Beginner_Mistakes)
- [x] [How To Ask Questions](https://wiki.osdev.org/How_To_Ask_Questions)
- [ ] [What Order Should I Make Things In?](https://wiki.osdev.org/What_Order_Should_I_Make_Things_In%3F)

---

## 🥾 Phase 1: Bare Bones Kernel (Difficulty ⭐)

### Basic Kernel
- [x] [Bare Bones](https://wiki.osdev.org/Bare_Bones) - Write a basic 32-bit kernel in C for x86
  - [x] Boot with GRUB
  - [x] Basic VGA text mode output
  - [x] Adding Support for Newlines to Terminal Driver
- [x] [User:Zesterer/Bare Bones](https://wiki.osdev.org/User:Zesterer/Bare_Bones) - Improved tutorial
  - [x] Add Color Support To Your Terminal
  - [ ] [Add ANSI Support To Your Terminal](https://en.wikipedia.org/wiki/ANSI_escape_code)
  - [x] Add Scrolling To Your Terminal
- [x] [Meaty Skeleton](https://wiki.osdev.org/Meaty_Skeleton) - Template operating system
  - [x] Renaming MyOS to YourOS
  - [x] [Stack Smash Protector](https://wiki.osdev.org/Stack_Smashing_Protector)
  - [ ] [Improving the Build System](https://wiki.osdev.org/Hard_Build_System)

### Boot Process
- [ ] [Bootloader](https://wiki.osdev.org/Bootloader) - Theory and concepts
- [ ] [Boot Sequence](https://wiki.osdev.org/Boot_Sequence)
- [x] [GRUB](https://wiki.osdev.org/GRUB) - Using GRUB as bootloader
- [ ] [Bootable Disk](https://wiki.osdev.org/Bootable_Disk) - Create bootable USB
- [ ] [Bootable CD](https://wiki.osdev.org/Bootable_CD) - Create bootable CD/ISO
- [ ] [Rolling Your Own Bootloader](https://wiki.osdev.org/Rolling_Your_Own_Bootloader) - Advanced

### Multiboot
- [x] [Multiboot](https://wiki.osdev.org/Multiboot)
  - [x] [Multiboot Specification v1](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) 🔗
  - [ ] [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html) 🔗
  - [x] Parse Multiboot information structure
  - [x] Memory map detection

### Basic I/O
- [x] [Serial Ports](https://wiki.osdev.org/Serial_Ports) - COM port communication
- [x] [Printing To Screen](https://wiki.osdev.org/Printing_To_Screen) - VGA text mode
- [x] [Text Mode Cursor](https://wiki.osdev.org/Text_Mode_Cursor)
- [ ] [Drawing In Protected Mode](https://wiki.osdev.org/Drawing_In_Protected_Mode) - Pixel plotting basics
- [ ] [PC Screen Font](https://wiki.osdev.org/PC_Screen_Font) - Bitmap fonts

---

## 🏗️ Phase 2: CPU Initialization & Protection (Difficulty ⭐⭐)

### Higher Half Kernel
- [x] [Higher Half x86 Bare Bones](https://wiki.osdev.org/Higher_Half_x86_Bare_Bones)
  - [x] Higher Half Kernel at 0xC0000000
  - [x] [Setting Up Paging](https://wiki.osdev.org/Setting_Up_Paging) (boot.s)
  - [x] Identity mapping (boot-time only)
  - [x] Kernel space mapping at 0xC0000000
  - [x] VGA buffer mapping at 0xB8000
  - [x] Remove identity mapping after jump to higher half
  - [x] [Paging](https://wiki.osdev.org/Paging) - Runtime management with map/unmap API ✅
  - [ ] [Page Frame Allocation](https://wiki.osdev.org/Page_Frame_Allocation) - Physical memory manager

### Segmentation (GDT)
- [x] [GDT Tutorial](https://wiki.osdev.org/GDT_Tutorial)
  - [x] [Global Descriptor Table](https://wiki.osdev.org/Global_Descriptor_Table)
  - [x] Create and load GDT (gdt.c, gdt_load.s)
  - [x] Kernel code/data segments
  - [x] User code/data segments (DPL=3)
  - [x] GDT helper functions for debugging (gdt_helper.c)
  - 🚧 [Task State Segment](https://wiki.osdev.org/Task_State_Segment) (TSS) - Entry exists but not initialized
- [ ] [Segmentation](https://wiki.osdev.org/Segmentation) - Deep dive
  - [ ] [Segment Limits](https://wiki.osdev.org/Segment_Limits)
  - [ ] [Getting to Ring 3](https://wiki.osdev.org/Getting_to_Ring_3) - User mode

### Call Conventions & ABI
- [x] [Calling Global Constructors](https://wiki.osdev.org/Calling_Global_Constructors)
- [ ] [Calling Conventions](https://wiki.osdev.org/Calling_Conventions)
- [ ] [System V ABI](https://wiki.osdev.org/System_V_ABI)

---

## ⚡ Phase 3: Interrupts & Exceptions (Difficulty ⭐⭐) ⬅️ **CURRENT PHASE**

> ⚠️ **STATUS**: Not started - No IDT/interrupt code found in codebase
> 🎯 **NEXT STEP**: Implement basic IDT and exception handlers (start with divide-by-zero)

### Interrupt Descriptor Table
- [ ] [Interrupts](https://wiki.osdev.org/Interrupts) - Theory and overview ⭐ START HERE
- [ ] [Interrupts Tutorial](https://wiki.osdev.org/Interrupts_Tutorial) ⭐ CRITICAL
  - [ ] [Interrupt Descriptor Table](https://wiki.osdev.org/Interrupt_Descriptor_Table) (IDT)
  - [ ] Create IDT structure (similar to GDT pattern)
  - [ ] Load IDT with LIDT
  - [ ] [Interrupt Service Routines](https://wiki.osdev.org/Interrupt_Service_Routines) (ISRs)
  - [ ] [IDT problems](https://wiki.osdev.org/IDT_problems) - Common issues

### CPU Exceptions
- [ ] [Exceptions](https://wiki.osdev.org/Exceptions) ⭐ IMPLEMENT FIRST
  - [ ] Division Error (#DE) - Good starting point for testing
  - [ ] Debug Exception (#DB)
  - [ ] Breakpoint (#BP)
  - [ ] Invalid Opcode (#UD)
  - [ ] General Protection Fault (#GP)
  - [ ] [Page Fault](https://wiki.osdev.org/Page_Fault) (#PF) - Critical for paging
  - [ ] Double Fault (#DF) - Safety net
  - [ ] [Triple Fault](https://wiki.osdev.org/Triple_Fault) - Understanding and prevention

### Hardware Interrupts
- [ ] [PIC](https://wiki.osdev.org/PIC) - Programmable Interrupt Controller ⭐ REQUIRED FOR IRQs
  - [ ] Initialize and remap PIC (IRQ 0-15 to 32-47)
  - [ ] [IRQ](https://wiki.osdev.org/IRQ) - Hardware interrupt requests
  - [ ] EOI (End of Interrupt) handling
- [ ] [NMI](https://wiki.osdev.org/NMI) - Non-Maskable Interrupt
- [ ] [APIC](https://wiki.osdev.org/APIC) - Advanced PIC (modern systems)
  - [ ] Local APIC configuration
  - [ ] [APIC timer](https://wiki.osdev.org/APIC_timer)
- [ ] [IOAPIC](https://wiki.osdev.org/IOAPIC) - I/O APIC
- [ ] [Message Signaled Interrupts](https://wiki.osdev.org/Message_Signaled_Interrupts) (MSI)

### Timers
- [ ] [Programmable Interval Timer](https://wiki.osdev.org/Programmable_Interval_Timer) (PIT)
- [ ] [HPET](https://wiki.osdev.org/HPET) - High Precision Event Timer
- [ ] [RTC](https://wiki.osdev.org/RTC) - Real Time Clock
- [ ] [CMOS](https://wiki.osdev.org/CMOS) - CMOS clock
- [ ] [Detecting CPU Speed](https://wiki.osdev.org/Detecting_CPU_Speed)

---

## 🧠 Phase 4: Memory Management (Difficulty ⭐⭐)

### Memory Detection
- [ ] [Detecting Memory (x86)](https://wiki.osdev.org/Detecting_Memory_(x86))
  - [x] Memory Map via GRUB/Multiboot
  - [ ] E820 memory map
  - [ ] [A20 Line](https://wiki.osdev.org/A20_Line) - Enable A20 gate

### Physical Memory Management
- [ ] [Page Frame Allocation](https://wiki.osdev.org/Page_Frame_Allocation)
  - [ ] [Writing A Page Frame Allocator](https://wiki.osdev.org/Writing_A_Page_Frame_Allocator)
  - [ ] Bitmap allocator
  - [ ] Stack allocator
  - [ ] Buddy allocator (advanced)

### Virtual Memory & Paging
- [ ] [Paging](https://wiki.osdev.org/Paging) - Overview
  - [ ] [Setting Up Paging](https://wiki.osdev.org/Setting_Up_Paging) - 32-bit paging
  - [ ] [Setting Up Paging With PAE](https://wiki.osdev.org/Setting_Up_Paging_With_PAE) - PAE mode
  - [ ] Page directory/table management
  - [ ] TLB flushing
  - [ ] Page permissions (R/W/X)
- [ ] [Memory Management Unit](https://wiki.osdev.org/Memory_Management_Unit) - In-depth MMU
- [ ] [Brendan's Memory Management Guide](https://wiki.osdev.org/Brendan%27s_Memory_Management_Guide)

### Heap & Dynamic Allocation
- [ ] [Memory Allocation](https://wiki.osdev.org/Memory_Allocation)
- [ ] [Heap](https://wiki.osdev.org/Heap) - Kernel heap
  - [ ] [Writing a memory manager](https://wiki.osdev.org/Writing_a_memory_manager)
  - [ ] Simple linked list allocator
  - [ ] [Slab Allocator](https://en.wikipedia.org/wiki/Slab_allocation) 🔗
  - [ ] kmalloc/kfree implementation

---

## 💾 Phase 5: Device Drivers (Difficulty ⭐⭐)

> ⚠️ **STATUS**: Not started - No device drivers beyond VGA/Serial found in codebase
> 📋 **REQUIRES**: Phase 3 complete (interrupts for IRQ handling)

### Input Devices
- [ ] [PS/2 Keyboard](https://wiki.osdev.org/PS/2_Keyboard) ⭐ START HERE (needs IRQ 1)
  - [ ] Scan codes
  - [ ] Keyboard layouts
- [ ] [Mouse Input](https://wiki.osdev.org/Mouse_Input)
  - [ ] PS/2 mouse
  - [ ] USB mouse (advanced)

### Storage Devices
- [ ] [ATA](https://wiki.osdev.org/Category:ATA) - IDE/PATA hard disks
  - [ ] [ATA PIO Mode](https://wiki.osdev.org/ATA_PIO_Mode) ⭐ Simplest storage
  - [ ] [ATA DMA](https://wiki.osdev.org/Category:ATA)
- [ ] [ATAPI](https://wiki.osdev.org/ATAPI) - CD-ROM drives
- [ ] [AHCI](https://wiki.osdev.org/AHCI) - SATA native (modern)
- [ ] [NVMe](https://wiki.osdev.org/NVMe) - NVMe SSDs (very modern)
- [ ] [Floppy Disk Controller](https://wiki.osdev.org/Floppy_Disk_Controller) (optional, legacy)
- [ ] [ISA DMA](https://wiki.osdev.org/ISA_DMA)

### Video & Graphics
- [ ] [VGA Hardware](https://wiki.osdev.org/VGA_Hardware) & [VGA Resources](https://wiki.osdev.org/VGA_Resources)
- [ ] [BGA](https://wiki.osdev.org/Bochs_VBE_Extensions) - Bochs Graphics Adapter
- [ ] [Drawing In a Linear Framebuffer](https://wiki.osdev.org/Drawing_In_a_Linear_Framebuffer)
- [ ] [Double Buffering](https://wiki.osdev.org/Double_Buffering)
- [ ] [VGA Fonts](https://wiki.osdev.org/VGA_Fonts)
- [ ] [Accelerated Graphic Cards](https://wiki.osdev.org/Accelerated_Graphic_Cards)
  - [ ] [Native Intel graphics](https://wiki.osdev.org/Native_Intel_graphics)

### Audio
- [ ] [Sound](https://wiki.osdev.org/Sound)
- [ ] [PC Speaker](https://wiki.osdev.org/PC_Speaker)
- [ ] [Sound Blaster 16](https://wiki.osdev.org/Sound_Blaster_16)

### Network
- [ ] [3c90x](https://wiki.osdev.org/3c90x)
- [ ] [Intel 8254x](https://wiki.osdev.org/Intel_8254x)
- [ ] [Ne2000](https://wiki.osdev.org/Ne2000)
- [ ] [RTL8139](https://wiki.osdev.org/RTL8139)
- [ ] [RTL8169](https://wiki.osdev.org/RTL8169)
- [ ] [AMD PCnet](https://wiki.osdev.org/AMD_PCNET)

### Bus Systems
- [ ] [PCI](https://wiki.osdev.org/PCI) - PCI bus enumeration
- [ ] [PCI Express](https://wiki.osdev.org/PCI_Express)
- [ ] [USB](https://wiki.osdev.org/Universal_Serial_Bus)
  - [ ] USB UHCI/OHCI
  - [ ] USB EHCI
  - [ ] USB xHCI
- [ ] [Plug-and-Play](https://wiki.osdev.org/Plug-and-Play) (PNP)

### ACPI
- [ ] [ACPI](https://wiki.osdev.org/ACPI)
- [ ] [RSDP](https://wiki.osdev.org/RSDP)
- [ ] [AML](https://wiki.osdev.org/AML) - ACPI Machine Language

---

## 🧵 Phase 6: Multitasking & Scheduling (Difficulty ⭐⭐⭐)

> ⚠️ **STATUS**: Not started - No scheduler, threads, or processes found in codebase
> 📋 **REQUIRES**: Phase 3 (timer interrupts) + Phase 4 (memory management)
> ℹ️ **NOTE**: TSS entry exists in GDT but not initialized (base=0, limit=0)

### Process & Thread Management
- [ ] [Processes and Threads](https://wiki.osdev.org/Processes_and_Threads)
- [ ] [Brendan's Multi-tasking Tutorial](https://wiki.osdev.org/Brendan%27s_Multi-tasking_Tutorial)
- [ ] [Context Switching](https://wiki.osdev.org/Context_Switching)
- [ ] Process Control Block (PCB)
- [ ] Thread Control Block (TCB)
- [ ] [Task State Segment](https://wiki.osdev.org/Task_State_Segment) - Initialize TSS for ring transitions

### Scheduling
- [ ] [Scheduling Algorithms](https://wiki.osdev.org/Scheduling_Algorithms)
  - [ ] Round-robin ⭐ START HERE (simplest)
  - [ ] Priority-based
  - [ ] Multi-level feedback queue
- [ ] [Multiprocessor Scheduling](https://wiki.osdev.org/Multiprocessor_Scheduling)
- [ ] [Blocking Process](https://wiki.osdev.org/Blocking_Process) - Sleep/wait

### Synchronization
- [ ] [Synchronization Primitives](https://wiki.osdev.org/Synchronization_Primitives)
  - [ ] Spinlocks
  - [ ] Mutexes
  - [ ] Semaphores
  - [ ] Read-write locks
- [ ] [Signals](https://wiki.osdev.org/Signals)

### Inter-Process Communication
- [ ] [Message Passing](https://wiki.osdev.org/Message_Passing)
- [ ] [Shared Memory](https://wiki.osdev.org/Shared_Memory)
- [ ] [Remote Procedure Call](https://wiki.osdev.org/Remote_Procedure_Call)
- [ ] Pipes
- [ ] Sockets

### Multiprocessing
- [ ] [Multiprocessing](https://wiki.osdev.org/Multiprocessing)
  - [ ] SMP (Symmetric Multiprocessing)
  - [ ] AP (Application Processor) initialization
  - [ ] Per-CPU data structures

---

---

## � Phase 7: File Systems (Difficulty ⭐⭐⭐)

> ⚠️ **STATUS**: Not started - No file system code found in codebase
> 📋 **REQUIRES**: Phase 5 (storage drivers like ATA)

### File System Basics
- [ ] [VFS](https://wiki.osdev.org/VFS) - Virtual File System
- [ ] [Initrd](https://wiki.osdev.org/Initrd) - Initial ramdisk ⭐ START HERE (no disk driver needed)
- [ ] [FS](https://wiki.osdev.org/FS) - Generic file systems

### Specific File Systems
- [ ] [FAT](https://wiki.osdev.org/FAT) - FAT12/FAT16/FAT32 ⭐ Good first filesystem
- [ ] [Ext2](https://wiki.osdev.org/Ext2) - Second Extended Filesystem

---

## 🌐 Phase 8: Networking (Difficulty ⭐⭐⭐⭐)

> ⚠️ **STATUS**: Not started - No networking code found in codebase
> 📋 **REQUIRES**: Phase 5 (network card driver like RTL8139)

### Network Stack
- [ ] OSI Model understanding
- [ ] Ethernet frames ⭐ START HERE
- [ ] [ARP](https://wiki.osdev.org/ARP) - Address Resolution Protocol
- [ ] [IP](https://en.wikipedia.org/wiki/Internet_Protocol) - Internet Protocol 🔗
- [ ] [ICMP](https://en.wikipedia.org/wiki/Internet_Control_Message_Protocol) - Ping 🔗
- [ ] [TCP](https://en.wikipedia.org/wiki/Transmission_Control_Protocol) 🔗
- [ ] [UDP](https://en.wikipedia.org/wiki/User_Datagram_Protocol) 🔗
- [ ] [DNS](https://en.wikipedia.org/wiki/Domain_Name_System) 🔗
- [ ] [DHCP](https://en.wikipedia.org/wiki/Dynamic_Host_Configuration_Protocol) 🔗

---

## 👤 Phase 9: User Space & System Calls (Difficulty ⭐⭐⭐)

> ⚠️ **STATUS**: Not started - No syscall or userspace code found in codebase
> 📋 **REQUIRES**: Phase 2 (Ring 3 in GDT) + Phase 3 (INT 0x80 or SYSENTER) + Phase 6 (process management)

### User Mode
- [ ] [Getting to Ring 3](https://wiki.osdev.org/Getting_to_Ring_3) ⭐ START HERE (GDT already has user segments)
- [ ] [Getting to User Mode](https://web.archive.org/web/20160326162854/http://xarnze.com/article/Entering%20User%20Mode) 🔗
- [ ] User/kernel space separation
- [ ] TSS configuration for ring transitions (TSS entry exists but not initialized)

### System Calls
- [ ] System call interface design
- [ ] [ARM System Calls](https://wiki.osdev.org/ARM_System_Calls) (reference)
- [ ] INT 0x80 / SYSENTER / SYSCALL ⭐ INT 0x80 simplest to start
- [ ] System call table
- [ ] Argument passing (via registers or stack)

### Executable Loading
- [ ] [ELF](https://wiki.osdev.org/ELF) - Executable and Linkable Format ⭐ START HERE
  - [ ] [ELF Tutorial](https://wiki.osdev.org/ELF_Tutorial)
  - [ ] Program headers
  - [ ] Dynamic linking
- [ ] [PE](https://wiki.osdev.org/PE) - Portable Executable (Windows)
- [ ] Process creation (fork/exec)

---

## 🎯 Phase 10: Advanced Features (Difficulty ⭐⭐⭐⭐)

> ⚠️ **STATUS**: Not started - All advanced features unimplemented
> 📋 **REQUIRES**: Most or all previous phases complete

### 64-bit Support
- [ ] [Setting Up Long Mode](https://wiki.osdev.org/Setting_Up_Long_Mode)
- [ ] [Creating a 64-bit kernel](https://wiki.osdev.org/Creating_a_64-bit_kernel)
- [ ] [x86-64](https://wiki.osdev.org/X86-64)
  - [ ] Long mode initialization
  - [ ] 64-bit paging (4-level)
  - [ ] 64-bit IDT/GDT

### UEFI
- [ ] [UEFI](https://wiki.osdev.org/UEFI)
- [ ] [Writing a bootloader for UEFI](https://wiki.osdev.org/Uefi.inc)
- [ ] [Broken UEFI implementations](https://wiki.osdev.org/Broken_UEFI_implementations)

### Shell & Command Line
- [ ] [Creating A Shell](https://wiki.osdev.org/Creating_A_Shell)
- [ ] Command parsing
- [ ] Built-in commands
- [ ] Process execution

### Porting Software
- [ ] [Porting Newlib](https://wiki.osdev.org/Porting_Newlib) - C library
- [ ] [Using Libsupc++](https://wiki.osdev.org/Libsupcxx) - C++ support
- [ ] [Porting GCC to your OS](https://wiki.osdev.org/Porting_GCC_to_your_OS)

### Other Architectures (Future)
- [ ] [ARM Overview](https://wiki.osdev.org/ARM_Overview)
- [ ] [Raspberry Pi Bare Bones](https://wiki.osdev.org/Raspberry_Pi_Bare_Bones)
- [ ] [RISC-V](https://wiki.osdev.org/Category:RISC-V)
- [ ] [PowerPC](https://wiki.osdev.org/Category:PowerPC)

---

## 📖 References & Resources

### Essential Documentation
- 📘 [Intel® 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) 🔗
- 📘 [OSDev Wiki](https://wiki.osdev.org/) 🔗
- 📘 [OSDev Forums](https://forum.osdev.org/) 🔗

### Recommended Reading
- [ ] [Books on OS Development](https://wiki.osdev.org/Books)
- [ ] [Academic Papers](https://wiki.osdev.org/Academic_Papers)
- [ ] [Going Further on x86](https://wiki.osdev.org/Going_Further_on_x86)
- [ ] [Creating an Operating System](https://wiki.osdev.org/Creating_an_Operating_System)

### Tools & Utilities
- [ ] [Disk Images](https://wiki.osdev.org/Disk_Images)
- [ ] [Loopback Device](https://wiki.osdev.org/Loopback_Device) (Linux)
- [ ] [Windows Tools](https://wiki.osdev.org/Windows_Tools)

---

## 🎓 Learning Path Notes

**Difficulty Ratings:**
- ⭐ **Beginner** - Essential basics, start here
- ⭐⭐ **Intermediate** - Core OS concepts
- ⭐⭐⭐ **Advanced** - Complex subsystems
- ⭐⭐⭐⭐ **Expert** - Cutting-edge features

**Recommended Order:**
1. Complete Phase 0-2 (Environment → Bare Bones → CPU Init)
2. Implement Phase 3 (Interrupts) - Critical for everything else
3. Build Phase 4 (Memory Management) - Foundation for advanced features
4. Add Phase 5 (Drivers) - As needed for your project
5. Implement Phase 6 (Multitasking) - Major milestone
6. Add Phase 7-10 as needed based on project goals

**Tips:**
- Don't skip the prerequisites
- Test thoroughly at each phase
- Read the Intel manuals for low-level details
- Ask questions on OSDev forums
- Study existing OS projects for inspiration
