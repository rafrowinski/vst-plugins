# Swarm Delay

Wielogłosowy delay VST3 (6 głosów, stereo), zbudowany na szkielecie Swarm Chorus.

- każdy głos to osobna linia opóźniająca z własnym feedbackiem: Time (ms albo wartość nutowa), Feedback,
  Reverse, Pitch (półtony w pętli), Detune, Level, Pan, Crush
- **Time x** z trybami **Tape** (wysokość wygina się jak taśma) i **Jump** (przeskok i zacięcie)
- **Freeze** - zamraża i zapętla zawartość linii; działa na niej Time x, Reverse, Pitch, chorus i crusher
- chorus na powtórzeniach (Detune / Rate / Mix), crusher (Bits / Rate, w pętli albo za nią)
- filtr w pętli (Lo cut / Hi cut), Warmth, ping-pong, tempo sync
- ducking (powtórzenia cichną, gdy grasz)
- losowe glitche: reverse, skok na pół czasu, stutter
- Spread (Time / Detune / Pan), presety z eksportem i importem CSV, Random, A/B
- **Time sweep**: co cykl (ms albo wartość nutowa) czas wszystkich głosów skacze i wraca - Up (czas rośnie) albo Down (czas maleje); z "One way" czas jedzie tylko w jedną stronę
- czas każdego głosu w ms albo jako wartość nutowa (1/8. itd.), osobno dla każdego głosu
- wykres wszystkich powtórzeń (góra = lewy kanał, dół = prawy); pierwsze powtórzenie głosu można przeciągać

## Instalacja (Windows)

Skopiuj `Swarm Delay.vst3` do `C:\Program Files\Common Files\VST3\` i w REAPERZE zrób
Options > Preferences > Plug-ins > VST > Clear cache/re-scan.

Presety użytkownika zapisują się w `%APPDATA%\Swarm Delay\user-presets.csv`.

## Format CSV presetów

Jeden wiersz = jeden preset. Pierwsza kolumna to nazwa, kolejne to parametry (nagłówek zawiera
ich symbole, np. `mix`, `time_mul`, `v1_time`, `v3_pitch`). Separator `;`, kropka dziesiętna.
Import przyjmuje też `,` lub tabulator jako separator, przecinek dziesiętny (np. po zapisie z Excela),
dowolną kolejność kolumn i brakujące kolumny (dostają wartości domyślne).

Duplikaty przy imporcie: ta sama nazwa i te same ustawienia -> pominięty;
ta sama nazwa, inne ustawienia -> dodany jako `Nazwa (2)`, `Nazwa (3)`...

Freeze nie jest częścią presetu: presety, Random i A/B go nie ruszają.

## Kod

- `src/Params.hpp`   - lista parametrów, wartości efektywne głosów, presety fabryczne
- `src/Engine.hpp`   - DSP (bez zależności od frameworka)
- `src/PresetIO.hpp` - biblioteka presetów i CSV
- `src/PluginDSP.cpp`, `src/PluginUI.cpp` - wtyczka i interfejs (DPF + Dear ImGui)
- `test/engine-test.cpp` - testy silnika offline: `g++ -std=c++17 -O2 -Isrc test/engine-test.cpp -o t && ./t`


## Budowanie

Wtyczki buduje się razem z całego repozytorium - instrukcja w [głównym README](../../README.md#budowanie).
Tylko ta wtyczka (Linux -> Windows, tak powstał dołączony plik):

    cmake -S ../.. -B ../../build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build ../../build-win --target swarm-delay-vst3

## Licencja

Kod wtyczki jest na licencji MIT - patrz [LICENSE](../../LICENSE). Cudzy kod (DPF, DPF-Widgets, Dear ImGui,
font DejaVu Sans) leży w `shared/` na własnych licencjach - szczegóły w [głównym README](../../README.md#licencja).

