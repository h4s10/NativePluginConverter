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

## Runtime restrictions (bridge 0.6.1)

The bridge rejects native instances that report `FPF_DemoVersion` (bit 15).
It also stops an instance when it requests `FHD_CheckProdCode` (host dispatcher
26): that reserved service has no supported activation contract in this bridge.
The callback does not fabricate activation data. Its ABI return value is not
interpreted as proof of a valid license. This conservative policy can reject
purchased plugins too; use those plugins in FL Studio.

The checks run before and after native calls, including state and preset loading.
A restriction observed during a call remains latched until the instance is closed
and explicitly opened again. Blocked instances produce silence, reject further state save/restore requests,
and stop further native processing. Native teardown
is still allowed. A restriction raised within a native call cannot cancel the
already-running call: rendered output and saved state are discarded, and a restore
result is rejected. Any internal change already made by that restore is not rolled
back; the instance remains unusable.

`FHD_KillAutomation` (host dispatcher 8) disables the inclusive parameter range
specified by the plugin, even if requested during instance creation. Incoming DAW
parameter writes and outgoing editor automation for those parameters are ignored,
including pending editor notifications. Updated parameter flags are reported to
the DAW from the editor idle callback. Loading another preset/state does not
clear the restriction. The bridge cannot delete existing automation lanes from
the DAW project; it refuses their parameter writes.

The launch check reports the rejection reason. Cached checks from bridge 0.6.0
are invalidated. Rebuild the bridge and desktop app and export new VST3 folders
to obtain this behavior; published 0.8.0 assets and existing exports are unchanged.

These are checks for known restrictive signals, not a complete license validator.
Absence of a demo flag is not proof of ownership, activation or EULA permission.
Nested modules and product-specific trial behavior still require separate testing;
the bridge does not invoke the reserved unlock-demo service. Unsupported host
warning/error callbacks are a remaining audit limitation.

Protocol names and numeric identifiers can be compared with the published
[FL interface declarations in Dplug](https://github.com/AuburnSounds/Dplug/blob/master/formats/dplug/flp/types.d).
No Dplug implementation or activation code is included in this change.

The synthetic `LicensePolicyTest.exe` exercises the restrictions without vendor
DLLs. Adapter tests additionally cover VST3 automation queues and metadata.
These tests verify bridge behavior, not the original plugins' licensing systems.
