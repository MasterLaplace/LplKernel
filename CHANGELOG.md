# Changelog

What changed in each release, generated from the commit titles on `main`. Regenerate it with
`tools/changelog.sh` from [MasterLaplace/.github](https://github.com/MasterLaplace/.github);
an edit by hand is lost at the next release, whose check refuses a file that differs from
what the history gives.

## [0.1.0] - 2026-10-05

### Added

- **version**: The version lives in config.h, and the boot says what was built (#433)
- **site**: Build the roadmap page from the GitHub issues (#416)
- **power**: Name each wakeup, sleep deeper, measure the delivered clock
- Add new elements to the roadmap and update the details of existing elements.
- **kernel**: Gate P20 folds a road planned coarse and refined fine
- **kernel**: Gate P20 folds a closed world and the roads it shortens
- **vesuvius**: Balayage du rouleau entier par BANDES, au niveau 2
- **kernel**: W^X, structured telemetry, a reconciler, and declared backpressure
- **arch**: Declare what a target has, so portable code stops assuming
- Link the cave warren into ring 0 and add the P19 parity gate
- Libassistant, libknowledge, le plancher energie et les gates P14 a P18
- Enhance interrupt handling and add support for IOAPIC line ownership
- Add key state tracking for PS/2 keyboard and expose API for input layer
- Add PS/2 mouse support and implement endless world generation
- Add support for multiple cartridges in ISO build and enhance client app functionality
- Update client app to support TerrainWorld and enhance world profile descriptions
- Implement boot module support for procedural generation and living simulation
- Enhance kernel memory management and real-time allocation rules
- Implement kernel server entry point and adjust project dependencies
- Enhance accessibility and SEO across the site
- **site**: Online book — render LplKernel_Book as a multi-chapter section
- **site**: Laplace project portfolio with in-browser v86 kernel demo
- Game-agnostic in-kernel client app + CubePile sim payload
- **p6**: In-kernel P6 smoke + bit-identical cross-target verification
- **p5**: In-kernel multi-viewport + render-to-texture, bit-identical
- **p5**: In-kernel classical lighting, bit-identical to oracle
- **p5**: In-kernel textures + textured cube present, bit-identical
- **p5**: In-kernel instancing/frustum-cull, bit-identical to oracle
- **p5**: In-kernel 3D cube present, bit-identical to oracle
- **p5**: In-kernel 3D projection smoke, bit-identical to oracle
- **p4**: Compile lpl::scene into libengine + cross-target scene smoke
- **p4**: Compile PPM codec into libengine + cross-target round-trip
- **p4**: Present a painted 2D scene to the scanout (image present smoke)
- **p4**: Compile Painter into libengine + cross-target paint signature
- **p4**: Compile lpl::image into libengine + cross-target image smoke
- **p4**: Route hal_display through a persistent virtio-gpu scanout
- **p4**: VirtIO-GPU 2D display lifecycle — first GPU present
- **p4**: VirtIO-GPU first control round-trip (GET_DISPLAY_INFO)
- **p4**: VirtIO-GPU split virtqueue setup + DRIVER_OK
- **p4**: VirtIO-GPU device bring-up handshake + queue sizing
- **p4**: VirtIO-GPU virtio-pci capability walk + MMIO mapping
- **p4**: VirtIO-GPU PCI discovery in the HAL
- **p4**: Pull the render portable-core into libengine
- **p3**: Drive the boot facade with the freestanding GameLoop
- **p3**: Lplplugin_initialize extern "C" engine boot facade
- **p3**: FXSAVE/FXRSTOR in ISR + KernelDisplayRenderer smoke
- Bring up the platform HAL seam in-kernel (clock/display/input/gpu-mem) — P2
- Run a deterministic ECS+physics tick headless in-kernel — P1 gate
- Run the ECS DAG scheduler headless in-kernel; SSE stack realign — P1
- Run engine memory-DI + ECS storage core headless in-kernel — P1
- Bring up freestanding C++ engine module (libengine) — P0
- Parse PCI base address registers
- Add AZERTY keyboard layout and fix TLB-shootdown ACK infinite spin
- Implement destination APIC ID handling in IOAPIC routing
- Implement NUMA policy support and enhance memory allocation strategies
- Implement stack allocator, TLSF memory allocator, and virtual memory manager
- Implement Buddy Allocator with Coalescing and Page Table Reclamation
- Implement APIC and IOAPIC support for interrupt handling and memory management
- Implement advanced APIC timer backend and enhance exception handling
- Add handlers for debug, breakpoint, and invalid opcode exceptions
- Implement clock subsystem with timer and RTC support, update roadmap
- Add PIT and RTC support, enhance IRQ handling and status reporting
- Add spurious IRQ handling for IRQ7 and IRQ15, update progress in roadmap
- Add exception handling and keyboard interrupt support
- Nhance interrupt handling and memory management
- Implement Interrupt Descriptor Table (IDT) and associated ISR handling
- Refactor and implement Physical Memory Manager (PMM)
- Add types and functions for manipulating fixed-point numbers in q16.16 format
- Add mathematical functions for processing floats and fixed type definitions
- Implement plasma shader effect and related graphics functionality
- **graphics**: Implement framebuffer driver and graphics mode support
- **cpu**: Implement TSS initialization, add TSS module to build configuration and correct NULL definition to use 0UL for compatibility in stddef
- **kernel**: Implement Task State Segment (TSS) initialization and loading
- **config**: Add LPL_LIKELY, LPL_UNLIKELY, and LPL_TODO macros for better TODO management
- **debug**: Add GDB and QEMU debugging configuration with custom commands and VSCode integration
- **paging**: Implement runtime paging management and related assembly functions
- **gdt**: Implement GDT helper functions for printing and writing entries
- **serial**: Add functions for writing hexadecimal and binary values
- **cpu**: Implement Global Descriptor Table and associated functions
- **gdt**: Add the structure of the global descriptor table (GDT) and its entries
- **string**: Add a string management library with utility functions
- **multiboot**: Add functions to write multiboot info to serial output
- **graphics**: Add graphics mode support (not implemented) and enhance multiboot info display
- **multiboot**: Add multiboot_info structure, related types for bootloader integration and integrate it into kernel initialization
- **kernel**: Implement serial character reading in kernel_main and call global destructor
- **serial**: Add serial communication functions and integrate with kernel initialization
- **libc**: Compile with -nostdinc
- Only process graphical and whitespace for printing (#3)
- **makefile**: Add stack protector flags to CFLAGS and LDFLAGS
- **arm**: Add the crti.c, crtn.c and linker.ld files to manage the initialisation and finalisation tables
- **kernel**: Add x86_64 initialization and finalization sections in assembly files
- **docs**: Add a ROADMAP.md file with the project stages
- **build**: Allow specifying the number of jobs for parallel builds and update scripts to pass arguments
- **tty**: Add vga.h for color constants and update terminal functions
- **build**: Add .clang-format for code style and update .gitignore to include kernel files
- **src**: Add number printing function
- **LplKernel**: Build a complete bootable iso using config, libc and terminal header
- **LplKernel**: Add initial setup, generate iso and emulate on Qemu

### Fixed

- **deps**: Lock the siblings at the mains that carry their config.h (#435)
- **site**: Move to astro 7 and keep the pages as they are (#23)
- **ci**: The linter formats C++ and pushes to its own branch, and the commit check reaches every branch and pull request (#10)
- Grab the pointer on hover, so a relative mouse reaches the guest at all
- Correct conditional compilation for LPL_PLUGIN_UNAVAILABLE in kernel_main
- Conditionally include C++ runtime support based on LplPlugin availability
- Update simulation initialization to use lazy construction for ActiveSim
- **ci**: Update cache key for GCC 14 toolchain and improve source tree handling
- **ci**: Update cross-toolchain paths and cache keys in deploy workflow
- **ci**: Build freestanding libstdc++ in cross-toolchain (fixes <cstddef>)
- **ci**: Build GCC 14 cross-toolchain in portfolio deploy for gnu++23
- **ci**: Build kernel C++ runtime with gnu++17 so older cross toolchains work
- **p4**: Run engine render + boot facade on a virtio-gpu-only display
- Resolve #PF from VA collision between framebuffer and pinned-memory allocator
- Update installation instructions; enhance build scripts and update terminal_buffer address
- **build_lplkernel**: Correct echo syntax for PATH variable in GitHub Actions
- **tty**: Correct terminal backspace behavior to handle edge cases
- **tty**: Improve terminal buffer management by correcting column and row overflow checks
- **tty**: Correct index calculations for the terminal buffer and improve line management
- **ctype**: Update isspace to recognize DEL character
- **tty**: Enhance terminal_putchar to handle backspace
- **env**: Prevent clangd lsp from finding system headers
- **ctype.h**: Add missing is{upper,lower} macros
- **iso**: Ensure the iso folder is ignored
- **ci**: Remove push trigger from LplKernel build workflow
- **boot**: Update terminal buffer address to correct memory location in a Higher Half x86 kernel
- **i3686**: Rename i686 folder to i386 and add building documentation

### Performance

- Put ccache in front of the cross compiler, and name the compiler behind the wrapper

### Changed

- **asm**: Keep every assembly instruction out of C sources
- Say it in names and Doxygen, never in stray comments
- Move smoke testing framework for kernel components
- Move freestanding C++ runtime support and kstd container implementations
- Move smoke tests for memory allocation, HAL, rendering, and image processing
- **p4**: Simplify VirtIO-GPU driver (cleanup pass)
- Update memory management headers and add helper functions
- Update code structure and remove redundant code blocks for improved readability and maintainability
- Update CPU and I/O operations to use asmutils
- Rename ISR stubs and exception names for clarity
- Rename global variables for consistency and clarity
- **linker**: Replace hardcoded address with global_kernel_start for better maintainability
- Ensure proper files organization
- **string**: Unify loop index initialization to use unsigned type across string manipulation functions
- **libc**: Improve the readability of the loop in the memset function
- Provide global_kernel_start symbol in linker script and use it instead of hardcoded address for KERNEL_START define
- **multiboot**: Rename multiboot info variable and add helper functions for multiboot info management
- **config**: Improve the clarity and structure of configuration definitions
- **boot**: Reorganise constant definitions for graphic configuration
- **boot**: Simplify graphics mode configuration in boot.s
- **boot**: Remove unnecessary push of multiboot info for compatibility
- **tty**: Enhance terminal_write_number to support negative values and zero
- **serial**: Standardize struct naming and enhance serial output functions
- **iso**: Simplify directory creation in iso.sh
- **boot**: Replace hardcoded kernel address with constant for better maintainability
- Update Makefile suffixes and improve error messages in tty.h; enhance welcome message visibility in kernel.c
- **kernel**: Improve code consistency and formatting
- **kernel**: Remove framebuffer and VBE related code, update kernel main function

### Documentation

- **contributing**: Say where a change goes and what the kernel code follows (#426)
- LplKernel shows the shared code of conduct (#12)
- **site**: Enhance roadmap details and statuses for kernel and assistant projects
- Add sections on interest rate management and latency compensation to the LplKernel book
- Add roadmap page and update navigation links
- Update LplKernel_Book
- Update benchmarks for the Laplace Engine with methodology and results
- **site**: Add links to Engine-3D and Flakkari GitHub repositories
- Translate the technical content of LplKernel_Book mermaid into English and keep it up to date
- Update lplkernel book and fix some errors
- Delete extra space
- Update ROADMAP and add sequence diagram in LplKernel_Book
- Add sequence diagram
- Update chapter 10's diagram
- **books**: Consolidate 12 scattered research reports into LplKernel_Book and ROADMAP
- Add diagram
- Update
- Add comprehensive architectural vision documentation and update repository ignore and roadmap files
- Diagram
- Add Chapter 9 on energy management and update the recommended courses
- Upload LplKernel book
- Format note about VSCode snap package in README
- Add some discussions I had with copilot
- Update and expand the LplKernel development roadmap with detailed phases and progress
- Reorganise the Roadmap section in the README for better readability
- Update README and ROADMAP with project objectives and progress
- Replace image.png with image.gif in README and update file references
- **roadmap**: Update roadmap to reflect completion of serial ports and global constructors
- **ROADMAP**: Mark Stack Smash Protector and Call Global Constructors as completed in ROADMAP.md
- **ROADMAP**: Update links and mark Meaty Skeleton as completed
- **image**: Update the example image in the documentation
- **README**: Remove obsolete sections and update the licence to GPL-3.0 and add an example image
- **README**: Add instructions to navigate to LplKernel directory before building
- **LplKernel**: Add readme file with usage and references

### Build

- Add native xmake build for the kernel (alongside the .sh scripts)
- **root**: Setup a flag to generate compilation database
- **nix**: Add a nix-based build-system (#2)

### Housekeeping

- **build**: Siblings and a lock file instead of the LplPlugin submodule (#432)
- **license**: Move to MIT and add a citation file (#430)
- **docs**: Archive ROADMAP.md, the issues carry the roadmap (#427)
- Experimental.c leaves the repository (#192)
- Bump the kernel version to 0.0.0.5
- **license**: State the repository's GPLv3 in every file header
- Bump LplPlugin — the release build is clean
- Bump LplPlugin — screen placement flags for the desktop client
- Bump LplPlugin — windows open on the primary monitor
- Bump LplPlugin — X11 windows declare WM_CLASS
- Bump LplPlugin — glclient checks what the window received
- Bump LplPlugin — the desktop client and the chronicle sample
- Update the LplPlugin submodule to commit db6e309
- Update the LplPlugin submodule to commit d7b164a
- Update the LplPlugin submodule to commit b07df4a
- Bump LplPlugin for the AZERTY cave-warp key
- Bump LplPlugin for the carried lamp and the cave warp
- Bump LplPlugin onto the linter's formatting commit
- Bump LplPlugin sur le commit de formatage du linter
- Update the LplPlugin submodule
- Update the LplPlugin submodule
- Update the commit for the LplPlugin submodule
- Update the commit for the LplPlugin submodule
- Update subproject commit for LplPlugin
- Update the commit reference for the LplPlugin sub-project
- Update subproject commit reference for LplPlugin
- Update the LplPlugin commit pointer
- Update LplPlugin commit pointer and update ROADMAP.md
- Re-pin LplPlugin (Mesh-driven KernelDisplayRenderer)
- Re-pin LplPlugin (clang-format pass on the P2b platform DI)
- Re-pin LplPlugin to P2b engine-side platform DI
- Retire the plasma graphics POC — P2b
- Consolidate .gitignore entries by removing redundant files from kernel and libc directories
- **.github**: Add documentation, templates and workflows
- **kernel**: Update kernel main function to display loading message and kernel config
- **LplKernel**: Add new files and update existing files for the kernel and libc directories
- **src**: Clean up build script and terminal header and add auto scrolling in terminal

### Other

- **.Test**: Delete all experimentation
- **root**: Add build options and linker script
