# Generic VST3 template

`bridge.ini` supplies UTF-8 display name, stable class UID, instrument/effect
kind, source mode and relative DLL/dependency paths. New exports use
`source=installed`; metadata scans validate syntax without requiring an installed
FL Studio or executing DLLs. Instance initialization resolves the original files
using `installed_fl.h`. See `docs/INSTALLED_FL.md` for lookup precedence.
Paths cannot escape the selected installation, including through junctions.
Older bundle-relative configurations remain readable, but are no longer exported.
Invalid/missing configuration exposes zero classes. A missing installation fails
instance initialization, leaving the config's class identity intact.

The component exposes stereo float32 output and a note input. Effects also expose
stereo input. Input is copied into preallocated scratch buffers before rendering,
including for host input/output buffer aliasing. Host silence flags are honored;
missing or inactive input is silence. Event and parameter sample offsets split
rendering. Parameter queues retain their preallocated per-parameter cursors.

Factory identity and category are per bundle. State version 2 includes the
16-byte configured UID before the engine blob, so another converted plugin's
state is rejected. This is a separate product from the prior Granulizer wrapper
and has a separate identity/state envelope. Native state serialization remains
the engine's responsibility.

The original HWND editor is hosted and its size is requested from the host when
it changes. The WAV load button and replacement drop handler are installed only
when Engine::supportsSampleLoad() permits them. Otherwise native controls retain
their original drag/drop behavior. Windows HWND is the sole platform UI type.
Native latency is exposed through getLatencySamples(). MIDI CC, pitch bend,
note expression, additional audio buses and sidechain are not implemented here.

Validation: generic_adapter_test.cpp uses a fake engine and must be run in a
scratch directory, because it creates/deletes bridge.ini beside its executable.
Run once with `instrument` and once with `effect`. Both passed under CrossOver
with Zig0.14.1 optimized Windows x64 compilation. Coverage includes factory UID,
Unicode metadata/category, bus arrangements, effect input/output aliasing and
silence, latency, note/automation offsets, zero-frame updates, partial streams,
cross-plugin/malformed state rejection and lifecycle. These are adapter contract
tests, not native FL ABI compatibility tests.

`tests/native_vst3_test.cpp` is a separate real-module integration probe. Compile
it with bridge/engine.cpp and the same Windows/VST3 headers and libraries as the
bridge. Command:

    NativeVST3Test.exe FULL-PATH-TO-INNER-MODULE.vst3 [--sample WAV] [--gui] [--allow-silent] [--peer OTHER-INNER-VST3]

The optional sample uses the engine API only to construct the initial native
state. Factory creation, audio processing, buses, state roundtrip, identity
rejection and editor lifecycle are exercised through the dynamically loaded
VST3. Two instances run at 44.1/48 kHz with variable blocks; the effect instance
at 48 kHz aliases host input/output buffers. Nonfinite and excessive samples
fail. Nonzero output is required unless --allow-silent is explicit. GUI mode
opens and reopens the real editor briefly; this tests lifecycle, not visual QA.
A process crash/timeout is a failure, not compatibility evidence.

Final packaged-module integration passed for Fruity Balance, Fruity Granulizer,
3x Osc and Sytrus after real ILTools path-manager integration. The 3x Osc run
also loaded Sytrus concurrently, verified different identities and audio from
both, unloaded Sytrus, then continued rendering 3x Osc. Module hashes and logs
are recorded in verification/native-vst3-final.json.

After the final GP7 Shared-path fix and FLEX Maximus dependency profile, three
real VST3 integration runs passed again: Fruity Balance, FLEX, and 3x Osc with
Sytrus loaded concurrently as a peer. Final-module hashes and exact per-plugin
coverage are in verification/native-vst3-final.json. Earlier full Granulizer
and Sytrus editor/state runs retain their original binary hashes in
verification/native-vst3-historical.json; they are not relabeled as final-build
full tests. FLEX final test rendered nonzero output at both rates and reopened
its editor twice, with no optional silence allowance.
