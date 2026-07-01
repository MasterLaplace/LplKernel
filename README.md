# LplKernel

Kernel of the Laplace project

## Description

This is the kernel of the Laplace project.

![image](docs/image.gif)

## Dependencies

to build the kernel, you need to install the following dependencies:
```sh
sudo apt update
sudo apt install build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo grub-pc-bin grub-common xorriso

mkdir -p ~/src
cd ~/src
wget http://ftp.gnu.org/gnu/binutils/binutils-2.36.tar.gz
tar -xvf binutils-2.36.tar.gz
wget http://ftp.gnu.org/gnu/gcc/gcc-10.2.0/gcc-10.2.0.tar.gz
tar -xvf gcc-10.2.0.tar.gz

mkdir -p ~/src/build-binutils
cd ~/src/build-binutils
../binutils-2.36/configure --target=i686-elf --prefix=/usr/local/i686-elf --disable-nls --disable-werror
make -j$(nproc)
sudo make install

mkdir -p ~/src/build-gcc
cd ~/src/build-gcc
../gcc-10.2.0/configure --target=i686-elf --prefix=/usr/local/i686-elf --disable-nls --enable-languages=c,c++ --without-headers
make all-gcc -j$(nproc)
make all-target-libgcc -j$(nproc)
sudo make install-gcc
sudo make install-target-libgcc

echo 'export PATH=/usr/local/i686-elf/bin:$PATH' >> ~/.zshrc
source ~/.zshrc
```

to run the kernel, you need to install QEMU:
```sh
sudo apt install qemu-system-x86 qemu-utils
```

> [!NOTE]
> If your terminal comes from the VSCode snap package the
> Snap runtime injects library paths (`/snap/core20/...`) into the
> environment which confuse `qemu-system-*` and lead to a symbol lookup
> error for `libpthread.so.0`.  The `qemu.sh` wrapper now clears that
> environment automatically, but you can also work around the problem by
> running the command from a non‑snap shell or by prefixing it with `sudo`.

## Build

```sh
cd LplKernel

./clean.sh
./build.sh --client   # deterministic client profile
./build.sh --server   # throughput server profile
```

### Build ISO per profile

```sh
./iso.sh --client
cp lpl.iso lpl-client.iso

./iso.sh --server
cp lpl.iso lpl-server.iso
```

### One-shot dual-profile pipeline

```sh
./pipeline_profiles.sh
```

Pipeline outputs:

- `lpl-client.iso`
- `lpl-server.iso`
- `lpl-client.kernel`
- `lpl-server.kernel`

## Run

```sh
./qemu.sh --client  # graphics + deterministic policy
./qemu.sh --server  # text + throughput policy
```

### Running using nix

```sh
nix run github:MasterLaplace/LplKernel
```

## Keyboard layout

LplKernel ships several keyboard layouts. The default is **US QWERTY**.

### Choose the layout at build time

Pass `--azerty` (French AZERTY) or `--qwerty` (US QWERTY, the default) to
`build.sh`. The flag combines with the profile flags and is forwarded by
`iso.sh` / `qemu.sh` (they source `build.sh`):

```sh
./build.sh --client --azerty   # deterministic client, French AZERTY
./build.sh --server            # throughput server, US QWERTY (default)
./qemu.sh  --client --azerty   # build + ISO + run, French AZERTY
```

Changing the layout flag is recorded in `.last_build_mode` and triggers an
automatic clean rebuild, so stale objects never leak across layouts.

### Switch the layout at runtime

In **text-mode** builds (`--server` / `--text`) the interactive console exposes
a `layout` command:

```
> layout        # show the active layout
> layout fr     # switch to French AZERTY
> layout us     # switch to US QWERTY
```

> In graphics/client builds the interactive command parser is disabled, so the
> build-time flag (`--azerty` / `--qwerty`) is the way to choose the layout there.

> The guest performs the scancode → character mapping itself, so selecting the
> layout that matches your physical keyboard is sufficient. QEMU's `-k` option
> only affects VNC sessions and is not needed for the local display.

## Debugging

The project is configured for VSCode debugging with GDB and QEMU integration.

### Quick Start

Press **F5** in VSCode to:
1. Build the kernel automatically
2. Launch QEMU with GDB server (`-s -S`)
3. Connect GDB and set breakpoints at kernel startup (`kernel_main`, `kernel_initialize`)
4. Start debugging with full source-level debugging support

