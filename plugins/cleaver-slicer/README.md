# Cleaver Slicer

Wtyczka VST3 (Windows 64-bit) typu pattern slicer / stutter dla gitary, wzorowana na idei Boss Slicer:
sekwencer do 32 kroków tnie dźwięk w rytmiczne wzory, z przestrojeniem, filtrem i panoramą na krok.

## Instalacja

Gotowa wtyczka jest w `release/Cleaver Slicer.vst3`. Skopiuj ją do `C:\Program Files\Common Files\VST3\` i zrób w REAPERZE re-scan wtyczek.

## Własne presety

Przycisk **Save** zapisuje pattern i ustawienia pod podaną nazwą (bez tempa i Host sync), **Delete** usuwa wybrany własny preset.
Presety są w pliku tekstowym `%APPDATA%\Cleaver Slicer\user-presets.txt` – jeden preset w wierszu, pola rozdzielone tabulatorem.
Plik można skopiować na inny komputer.

## Pliki

- `src/SlicerShared.hpp` – parametry, format patternu, patterny fabryczne
- `src/SlicerEngine.hpp` – silnik DSP (niezależny od frameworka)
- `src/UserPresets.hpp` – zapis i odczyt własnych presetów
- `src/PluginDSP.cpp` – opakowanie DPF
- `src/PluginUI.cpp` – interfejs (Dear ImGui)
- `test/dsp_test.cpp` – test silnika offline, `test/host.cpp` – test ładowania VST3, `test/preset_test.cpp` – test pliku presetów

## Budowanie

Wtyczki buduje się razem z całego repozytorium - instrukcja w [głównym README](../../README.md#budowanie).
Tylko ta wtyczka (Linux -> Windows, tak powstał dołączony plik):

    cmake -S ../.. -B ../../build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build ../../build-win --target cleaver-slicer-vst3

## Licencja

Kod wtyczki jest na licencji MIT - patrz [LICENSE](../../LICENSE). Cudzy kod (DPF, DPF-Widgets, Dear ImGui,
font DejaVu Sans) leży w `shared/` na własnych licencjach - szczegóły w [głównym README](../../README.md#licencja).

