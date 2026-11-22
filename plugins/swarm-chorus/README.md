# Swarm Chorus

> **[⬇ Download the plugins here](https://github.com/rafrowinski/vst-plugins/releases)** - ready-to-use Windows files.
> On that page, open the plugin you want and click the `.zip` file under **Assets**.

Multi-voice VST3 chorus (6 voices, stereo) with detune, feedback, a hue/wah filter per voice, EQ,
start on pick attack, envelope control, tempo sync, a hand-drawn custom wave
and a graph of each voice's delay time (the curves can be dragged with the mouse).

Version 1.1: Warmth, Envelope, Tempo sync, stereo input, editing on the graph, custom wave, Random, A/B.

## Installation (Windows)

Copy `Swarm Chorus.vst3` to `C:\Program Files\Common Files\VST3\` and in REAPER run
Options > Preferences > Plug-ins > VST > Re-scan.

User presets are saved in `%APPDATA%\Swarm Chorus\user-presets.csv`.

## Custom wave (Shape = Custom wave)

The "Edit wave..." button opens the editor. A wave is either points (drag to move, double-click to add,
right-click to remove; smooth curve or straight segments) or a formula in the variable `x` (0..1 over one cycle).
Either way the plugin converts the wave once into a 256-value table and only reads from it while playing.

Text form of a wave (the `wave` column in CSV, the `wave` state in a project):

- `n:x,y;x,y;...` - points joined by a smooth curve
- `l:x,y;x,y;...` - points joined by straight segments
- `f:formula` - e.g. `f:sin(2*pi*x)^3`; the result is scaled to fill the whole Min..Max range

In a formula: `+ - * / ^`, parentheses, `pi`, `e`, the functions `sin cos tan asin acos atan sinh cosh tanh abs sqrt exp ln log10
floor ceil round frac sign min max pow mod clamp mix step`, and ready-made waveforms with period 1: `tri saw sqr`.

## Preset CSV format

One row = one preset. The first column is the name, the next ones are parameters (the header holds
their symbols, e.g. `mix`, `sp_phase`, `v1_rate`, `v3_detune`), the last one is `wave`. Separator `;`, decimal point.
Import also accepts `,` or tab as the separator, a decimal comma (e.g. after saving from Excel),
any column order and missing columns (they get default values). Files from versions 1.1/1.2
(columns `cw1`..`cw16`) are converted to 16 points.

Duplicates on import: same name and same settings -> skipped;
same name, different settings -> added as `Name (2)`, `Name (3)`...

## Code

- `src/Params.hpp`   - parameter list, LFO shapes, factory presets
- `src/Wave.hpp`     - custom wave: points, formula parser, compiling to a table
- `src/Engine.hpp`   - DSP (no framework dependencies)
- `src/PresetIO.hpp` - preset library and CSV
- `src/PluginDSP.cpp`, `src/PluginUI.cpp` - plugin and user interface (DPF + Dear ImGui)
- `tests/`           - engine and wave tests that run without a host (see below)

## Tests

    g++ -O2 -std=c++17 tests/engine_test.cpp -o engine_test && ./engine_test
    g++ -O2 -std=c++17 tests/wave_preset_test.cpp -o wave_preset_test && ./wave_preset_test

The first runs a test signal through every preset (looks for NaN, measures level and CPU load)
and checks detune, stereo, Warmth, sync and the custom wave. The second checks the formula parser, points and CSV.

## Building

All plugins are built together from the whole repository - see the [main README](../../README.md#building).
This plugin only (Linux -> Windows, this is how the included file was made), from the repository root:

    cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build-win --target swarm-chorus-vst3

## License

The plugin code is MIT licensed - see [LICENSE](../../LICENSE). Third-party code (DPF, DPF-Widgets, Dear ImGui,
DejaVu Sans font) lives in `shared/` under its own licenses - details in the [main README](../../README.md#license).

## Trademarks

VST is a trademark of Steinberg Media Technologies GmbH. Device names in presets
(e.g. "CE-2 style", "Rockman Chorus style") only describe the character of the sound. The plugin is not
affiliated with or endorsed by the makers of those devices.
