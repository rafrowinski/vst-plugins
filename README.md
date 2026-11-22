# VST plugins

> **[⬇ Download the plugins here](https://github.com/rafrowinski/vst-plugins/releases)** - ready-to-use Windows files.
> On that page, open the plugin you want and click the `.zip` file under **Assets**.

Audio plugins for guitar (Windows 64-bit), built with [DPF](https://github.com/DISTRHO/DPF) and [Dear ImGui](https://github.com/ocornut/imgui).

| Plugin | What it does | Formats |
|---|---|---|
| [Swarm Chorus](plugins/swarm-chorus) | multi-voice chorus (6 voices, stereo), hand-drawn custom wave | VST3 |
| [Swarm Delay](plugins/swarm-delay) | multi-voice delay (6 voices, stereo) | VST3 |
| [Swarm Synth](plugins/swarm-synth) | guitar synthesizer: up to 8 oscillators playing what the guitar plays | VST3 |
| [Guitar to MIDI Live](plugins/guitar-to-midi-live) | guitar to MIDI in real time, to drive any instrument | VST3, VST2, CLAP |
| [Cleaver Slicer](plugins/cleaver-slicer) | pattern slicer / stutter, step sequencer of up to 32 steps | VST3 |

## Download

Ready-to-use builds are in [Releases](https://github.com/rafrowinski/vst-plugins/releases): one zip per plugin
(Windows 64-bit) with its README and license. Unzip and copy the `.vst3` file to
`C:\Program Files\Common Files\VST3` (VST2 / CLAP: see the plugin's README).
The same files are also in each plugin's `release/` folder.

## Layout

- `plugins/<name>/` - code, tests, README and ready-made files (`release/`) of one plugin
- `shared/dpf/` - [DPF](https://github.com/DISTRHO/DPF) (commit `d849424c`), one copy for all plugins
- `shared/dpf-widgets/` - [DPF-Widgets](https://github.com/DISTRHO/DPF-Widgets) (commit `730da639`) with one change:
  in `opengl/DearImGui.cpp` the font glyph range covers Polish letters (Latin Extended-A), dashes, arrows
  and shapes used by the plugins
- `shared/guitar-engine/` - the pitch detector and synth engine shared by Swarm Synth and Guitar to MIDI Live,
  together with the saved detector settings (shared file `%APPDATA%\Guitar Detector\detector-presets.csv`)

## Building

All plugins at once, Linux -> Windows (this is how the files in `release/` were made):

    sudo apt install mingw-w64 cmake
    cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build-win -j8

One plugin: `cmake --build build-win --target swarm-synth-vst3` (targets: `<name>-vst3`, and for Guitar to MIDI Live
also `-vst2` and `-clap`). On Windows: MSYS2 (MinGW64) or Visual Studio with CMake, the same `CMakeLists.txt`
without the toolchain file. On Linux without the toolchain you also get JACK versions (standalone programs) for testing.

Each plugin's tests run without a host - the commands are in the plugin's README.

## Versions and releases

Each plugin has its own version number (`d_version` in `src/PluginDSP.cpp`). To release a plugin:

    git tag swarm-synth-v1.4.0
    git push origin swarm-synth-v1.4.0

or publish a release with such a tag on github.com (Releases -> Draft a new release).
GitHub Actions builds that plugin and attaches a zip to the release. If the version in the tag does not match the version
in the code, the build stops (so a file with a forgotten version number never gets released).

## License

The plugin code (`plugins/`, `shared/guitar-engine/`, build files) is MIT licensed - see [LICENSE](LICENSE).

The `shared/dpf/` and `shared/dpf-widgets/` folders are third-party code under their own licenses, whose files are in those folders:

- [DPF](https://github.com/DISTRHO/DPF) and [DPF-Widgets](https://github.com/DISTRHO/DPF-Widgets) - Filipe Coelho (falkTX) and contributors, ISC license
- [Dear ImGui](https://github.com/ocornut/imgui) - Omar Cornut, MIT license
- DejaVu Sans font - license in `shared/dpf/dgl/src/resources/`

The project skeleton comes from the [imgui-template-plugin](https://github.com/DISTRHO/imgui-template-plugin) template (ISC).
