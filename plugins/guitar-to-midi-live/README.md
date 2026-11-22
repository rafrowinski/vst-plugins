# Guitar to MIDI Live

Gitara na MIDI. Wtyczka słucha pojedynczych nut granych na gitarze i wysyła je jako MIDI: nuty z velocity,
podciągnięcia i vibrato jako pitch bend, a poziom grania jako kontroler (CC). Za nią można wstawić dowolny
syntezator, sampler albo automat perkusyjny.

Wykrywanie nut to ten sam detektor co w [Swarm Synth](../swarm-synth), z jednym dodatkiem potrzebnym w MIDI
(patrz „Wytłumienia" niżej).

![Guitar to MIDI Live](screenshot.png)

## Instalacja (Windows)

W `release/` są trzy wersje tej samej wtyczki - wystarczy jedna:

| plik | dokąd | uwagi |
|---|---|---|
| `Guitar to MIDI Live.vst3` | `C:\Program Files\Common Files\VST3\` | zacznij od tej |
| `Guitar to MIDI Live.dll` (VST2) | `C:\Program Files\VstPlugins\` | gdyby VST3 nie podawał MIDI dalej |
| `Guitar to MIDI Live.clap` | `C:\Program Files\Common Files\CLAP\` | REAPER obsługuje CLAP |

W REAPERZE: Options > Preferences > Plug-ins > VST (i CLAP) > Re-scan.

## Jak podłączyć w REAPERZE

Najprościej: na torze z gitarą wstaw **Guitar to MIDI Live**, a **za nim** instrument (VSTi). MIDI z wtyczki trafia do
następnej wtyczki na tym samym torze. Większość instrumentów zastępuje dźwięk, który dostają, swoim własnym -
jeśli chcesz słyszeć też gitarę, zrób osobny tor na instrument i wyślij do niego MIDI z toru gitary
(Routing > Send, „MIDI: All", audio wyłączone).

Pitch bend: ustaw w syntezatorze ten sam zakres bendu co w **Bend range** (domyślnie +/- 2 półtony).
Przy szerokich podciągnięciach i slajdach ustaw w obu +/- 12.

## Co wysyła

- **Nuty**: note-on przy każdym uderzeniu (także tej samej nuty), note-off przy wytłumieniu, ścichnięciu
  poniżej **Cut** albo zamknięciu bramki **Gate**. **Hold** trzyma nutę jak przytrzymany klawisz.
- **Velocity** z siły uderzenia: **Full at** to szczyt wejścia dla najwyższego velocity, **Range** - o ile dB
  słabsze jest najsłabsze uderzenie, **Curve**, **Min**, **Max** (Min = Max daje stałe velocity).
  W sprawdzonych nagraniach uderzenia miały szczyty od -30 do -12 dBFS, stąd domyślnie -12 dBFS i 24 dB.
- **Podciągnięcia i vibrato** - trzy tryby:
  - *Pitch bend* - płynnie, jako pitch bend (co ok. 2 ms, gdy wysokość zmieni się o ponad pół centa),
  - *Note steps* - zaokrąglone do półtonów, każdy krok to nowa nuta (jak na klawiszach),
  - *Ignore* - jedna nuta na uderzenie, wysokość się nie zmienia.
- **Legato** (hammer-on, pull-off, slajd bez uderzenia): *Retrigger* - note-off, potem note-on;
  *Overlap (glide)* - najpierw note-on, potem note-off, więc monofoniczny syntezator w trybie legato płynnie
  przechodzi między nutami.
- **Expression**: poziom grania na bieżąco jako CC 1, 2, 7, 11 albo 74 (skala 40 dB, **Gain** podnosi poziom).
- **Channel**, **Octave**, **Semitones** - kanał MIDI i transpozycja.
- MIDI przychodzące na tor przechodzi dalej bez zmian; **Guitar through** decyduje, czy dźwięk gitary idzie dalej.

Okno **Detector...** ma te same ustawienia detektora co Swarm Synth, każde z opisem. Jedna różnica w
ustawieniach domyślnych: **Legato time** to 20 ms (w Swarm Synth 12,5 ms) - wyjaśnienie niżej.

## Zapisane ustawienia detektora

W oknie **Detector...**, wiersz **My settings**:

- **Save...** zapisuje bieżące ustawienia detektora pod nazwą (Gate, Cut, Window i wszystko z okna Detector).
  Ta sama nazwa nadpisuje; nazw wbudowanych profili (Fast solo, Balanced, Careful) nie da się użyć.
- lista obok przywraca zapisane ustawienie; pokazuje też, czy bieżące wartości odpowiadają któremuś z nich
  albo wbudowanemu profilowi
- **Delete** usuwa zaznaczone ustawienie
- **Export...** zapisuje do CSV bieżące ustawienia albo wszystkie zapisane
- **Import...** dodaje ustawienia z pliku CSV. Identyczne są pomijane, ta sama nazwa z innymi wartościami dostaje
  „(2)”. Plik presetów Swarm Synth też zadziała - brane są tylko jego kolumny detektora.

Zapisane ustawienia są **wspólne dla Swarm Synth i Guitar to MIDI Live** - jeden plik
`%APPDATA%\Guitar Detector\detector-presets.csv`. Ustawienie zapisane w jednej wtyczce od razu jest w drugiej.

Format CSV: pierwszy wiersz to nagłówek, potem jeden wiersz na ustawienie:

    name;gate;cut;track;det_clarity;det_legato;det_low;det_harm;det_probation;det_repick;det_refract;det_tail;det_bridge
    Tele bridge;-52;-30;1;80;20;30;40;60;1.5;40;25;1

`track` to Window. Import przyjmuje też przecinek albo tabulator jako separator, przecinek dziesiętny
(plik z Excela), dowolną kolejność kolumn i brakujące kolumny (zostają bieżące wartości); wartości spoza zakresu
są przycinane.

## Wytłumienia (różnica względem Swarm Synth)

W syntezatorze nuta po wytłumieniu struny po prostu cichnie razem z gitarą. W MIDI to za mało, więc tu:

- **nagły spadek głośności** (o ponad 10 dB w ciągu 30 ms) wysyła note-off od razu,
- uderzenie po takim wytłumieniu jest wykrywane jako skok o ponad 12 dB ponad ciszę po wytłumieniu,
- **zmiana nuty bez uderzenia** nie jest przyjmowana, póki głośność wyraźnie spada. Inaczej nuta przyciśnięta
  na progu tuż przed uderzeniem (przez chwilę brzmi jak hammer-on) szłaby dwa razy: raz jako legato, raz przy
  uderzeniu. Z tego samego powodu **Legato time** jest tu dłuższy - w nagraniu testowym taki „duch" trwał 18 ms.

## Wyniki na nagraniach testowych

Dwa czyste nagrania gitary (pojedyncze nuty; drugie ze strunami brzmiącymi jedna na drugiej):

- sekwencja nut zgodna z nagraniem; jedna dodatkowa nuta w ostatniej sekundzie pierwszego nagrania,
  w tym samym miejscu, w którym komercyjny syntezator gitarowy też się myli,
- każde ponowne uderzenie tej samej nuty daje nową nutę (sprawdzone na poziomie sygnału: skok o 17-28 dB),
- rozpoznanie nuty po uderzeniu: mediana ok. 7 ms, najdłużej 12,5 ms,
- wynik nie zależy od tego, jak sygnał trafia w bufory hosta (to samo przy przesunięciu o 0-1000 próbek
  i przy buforach 1-2048 próbek),
- obciążenie: ok. 1,7% jednego rdzenia.

`examples/` zawiera pliki .mid z obu nagrań - można je wrzucić do REAPERA i zobaczyć w piano rollu, co
wtyczka by wysłała.

Wtyczki nie sprawdzałem w samym REAPERZE. Sprawdzone jest, że wszystkie trzy wersje ładują się jak w hoście
i zgłaszają wejście i wyjście MIDI (`tests/load_check.cpp`), a pod JACK na Linuksie MIDI faktycznie wychodzi
z wtyczki, a dźwięk przechodzi bez zmian (bit w bit).

## Ograniczenia

- Tylko pojedyncze nuty. Akordy dadzą przypadkowe nuty.
- Nuta wychodzi z opóźnieniem detektora (zwykle 5-25 ms, więcej na niskich strunach).
- Uderzenie o ponad ok. 16 dB słabsze od poprzedniego, gdy poprzednia nuta jeszcze brzmi, nie jest
  rozpoznawane jako nowe uderzenie.
- Duży skok w dół (ponad kwintę) jest potwierdzany ok. 30 ms - w tym czasie gra poprzednia nuta.

## Zamiana nagrania na MIDI (bez DAW)

    g++ -O2 -std=c++17 tests/to_midi.cpp -o to_midi
    ./to_midi nagranie.wav wynik.mid
    ./to_midi solo.wav solo.mid bends 1 det_legato 8        # dowolne parametry: symbol wartość

Symbole parametrów są w `src/MidiParams.hpp` (np. `bends`, `bend_range`, `vel_full`, `det_legato`).

## Kod

- `src/MidiCore.hpp`   - zamiana tego, co zgłasza detektor, na komunikaty MIDI (bez zależności od frameworka)
- `src/MidiParams.hpp` - parametry i profile detektora
- [`shared/guitar-engine/`](../../shared/guitar-engine) - detektor ze Swarm Synth (`Engine.hpp`, `Tracker.hpp`,
  `Params.hpp`, `Wave.hpp`) i zapisane ustawienia detektora (`DetectorPresets.hpp`), **wspólne z Swarm Synth**.
  Guitar to MIDI Live używa silnika z wyłączonymi oscylatorami i z `setEndOnMute(true)`.
- `src/PluginDSP.cpp`, `src/PluginUI.cpp` - wtyczka i interfejs (DPF + Dear ImGui)
- `tests/midi_test.cpp` - nuty, ponowne uderzenia, velocity, pitch bend, kroki, kolejność legato, CC, bufory
- `tests/to_midi.cpp`   - nagranie WAV -> plik .mid
- `tests/load_check.cpp` - ładowanie VST3 / VST2 / CLAP jak w hoście (Windows / Wine)


## Testy

    g++ -O2 -std=c++17 tests/midi_test.cpp -o midi_test && ./midi_test

## Budowanie

Wtyczki buduje się razem z całego repozytorium - instrukcja w [głównym README](../../README.md#budowanie).
Tylko ta wtyczka (Linux -> Windows, tak powstał dołączony plik):

    cmake -S ../.. -B ../../build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build ../../build-win --target guitar-to-midi-live-vst3 guitar-to-midi-live-vst2 guitar-to-midi-live-clap

## Licencja

Kod wtyczki jest na licencji MIT - patrz [LICENSE](../../LICENSE). Cudzy kod (DPF, DPF-Widgets, Dear ImGui,
font DejaVu Sans) leży w `shared/` na własnych licencjach - szczegóły w [głównym README](../../README.md#licencja).

## Znaki towarowe

VST jest znakiem towarowym Steinberg Media Technologies GmbH.