### Available Tasks

Use `Ctrl+Shift+P` → "Tasks: Run Task" to access:

- **Build Kernel** - Compile the kernel (`./build.sh`)
- **Build Kernel (Client)** - Compile deterministic client profile (`./build.sh --client`)
- **Build Kernel (Server)** - Compile throughput server profile (`./build.sh --server`)
- **Launch QEMU with GDB** - Start QEMU in debug mode (background task)
- **Run QEMU (No Debug)** - Run kernel without debugging
- **Build ISO** - Create bootable ISO image
- **Build ISO (Client)** - Create client-profile ISO (`./iso.sh --client`)
- **Build ISO (Server)** - Create server-profile ISO (`./iso.sh --server`)
- **Validate Pipeline (Client+Server)** - Build both profiles and generate distinct artifacts
- **Kill QEMU** - Stop running QEMU instances

### Custom GDB Commands

The `.gdbinit` file provides kernel-specific debugging helpers:

```gdb
dump_pd [address]     # Dump page directory entries with flags
dump_gdt              # Display GDT information (segments, base, limit, flags)
show_mode             # Show CPU mode (protected, paging, PAE, interrupts)
```

### Debug Output Files

When debugging, QEMU generates log files in the project root:

- **`serial.log`** - Serial output from the kernel (COM1)
- **`qemu.log`** - QEMU debug logs (interrupts, CPU resets)

### Manual GDB Connection

If you need to connect GDB manually:

```sh
# Terminal 1: Start QEMU with GDB server
qemu-system-i386 -cdrom lpl.iso -s -S

# Terminal 2: Connect GDB
i686-elf-gdb kernel/lpl.kernel
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

### Debugging Tips

- **Breakpoints**: Set breakpoints on functions before hardware initialization
- **Watchpoints**: Use `watch variable` to track memory changes
- **Page Tables**: Use `dump_pd` to inspect paging structures
- **GDT/Segments**: Use `dump_gdt` to verify segmentation setup
- **CPU State**: Use `show_mode` to check protected mode, paging, interrupts
- **Registers**: All CPU registers are displayed automatically at each breakpoint

### Requirements

- Cross-compiler at `/usr/local/i686-elf/bin/i686-elf-gcc` (or update `.vscode/c_cpp_properties.json`)
- QEMU installed (`qemu-system-i386`)
- GDB with i686-elf support

## Portfolio / Live demo

The Laplace project has a showcase site (Astro + Tailwind) in [`site/`](site/) that presents
LplKernel, LplPlugin, and the origin projects (Engine-3D, Flakkari), plus a blog/papers section.
Its headline feature is an **in-browser boot of the kernel** via the [v86](https://github.com/copy/v86)
WASM emulator — visitors can run both the text-console and graphics profiles without installing
anything.

- **Hosting:** GitHub Pages, served from the `gh-pages` branch. The site source stays on `main`
  under `site/`; only the built output is published to `gh-pages` (never polluting `main`).
- **CI:** [`.github/workflows/deploy_portfolio.yml`](.github/workflows/deploy_portfolio.yml) runs on
  every push to `main`. It reuses the cached i686-elf toolchain, rebuilds `lpl-client.iso` /
  `lpl-server.iso` via `pipeline_profiles.sh`, builds the Astro site, embeds the fresh ISOs, and
  publishes to `gh-pages` — so the live demo always matches the latest commit.
- **One-time setup:** after the first successful run, set **Settings → Pages → Source: Branch
  `gh-pages` / (root)**.

Local development:

```sh
cd site
npm install
npm run fetch-v86              # vendor the v86 runtime blobs
../pipeline_profiles.sh        # build the ISOs (from repo root)
cp ../lpl-*.iso public/isos/   # let the local emulator find them
npm run dev
```

## Roadmap

The kernel development and its objectives are listed in the project roadmap. Consult the roadmap to see the planned features, progress status, and next steps:

[ROADMAP](docs/ROADMAP.md)

## References

- [OSDev Wiki](https://wiki.osdev.org/Main_Page)
- [OSDev Notes](https://github.com/dreamportdev/Osdev-Notes/tree/master)

## License

This project is licensed under the GPL-3.0 License - see the [LICENSE](LICENSE) file for details.

## Author

This project is authored by [Master Laplace](https://github.com/MasterLaplace).
