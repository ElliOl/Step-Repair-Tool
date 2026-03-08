# StepFixer

Electron desktop app that repairs STEP files exported from Plasticity (via HOOPS Exchange) so they import correctly into SolidWorks, Creo, Keyshot, and other professional CAD tools.

## Fixes

1. **Part name repair** — PRODUCT entities with name `'0'` are replaced with the real part name from NAUO instance labels.
2. **Disconnected shell split** — Solids that contain multiple geometrically disconnected face regions are split into separate solids.
3. **HOOPS Exchange compatibility** — Per-face color overrides that cause partial MDGPR coverage are stripped; some readers (Creo, Keyshot) otherwise misinterpret this as a second geometric body and import the part as "2 sheets".

## Requirements

- Node.js 18+
- OpenCASCADE 7.8.1 at `$HOME/Libraries/opencascade/7.8.1` (or adjust `native/binding.gyp` and `electron/native-bridge.ts` for your install)
- macOS 10.15+ (or Linux/Windows with matching OCCT build)

## Setup

```bash
npm install
cd native && npm install && npm run build && cd ..
```

## Development

```bash
npm run dev
```

## Build

```bash
npm run build
npm run build:native
npm run dist   # or npm run pack for unpacked app
```

## Project structure

- `electron/` — Main process, preload, native addon bridge
- `src/` — React UI (Trace-style layout), CAD viewer (R3F), stores, hooks
- `native/` — C++ addon (node-gyp): STEP read/write, name repair, shell split, HOOPS compat fix, tessellation for viewer

## License

MIT
