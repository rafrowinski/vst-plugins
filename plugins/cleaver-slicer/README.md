# Cleaver Slicer

A VST3 plugin (Windows 64-bit) of the pattern slicer / stutter kind for guitar, inspired by the idea of the Boss Slicer:
a step sequencer of up to 32 steps chops the sound into rhythmic patterns, with pitch shift, filter and pan per step.

## Installation

The ready plugin is in `release/Cleaver Slicer.vst3`. Copy it to `C:\Program Files\Common Files\VST3\` and re-scan plugins in REAPER.

## User presets

**Save** stores the pattern and settings under the given name (without tempo and Host sync), **Delete** removes the selected user preset.
Presets live in the text file `%APPDATA%\Cleaver Slicer\user-presets.txt` - one preset per line, fields separated by tabs.
The file can be copied to another computer.

## Files

- `src/SlicerShared.hpp` - parameters, pattern format, factory patterns
- `src/SlicerEngine.hpp` - DSP engine (framework independent)
- `src/UserPresets.hpp` - saving and loading user presets
- `src/PluginDSP.cpp` - DPF wrapper
- `src/PluginUI.cpp` - user interface (Dear ImGui)
- `test/dsp_test.cpp` - offline engine test, `test/host.cpp` - VST3 load test, `test/preset_test.cpp` - preset file test

## Tests

    g++ -O2 -std=c++17 -Isrc test/dsp_test.cpp -o dsp_test && ./dsp_test
    g++ -O2 -std=c++17 -Isrc test/preset_test.cpp -o preset_test && ./preset_test

## Building

All plugins are built together from the whole repository - see the [main README](../../README.md#building).
This plugin only (Linux -> Windows, this is how the included file was made), from the repository root:

    cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build-win --target cleaver-slicer-vst3

## License

The plugin code is MIT licensed - see [LICENSE](../../LICENSE). Third-party code (DPF, DPF-Widgets, Dear ImGui,
DejaVu Sans font) lives in `shared/` under its own licenses - details in the [main README](../../README.md#license).

## Trademarks

VST is a trademark of Steinberg Media Technologies GmbH. Boss is a trademark of Roland Corporation; the plugin is
not affiliated with or endorsed by Roland.
