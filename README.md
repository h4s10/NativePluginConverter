# NativePluginConverter

A desktop application for packaging native audio plugins for use in another DAW.
Choose plugins from a local library and export one ZIP per plugin, with its bridge,
configuration and license notices. Original DLLs, presets, fonts and samples
are read from the user’s installed FL Studio, never copied into exported ZIPs.

**Current implementation:** FL Studio native Windows x64 plugins → Windows x64
VST3. The converter runs on Windows, macOS and Linux; exported plugins currently
run **only on Windows x64**. Other DAWs' native plugin formats and other output
systems are future work, not supported conversion targets today.

[Русская документация](docs/README.ru.md) · [Build instructions](docs/BUILD.md) ·
[Compatibility](docs/COMPATIBILITY.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

## Features

- Searchable library with instrument/effect categories and batch export.
- Dependency checks against the selected installation; factory presets are read in place.
- Original native plugin sound and interface hosted by a VST3 bridge.
- Preset browser, sample drag-and-drop where supported, and instrument Channel controls.
- Local processing, background operations and cancellation.
- Optional isolated launch checks on Windows x64.

This repository contains the converter and bridge source, plus MIT-licensed VST3
interface headers. It contains **no Image-Line plugin DLLs, presets, fonts, samples,
FL Studio installation, compiled bridge binaries or converted-plugin archives**.
Use your own installed Windows edition of FL Studio as the input library.

Bridge 0.6.1 refuses reported demo/trial instances and unsupported product-check
requests, and honors native requests to disable parameter automation. This may
also reject purchased plugins that depend on FL Studio's product-check service.
See [runtime restrictions and limits](docs/INSTALLED_FL.md#runtime-restrictions-bridge-061).

## Development quick start

Requirements: Node.js 24 and [Zig 0.14.1](https://ziglang.org/download/0.14.1/).
The native bridge can be cross-compiled from macOS/Linux; it still targets Windows.

```sh
npm ci
npm test
npm run build:native
npm start
```

Zig must be on `PATH`, or `ZIG` must point to its executable. The UI can start
without the native build, but exporting requires the generated bridge. See
[BUILD.md](docs/BUILD.md) for desktop packages and native tests.

## Using the converter

1. Open Settings and select a Windows FL Studio installation folder containing `Plugins/Fruity`.
2. Scan the library and select plugins.
3. Choose an output directory and create the archives.
4. On Windows, close your DAW and copy each complete `.vst3` directory from the ZIP
   into `C:\Program Files\Common Files\VST3`. Rescan plugins in the DAW.

The target computer must have FL Studio installed, including the original plugin.
Do not copy its DLLs into the bridge bundle. Existing ZIPs are not overwritten.

On Windows, scanning a library saves its path locally for all bridges. Standard
installations under `Program Files/Image-Line` can also be detected automatically.
For custom locations, multiple installations, or ZIPs built on macOS/Linux, see
[Installation discovery](docs/INSTALLED_FL.md).
Only explicitly unavailable components are marked in the library; detailed
resource analysis and launch results are under **Composition and checks**
(`Зависимости и проверка` in the current Russian interface).

## Project layout

| Directory | Purpose |
| --- | --- |
| `ui/` | Electron interface |
| `src/` | Desktop process, worker, inventory, dependency resolver and ZIP packaging |
| `native/bridge/` | Windows x64 native ABI / VST3 bridge |
| `native/third_party/pluginterfaces/` | Vendored VST3 interface headers and license |
| `tests/`, `native/tests/` | Synthetic unit tests and native/integration probes |
| `resources/` | Generated bridge/probe and initial empty compatibility cache |
| `scripts/` | Build, smoke-test and source-publication checks |

The present backend is specific to FL Studio. Additional backends, formats and
platforms will need their own implementation and testing; renaming the product
does not make the existing native bridge portable.

## License and third-party material

Project-owned code is licensed under **MIT**; see [LICENSE](LICENSE).
The VST3 headers and toolchain components retain their own licenses and notices.
Retain the MIT copyright/license notice and the applicable third-party notices
when distributing copies or binaries.

FL Studio plugins and their assets remain third-party proprietary material. The
project's license does not grant rights to redistribute them. New converted ZIPs
contain only bridge code/configuration/notices. Do not add vendor files to these
archives, source commits, issues or releases. Changing the project license and
loading installed files does not by itself resolve questions under the vendor's
EULA or the law governing reverse engineering.
NativePluginConverter is independent and is not affiliated with Image-Line or
Steinberg. Product names are used to identify compatibility.
