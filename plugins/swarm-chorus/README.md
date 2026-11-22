# Swarm Chorus

Wielogłosowy chorus VST3 (6 głosów, stereo) z detune, feedbackiem, filtrem hue/wah na głos, EQ,
startem od uderzenia w struny, sterowaniem obwiednią, synchronizacją z tempem, własną rysowaną falą
i wykresem czasu opóźnienia każdego głosu (krzywe można przeciągać myszą).

Wersja 1.1: Warmth, Envelope, Tempo sync, stereo na wejściu, edycja na wykresie, własna fala, Random, A/B.

## Instalacja (Windows)

Skopiuj `Swarm Chorus.vst3` do `C:\Program Files\Common Files\VST3\` i w REAPERZE zrób
Options > Preferences > Plug-ins > VST > Re-scan.

Presety użytkownika zapisują się w `%APPDATA%\Swarm Chorus\user-presets.csv`.

## Własna fala (Shape = Custom wave)

Edytor otwiera przycisk „Edit wave...". Fala to albo punkty (przeciąganie, podwójne kliknięcie dodaje,
prawy przycisk usuwa; gładka krzywa albo odcinki), albo wzór w zmiennej `x` (0..1 na jeden cykl).
W obu przypadkach wtyczka raz przelicza falę na tablicę 256 wartości i w trakcie grania tylko z niej czyta.

Zapis tekstowy fali (kolumna `wave` w CSV, stan `wave` w projekcie):

- `n:x,y;x,y;...` - punkty połączone gładką krzywą
- `l:x,y;x,y;...` - punkty połączone odcinkami
- `f:wzór` - np. `f:sin(2*pi*x)^3`; wynik jest skalowany tak, żeby wypełnić cały zakres Min..Max

We wzorze: `+ - * / ^`, nawiasy, `pi`, `e`, funkcje `sin cos tan asin acos atan sinh cosh tanh abs sqrt exp ln log10
floor ceil round frac sign min max pow mod clamp mix step`, oraz gotowe przebiegi o okresie 1: `tri saw sqr`.

## Format CSV presetów

Jeden wiersz = jeden preset. Pierwsza kolumna to nazwa, kolejne to parametry (nagłówek zawiera
ich symbole, np. `mix`, `sp_phase`, `v1_rate`, `v3_detune`), ostatnia to `wave`. Separator `;`, kropka dziesiętna.
Import przyjmuje też `,` lub tabulator jako separator, przecinek dziesiętny (np. po zapisie z Excela),
dowolną kolejność kolumn i brakujące kolumny (dostają wartości domyślne). Pliki z wersji 1.1/1.2
(kolumny `cw1`..`cw16`) są zamieniane na 16 punktów.

Duplikaty przy imporcie: ta sama nazwa i te same ustawienia -> pominięty;
ta sama nazwa, inne ustawienia -> dodany jako `Nazwa (2)`, `Nazwa (3)`...

## Kod

- `src/Params.hpp`   - lista parametrów, kształty LFO, presety fabryczne
- `src/Wave.hpp`     - własna fala: punkty, parser wzorów, kompilacja do tablicy
- `src/Engine.hpp`   - DSP (bez zależności od frameworka)
- `src/PresetIO.hpp` - biblioteka presetów i CSV
- `src/PluginDSP.cpp`, `src/PluginUI.cpp` - wtyczka i interfejs (DPF + Dear ImGui)

- `tests/`           - testy silnika i fali uruchamiane bez hosta (patrz niżej)


## Testy

    g++ -O2 -std=c++17 tests/engine_test.cpp -o engine_test && ./engine_test
    g++ -O2 -std=c++17 tests/wave_preset_test.cpp -o wave_preset_test && ./wave_preset_test

Pierwszy przepuszcza sygnał testowy przez wszystkie presety (szuka NaN, mierzy poziom i obciążenie)
i sprawdza detune, stereo, Warmth, sync i własną falę. Drugi sprawdza parser wzorów, punkty i CSV.

## Budowanie

Wtyczki buduje się razem z całego repozytorium - instrukcja w [głównym README](../../README.md#budowanie).
Tylko ta wtyczka (Linux -> Windows, tak powstał dołączony plik):

    cmake -S ../.. -B ../../build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build ../../build-win --target swarm-chorus-vst3

## Licencja

Kod wtyczki jest na licencji MIT - patrz [LICENSE](../../LICENSE). Cudzy kod (DPF, DPF-Widgets, Dear ImGui,
font DejaVu Sans) leży w `shared/` na własnych licencjach - szczegóły w [głównym README](../../README.md#licencja).

## Znaki towarowe

VST jest znakiem towarowym Steinberg Media Technologies GmbH. Nazwy urządzeń w presetach
(np. „CE-2 style", „Rockman Chorus style") opisują jedynie charakter brzmienia. Wtyczka nie jest
powiązana z producentami tych urządzeń ani przez nich zatwierdzona.
