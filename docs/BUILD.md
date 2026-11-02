# Building NativePluginConverter

## Requirements

- Node.js 24, npm and a checkout of this repository.
- Zig **0.14.1** from <https://ziglang.org/download/0.14.1/> for the Windows bridge.
- A Windows FL Studio installation only for conversion or real-plugin tests.

`npm ci` installs the versions in `package-lock.json`, including Electron.
The first install/build may download its desktop runtime. No Python, FL SDK,
FL Studio DLLs, absolute developer paths or proprietary assets are needed to
compile this project's bridge.

## Build and run

```sh
npm ci
npm test
npm run build:native
npm start
```

If Zig is not on PATH, set its absolute executable path before building:

```sh
# macOS / Linux
export ZIG=/path/to/zig
npm run build:native
```

```powershell
# Windows PowerShell
$env:ZIG = 'C:\Tools\zig\zig.exe'
npm run build:native
```

The script compiles `native/bridge/vst3.cpp` and `engine.cpp` into
`resources/BridgeTemplate.vst3`, and `probe.cpp` plus `engine.cpp` into
`resources/BridgeProbe.exe`. It verifies Windows x64 PE headers and the
VST3 factory export before installing the outputs. Both files are ignored by Git.
The bridge's version is 0.6.1; the desktop app's version is 0.8.0.

## Desktop packages

After building the native resources:

```sh
npm run build:win
npm run build:mac
node scripts/build.js darwin x64
npm run build:linux
```

Outputs are in `dist/NativePluginConverter-<platform>-<arch>/`.
macOS packages must be built on macOS and receive an ad-hoc signature;
Developer ID / notarization and Windows code signing are not configured.
Distribute the complete application directory, not a single executable.
The packaging script includes project and third-party notices.
Builds target Windows x64, macOS arm64/x64 and Linux x64.

## Tests

- `npm test`: synthetic dependency, asset, archive, state and settings tests.
  No vendor DLLs or native runtime are required.
- `npm run test:native`: builds the mock adapter, channel-engine and installed-path tests. On
  Windows x64 it also executes them. On other systems, run the generated
  `build/native/AdapterTest.exe instrument`, `AdapterTest.exe effect`, and
  `ChannelTest.exe`, `LicensePolicyTest.exe` and `InstalledConfigTest.exe` using a Windows environment. Compilation alone is not a pass.
- `npm run check:repo`: checks the files Git would publish, excluding ignored
  local build outputs. This is a targeted publication check, not a general secret scanner.

`scripts/smoke-electron.js` is an optional integration test for the maintainer's
140-entry FL Studio reference installation and the Distructor preset library.
It is not a test for arbitrary user installations and is not part of public CI.
Set `FL_TEST_ROOT` to your Windows FL folder and `FL_TEST_OUTPUT` to an empty
scratch directory outside that folder, then run:

```sh
npx electron scripts/smoke-electron.js
```

It creates ZIPs, screenshots and a temporary app profile. Keep those outputs
outside version control. Additional tests in `native/tests/` document native
behavior; some require real vendor modules/presets. See their arguments and
`native/bridge/ADAPTER_NOTES.md` before running them.

## Source releases

Keep compiled applications and converted-plugin ZIPs out of Git. For releases
of project-owned binaries, retain the MIT and third-party license notices.
Providing matching source snapshots and build instructions is recommended. Never include
vendor plugin DLLs or assets in this project's releases. A moving `main` branch
alone is not a substitute for the exact source used for a released binary.
