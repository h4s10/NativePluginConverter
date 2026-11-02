# Compatibility and limits

The only conversion backend currently implemented is FL Studio native Windows
x64 → VST3 Windows x64. The Electron app is cross-platform; the bridge is not. Exports contain no vendor
DLLs, presets or samples. The target computer must have its own compatible
FL Studio installation; moving the ZIP alone does not move the original plugin.

- Plugin support varies. Inventory discovery is not proof of working audio/UI.
- Wrapper, Patcher, VFX and FL-host routing/controller services are unavailable
  where they require the original host. Unsupported components stay visible.
- Sample drag-and-drop depends on plugin and source application behavior.
- Native factory presets and selected external assets are read from the target computer’s selected installation.
  Missing or ambiguous references are reported rather than guessed.
- Channel settings provide polyphony, Mono, legato Porta, root note, transpose,
  fine tuning, gain and pan. Full porta, FL arpeggiator, Cut/By groups,
  Echo/Fat and Time/Swing are not implemented.
- MIDI CC/pitch-bend mapping, additional audio buses and sidechain are not
  implemented by this adapter. Some auxiliary Edison/AudioRestore windows
  and host-specific services remain limited.
- The bridge does not intentionally disable activation/trial mechanisms. Full
  licensing behavior has not been independently audited. MIT does not change
  the vendor’s EULA or resolve reverse-engineering questions.
- Saved plugin identity remains stable across the product rename.

The source repository starts with an empty compatibility cache. Runtime checks
run only on Windows x64 and are saved in the local user profile. A successful
probe covers creation, basic render/state and editor lifecycle; it does not
establish full feature support in every DAW. CrossOver/Wine results should be
reported as such, separately from direct Windows DAW tests.
