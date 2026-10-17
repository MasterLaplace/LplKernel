# v86 runtime

This directory holds the [v86](https://github.com/copy/v86) WASM x86 emulator runtime
(MIT-licensed) used to boot LplKernel in the browser.

The blobs are **not committed**. Fetch them with:

```sh
npm run fetch-v86    # or: sh scripts/fetch-v86.sh
```

Expected files after fetching:

- `libv86.js` — emulator loader/API
- `v86.wasm` — the emulator core
- `seabios.bin` — BIOS
- `vgabios.bin` — VGA BIOS

The deploy workflow (`.github/workflows/deploy_portfolio.yml`) runs the same script,
and copies `lpl-server.iso` / `lpl-client.iso` into `../isos/` before building.
