# Third-party notices

Project-owned code is licensed under MIT (see LICENSE). Third-party
components retain their own licenses; this file does not relicense them.

| Component | Location / delivery | License |
| --- | --- | --- |
| Steinberg VST3 interface headers | `native/third_party/pluginterfaces/` | MIT, full text in that directory and `LICENSES/Steinberg-VST3.txt` |
| LLVM libc++ runtime used by Zig builds | Native bridge binary, when built | Apache-2.0 WITH LLVM-exception and retained legacy notices, `LICENSES/LLVM-libcxx.txt` |
| mingw-w64 runtime used by Zig builds | Native bridge binary, when built | ZPL-2.1 and per-component terms, `LICENSES/mingw-w64.txt` |
| Electron | Installed through npm, included in desktop packages | MIT plus Chromium/other third-party notices shipped with the Electron distribution |
| yazl | Installed through npm | MIT; `node_modules/yazl/LICENSE` |
| Node/npm build dependencies | Installed through npm | Each package's own license; exact versions in `package-lock.json` |

The VST3 interface license bundled here is MIT. Preserve upstream copyright
headers and license text when redistributing the vendored files or binaries.
Official information: <https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Licensing.html>.

Image-Line/FL Studio binaries, presets, fonts, samples and other vendor content
are not included and are not covered by the project's MIT license. Users supply
their own local installation. Exports contain only project-owned bridge code and notices; vendor DLLs/assets
are loaded from the installed FL Studio and never copied into the ZIP. The MIT
license does not authorize reverse engineering or use of vendor software outside
its applicable terms.

Do not remove Electron's `LICENSE`, `LICENSES.chromium.html` or its bundled
third-party notices from desktop packages. Distribute the native toolchain and
VST3 notices with any project-owned native binary.
