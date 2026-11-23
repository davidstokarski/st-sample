# ST Sample — guide for agents

ST Sample is a drum sampler instrument (16 pads, 4 layers per pad, XY blend). It is built on the shared module in
`../st-shell` (https://github.com/davidstokarski/st-shell). Before changing anything, read the shell docs:

- `../st-shell/CLAUDE.md` — non-negotiables (design, real-time safety, native menus, verification)
- `../st-shell/docs/DESIGN.md`, `../st-shell/docs/ARCHITECTURE.md`, `../st-shell/docs/EXAMPLES.md`
- `../st-sat` is the reference plugin; `../st-dc` for the list sidebar, `../st-split` for glass bars and the tray.

Plugin-specific notes:

- Instrument: `st_add_plugin (... IS_SYNTH VST3_CATEGORIES Instrument Drum Sampler)` → AU type `aumu`
  (`auval -v aumu Stsm StAu`). No input bus; `isBusesLayoutSupported` accepts mono / stereo out.
- Parameters: `Source/Params.*` (IDs are permanent — never rename them). Per pad `p<n>_<suffix>`, per layer
  `p<n>_<a..d>_<suffix>`. `blendWeights` (XY → layer gains) and `velocityGain` live there and are shared by the
  engine and the UI. Note names: `notes::name / parse`, middle C octave is a UI preference (`uiState["middleC"]`).
- Kit: `Source/Kit.*`. The truth is `apvts.state`'s `KIT/PAD/LAYER` tree (source = absolute path or
  `factory:<name>`, pad `name`), so presets, A/B, sessions and undo carry it. `reconcile()` turns the tree into
  samples: decoding on the loader thread, install on the message thread, replaced samples kept in a graveyard and
  freed on the message thread once no voice holds them. Tests use `waitUntilLoaded()`.
- DSP: `Source/dsp/Engine.*` — voice pool, Hermite interpolation, start/length with fades at cut points, per-chunk
  gain ramps, choke groups, gate / one-shot, polyphony stealing. The audio thread only try-locks the sample table
  when a voice starts and copies `shared_ptr`s; it never frees sample memory. Offline-testable.
- Built-in sounds: `Source/dsp/FactorySounds.cpp` (deterministic synthesis). Factory kits: `Source/FactoryKits.cpp`
  (presets with a `KIT` child; index 1 "ST Kit" is what a new instance loads, "Init" is the empty kit).
- UI: `Source/ui/PadList` (pads), `PadView` (selected pad: header, `WaveDisplay`, `XYPad`, Level / Velocity,
  `LayerPanel` x4; rebuilt per selection and cross-faded), `Keyboard` (MIDI map), `Footer`, `Style` (ink-violet
  theme, layer colours coral / gold / teal / sky). Components talk to the editor through `Services` (`ui/Common.h`).
- Verify: `StSamplePluginTests` (engine, loading, undo, state, A/B, presets, block sizes, CPU),
  `StSamplePluginTests --snapshot <dir>` (look at the PNGs), `auval -v aumu Stsm StAu`.
  Website videos: `StSamplePluginTests --usecases <dir> [scene ...]` (`tests/UseCases.cpp`, on the shell's
  `st_shell/testing/st_video.h`, a MIDI groove plays the kit): scenes layer-kick, tune-kit.
- Anything reusable belongs in st-shell, not here.
