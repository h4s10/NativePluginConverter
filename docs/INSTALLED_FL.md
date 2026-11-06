# Loading an installed FL Studio

Starting with desktop 0.8.0 / bridge 0.6.0, newly exported ZIPs contain only the
project's Windows x64 VST3 bridge, `bridge.ini`, metadata and license notices.
No vendor DLLs, helper EXEs, presets, fonts, samples or artwork are copied.
The launch probe also reads the selected installation directly, without staging
vendor files. Presets/resources remain in the original installation.

## On the target Windows computer

Install and license your own FL Studio and the original plugin. Copy the full
exported `.vst3` folder into your VST3 directory. The bridge searches in this order:

1. An explicit `[installation] root` in its `bridge.ini` (optional).
2. The `NPC_FL_STUDIO_ROOT` environment variable (optional).
3. `%LOCALAPPDATA%\NativePluginConverter\fl-studio-path.txt`, written when you
   scan a library in NativePluginConverter on Windows.
4. A single matching installation under the standard `Program Files/Image-Line`
   locations. If several match, select one explicitly instead of guessing a version.

An explicit but invalid/stale selection fails instead of silently choosing a
new version. Re-scan the correct folder in the converter or update the selection,
then restart the DAW. If no installation matches, plugin initialization fails;
the converter's launch check reports why. No folder dialog is opened by a DAW scan.

You can also edit the INI beside the **inner** `.vst3` binary, for example:

```ini
[installation]
root = D:\Audio\FL Studio 2025
```

Use an absolute local Windows drive path. UNC/network paths are not supported.
The configured DLL and dependency paths must stay inside the chosen installation;
junction/symlink escapes are rejected. The bridge does not load a bundled fallback.
Do not add DLLs to the VST3 directory.

## Creating archives on macOS or Linux

You can analyze a copy of a Windows FL Studio installation and export the bridge.
Its absolute path is **not** written into the ZIP. On the destination Windows
computer, install FL Studio and select it as described above. Matching plugin and
resource versions are recommended; an FL update/removal can affect saved projects.

## Existing exports

Existing VST3 class IDs and state formats remain stable. Older bundled archives
are not rewritten or deleted automatically. To move to this mode, create a new
archive with 0.8.0+, close the DAW and replace the entire old `.vst3` folder, so old
vendor files are not left behind. The native reader retains support for older
configuration files for existing projects/tests, but the converter no longer
creates or validates copied-vendor bundles.

## Licenses

The MIT license covers project-owned code only. This loading mode avoids
redistribution of FL Studio files; it does not grant rights under Image-Line's
EULA, prove reverse engineering is permitted, or guarantee activation/trial behavior.

## Runtime behavior (bridge 0.6.3)

Bridge 0.6.3 removes the bridge-owned rejection introduced in 0.6.1 for
`FPF_DemoVersion` (bit 15) and `FHD_CheckProdCode` (host dispatcher 26).
An otherwise valid instance can open and receive audio, editor, preset and state
calls in its native mode. No native flag is cleared, no activation data is
manufactured, and no unlock-demo service is invoked.

The unsupported product-check callback returns zero without reading or writing
its opaque value. This is the historical unhandled-service response, not a
license result. The original plugin may still enter trial mode or refuse an
operation. The bridge does not independently validate activation. Native state
API calls are delegated as before; a successful call does not prove that every
plugin accepted and completely restored a project.

`FHD_KillAutomation` (host dispatcher 8) disables the inclusive parameter range
specified by the plugin, even if requested during instance creation. Incoming DAW
parameter writes and outgoing editor automation for those parameters are ignored,
including pending editor notifications. Updated parameter flags are reported to
the DAW from the editor idle callback. Loading another preset/state does not
clear the restriction. The bridge cannot delete existing automation lanes from
the DAW project; it refuses their parameter writes.

Cached launch checks from earlier bridge versions are invalidated. Rebuild the
bridge and desktop app, then export new VST3 folders to obtain this behavior;
existing exports and published assets are unchanged.

Protocol names and numeric identifiers can be compared with the published
[FL interface declarations in Dplug](https://github.com/AuburnSounds/Dplug/blob/master/formats/dplug/flp/types.d).
No Dplug implementation or activation code is included in this change.

The synthetic `LicensePolicyTest.exe` verifies that native flags and product-check
values remain untouched, state/audio calls are delegated, and automation
restrictions are retained. These tests verify bridge behavior, not the original
plugins' licensing systems or full DAW compatibility.

## When a DAW says the plugin could not be opened

Starting with bridge 0.6.2, initialization failures write a local report to:

```text
%LOCALAPPDATA%\NativePluginConverter\Logs\bridge-<plugin-id>.log
```

Paste the folder path into Windows Explorer. Each report identifies the plugin,
configuration, UTC timestamp and failure reason. The latest failure overwrites
that plugin's previous report, so repeated DAW scans do not grow the file.
Reports stay on your computer and may contain local paths; review them before
sharing. If this directory is not writable, failure reporting is skipped and
the original startup failure is preserved.

Alternatively, run the launch check under **Зависимости и проверка** in the
converter on Windows x64. A DAW's generic message alone cannot distinguish a
missing installation from an unsupported host service.

Edison from the tested FL Studio installation requests `FHD_CheckProdCode` during
creation. Bridges 0.6.1–0.6.2 reject this service before audio or state processing;
this rejection is not proof that Edison is unlicensed. Bridge 0.6.3 removes the
bridge-owned rejection and leaves the original plugin's flags unchanged.
Export Edison again and replace its complete VST3 bundle to update an existing
installation. An old startup log is historical and is not a fresh launch result.

## Empty squares instead of waveform icons

Bridge 0.6.2 adds a fallback for missing font-file lookups in QuickFontCache.
Some plugins, including 3x Osc, ask for an FL font in the Windows fonts folder
even though it is present under the selected FL Studio installation. The bridge
resolves the font from that installation; it does not install system fonts,
copy vendor fonts into the VST3 archive, or change the DAW's working directory.

After updating the converter, export the affected plugin again, close the DAW,
and replace its complete VST3 bundle. Existing exports do not update themselves.
The selected FL Studio installation must still contain its original font files.
