# VST plugins

Wtyczki audio do gitary (Windows 64-bit), zbudowane na [DPF](https://github.com/DISTRHO/DPF) i [Dear ImGui](https://github.com/ocornut/imgui).
Gotowe pliki są w katalogu `release/` każdej wtyczki - wystarczy je skopiować do `C:\Program Files\Common Files\VST3`
(VST2 / CLAP: patrz README wtyczki).

| Wtyczka | Co robi | Formaty |
|---|---|---|
| [Swarm Chorus](plugins/swarm-chorus) | wielogłosowy chorus (6 głosów, stereo), własna rysowana fala | VST3 |
| [Swarm Delay](plugins/swarm-delay) | wielogłosowy delay (6 głosów, stereo) | VST3 |
| [Swarm Synth](plugins/swarm-synth) | syntezator gitarowy: do 8 oscylatorów grających to, co gitara | VST3 |
| [Guitar to MIDI Live](plugins/guitar-to-midi-live) | gitara na MIDI w czasie rzeczywistym, do dowolnego instrumentu | VST3, VST2, CLAP |
| [Cleaver Slicer](plugins/cleaver-slicer) | pattern slicer / stutter, sekwencer do 32 kroków | VST3 |

## Układ

- `plugins/<nazwa>/` - kod, testy, README i gotowe pliki (`release/`) jednej wtyczki
- `shared/dpf/` - [DPF](https://github.com/DISTRHO/DPF) (commit `d849424c`), jedna kopia dla wszystkich wtyczek
- `shared/dpf-widgets/` - [DPF-Widgets](https://github.com/DISTRHO/DPF-Widgets) (commit `730da639`) z jedną zmianą:
  w `opengl/DearImGui.cpp` zakres znaków fontu obejmuje polskie litery (Latin Extended-A), myślniki, strzałki
  i kształty używane przez wtyczki
- `shared/guitar-engine/` - detektor wysokości i silnik syntezatora wspólny dla Swarm Synth i Guitar to MIDI Live,
  razem z zapisanymi ustawieniami detektora (wspólny plik `%APPDATA%\Guitar Detector\detector-presets.csv`)

## Budowanie

Wszystkie wtyczki naraz, Linux -> Windows (tak powstały pliki w `release/`):

    sudo apt install mingw-w64 cmake
    cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build-win -j8

Jedna wtyczka: `cmake --build build-win --target swarm-synth-vst3` (cele: `<nazwa>-vst3`, a dla Guitar to MIDI Live
także `-vst2` i `-clap`). Na Windows: MSYS2 (MinGW64) albo Visual Studio z CMake, ten sam `CMakeLists.txt`
bez pliku toolchain. Na Linuxie bez toolchaina powstają też wersje JACK (samodzielne programy) do testów.

Testy każdej wtyczki uruchamia się bez hosta - komendy są w README wtyczki.

## Pobieranie

Gotowe wersje do pobrania są w [Releases](https://github.com/rafrowinski/vst-plugins/releases): jeden plik zip
na wtyczkę (Windows 64-bit), z README i licencją.

## Wersje i wydania

Każda wtyczka ma własny numer wersji (`d_version` w `src/PluginDSP.cpp`). Żeby wydać wtyczkę:

    git tag swarm-synth-v1.4.0
    git push origin swarm-synth-v1.4.0

GitHub Actions zbuduje tę wtyczkę i opublikuje wydanie z zipem. Można też na stronie repo opublikować wydanie
(Releases -> Draft a new release) z tagiem w tej postaci - zip zostanie do niego dołączony.
Jeśli wersja w tagu nie zgadza się z wersją
w kodzie, wydanie się nie utworzy (żeby nie wypuścić pliku z zapomnianym numerem wersji).

## Licencja

Kod wtyczek (`plugins/`, `shared/guitar-engine/`, pliki budowania) jest na licencji MIT - patrz [LICENSE](LICENSE).

Katalogi `shared/dpf/` i `shared/dpf-widgets/` to cudzy kod na własnych licencjach, których pliki są w tych katalogach:

- [DPF](https://github.com/DISTRHO/DPF) i [DPF-Widgets](https://github.com/DISTRHO/DPF-Widgets) - Filipe Coelho (falkTX) i współautorzy, licencja ISC
- [Dear ImGui](https://github.com/ocornut/imgui) - Omar Cornut, licencja MIT
- font DejaVu Sans - licencja w `shared/dpf/dgl/src/resources/`

Szkielet projektu wywodzi się z szablonu [imgui-template-plugin](https://github.com/DISTRHO/imgui-template-plugin) (ISC).
