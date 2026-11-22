# Swarm Delay

> **[⬇ Download the plugins here](https://github.com/rafrowinski/vst-plugins/releases)** - ready-to-use Windows files.
> On that page, open the plugin you want and click the `.zip` file under **Assets**.

Multi-voice VST3 delay (6 voices, stereo), built on the Swarm Chorus framework.

- every voice is a separate delay line with its own feedback: Time (ms or a note value), Feedback,
  Reverse, Pitch (semitones in the loop), Detune, Level, Pan, Crush
- **Time x** with **Tape** (pitch bends like tape) and **Jump** (jump and stutter) modes
- **Freeze** - freezes and loops the contents of the lines; Time x, Reverse, Pitch, chorus and crusher act on it
- chorus on the repeats (Detune / Rate / Mix), crusher (Bits / Rate, in the loop or after it)
- filter in the loop (Lo cut / Hi cut), Warmth, ping-pong, tempo sync
- ducking (the repeats get quieter while you play)
- random glitches: reverse, jump to half time, stutter
- Spread (Time / Detune / Pan), presets with CSV export and import, Random, A/B
- **Time sweep**: every cycle (ms or a note value) the time of all voices jumps and comes back - Up (time grows) or Down (time shrinks); with "One way" the time only moves in one direction
- each voice's time in ms or as a note value (1/8. etc.), set per voice
- a graph of all repeats (top = left channel, bottom = right); the first repeat of a voice can be dragged

## Installation (Windows)

Copy `Swarm Delay.vst3` to `C:\Program Files\Common Files\VST3\` and in REAPER run
Options > Preferences > Plug-ins > VST > Clear cache/re-scan.

User presets are saved in `%APPDATA%\Swarm Delay\user-presets.csv`.

## Preset CSV format

One row = one preset. The first column is the name, the next ones are parameters (the header holds
their symbols, e.g. `mix`, `time_mul`, `v1_time`, `v3_pitch`). Separator `;`, decimal point.
Import also accepts `,` or tab as the separator, a decimal comma (e.g. after saving from Excel),
any column order and missing columns (they get default values).

Duplicates on import: same name and same settings -> skipped;
same name, different settings -> added as `Name (2)`, `Name (3)`...

Freeze is not part of a preset: presets, Random and A/B leave it alone.

## Code

- `src/Params.hpp`   - parameter list, effective voice values, factory presets
- `src/Engine.hpp`   - DSP (no framework dependencies)
- `src/PresetIO.hpp` - preset library and CSV
- `src/PluginDSP.cpp`, `src/PluginUI.cpp` - plugin and user interface (DPF + Dear ImGui)
- `test/engine-test.cpp` - offline engine tests: `g++ -std=c++17 -O2 -Isrc test/engine-test.cpp -o t && ./t`

## Building

All plugins are built together from the whole repository - see the [main README](../../README.md#building).
This plugin only (Linux -> Windows, this is how the included file was made), from the repository root:

    cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build-win --target swarm-delay-vst3

## License

The plugin code is MIT licensed - see [LICENSE](../../LICENSE). Third-party code (DPF, DPF-Widgets, Dear ImGui,
DejaVu Sans font) lives in `shared/` under its own licenses - details in the [main README](../../README.md#license).

## Trademarks

VST is a trademark of Steinberg Media Technologies GmbH.
