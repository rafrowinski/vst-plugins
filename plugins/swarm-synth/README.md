# Swarm Synth

Syntezator gitarowy VST3 na szkielecie Swarm Chorus: do 8 głosów (oscylatorów), które grają to, co gitara -
każdy z własną falą, oktawą, interwałem, odstrojeniem i położeniem w stereo. Jednogłosowy: graj pojedyncze nuty.

![Swarm Synth](screenshot.png)

## Dwa sposoby zamiany gitary w syntezator

Przełącznik **Mode**. Oba tryby dzielą te same głosy, filtr i obwiednie - różnią się tylko tym, skąd oscylatory
biorą wysokość dźwięku.

**Pitch tracking** - detektor rozpoznaje nutę, oscylatory ją grają. Czysty, stabilny dźwięk. Nuta może zabrzmieć
dopiero wtedy, gdy detektor zobaczy około dwóch jej okresów, więc opóźnienie zależy od wysokości:

| nuta | Window 0.5 | Window 1.0 (domyślnie) | Window 2.0 |
|---|---|---|---|
| E2 (82 Hz, pusta struna E) | 20 ms | 25 ms | 37 ms |
| A2 (110 Hz) | 15 ms | 20 ms | 27 ms |
| G3 (196 Hz) | 10 ms | 12 ms | 16 ms |
| E4 (330 Hz, pusta struna e) | 7 ms | 7 ms | 10 ms |
| E5 (659 Hz) | 6 ms | 6 ms | 6 ms |

Sama wtyczka nie dodaje żadnego bufora (zero latencji zgłaszanej do hosta); do tych liczb dochodzi tylko bufor
karty dźwiękowej. Pokrętło **Window** ustawia, ile okresów detektor porównuje: mniej = szybciej, ale łatwiej o
pomyłkę. Zmierzone opóźnienie ostatniej nuty pokazuje pole „note found after". Póki nowa nuta nie jest
rozpoznana, syntezator milczy - poprzednia wysokość nigdy nie brzmi pod nowym atakiem.

**Reshape (instant)** - bez czekania na detektor. Sygnał gitary jest zamieniany na obracającą się fazę (sygnał
analityczny), a oscylatory przesuwają się dokładnie o ten obrót, próbka po próbce. Dźwięk jest od pierwszej
próbki, podciągnięcia i vibrato przechodzą natychmiast. Ceną jest atak: przez pierwsze milisekundy po uderzeniu
wysokość dopiero się ustala (na niskich strunach przez mniej więcej jeden okres nuty, czyli ok. 10 ms słychać
krótkie „wjechanie" w dźwięk z góry). Po rozpoznaniu nuty przez detektor odczyt jest pilnowany: jeśli wygrywa
harmoniczna, jest dzielona z powrotem do podstawy. **Glide** w tym trybie wygładza wysokość.

Liczby w tabeli i opis ataku pochodzą z testów na syntetycznych sygnałach przypominających szarpniętą strunę
(`tests/engine_test.cpp`), nie z nagrań prawdziwej gitary.

## Co jeszcze jest w środku

- **Głosy**: fala (Sine, Triangle, Saw, Square / pulse z regulowaną szerokością, biały i różowy szum, Custom),
  Octave (-2..+2), Semitones (-12..+12; +7 = kwinta), Detune, Level, Pan. Przyciski Quick ustawiają typowe
  interwały. Zakładka ALL VOICES ustawia wszystkie głosy naraz.
- **Spread**: Detune i Pan rozkładają włączone głosy automatycznie. Sąsiedzi w stereo dostają przeciwne
  odstrojenie, żeby obie strony brzmiały pełno.
- **Custom wave** (jedna, wspólna dla głosów z falą Custom): punkty przeciągane myszą, wzór matematyczny
  albo jeden okres z pliku WAV. Fala jest przeliczana na tablice z ograniczonym pasmem, więc nie aliasuje
  na wysokich nutach.
- **Loudness**: Follow guitar (syntezator jest tak głośny, jak grasz; **Compress** wyrównuje głośność w trakcie
  wybrzmiewania) albo Synth ADSR (własna obwiednia, wyzwalana uderzeniem; Dynamics = wpływ siły uderzenia).
- **Gate** i **Cut**: Gate to bezwzględny próg wejścia. Cut ucina nutę, gdy ścichnie o zadaną liczbę dB względem
  własnego szczytu - w końcówce wybrzmiewania podstawa zanika pierwsza i wysokość przestaje być pewna.
  Poniżej jednej czwartej szczytu nuta i tak trzyma ostatnią wysokość i nie przeskakuje na harmoniczne.
- **Filter**: dolnoprzepustowy ladder 6/12/18/24 dB, Reso, Drive, własna obwiednia (Env, Attack, Decay),
  Key tracking.
- **Quantize** i **Hold** (jak na klawiszach): Quantize zaokrągla wysokość do półtonu. Hold trzyma nutę pełnym
  poziomem także po wybrzmieniu struny - do następnej nuty albo do wytłumienia strun (wykrywane jako spadek
  głośności szybszy niż naturalne wybrzmiewanie).
- **Vibrato** (głębokość i szybkość, wchodzi chwilę po starcie nuty), **Velocity** w filtrze (siła uderzenia
  otwiera filtr), **Drift** (każdy głos niezależnie i powoli pływa w wysokości - do dronów).
- **Detector...** (okno dokładnych ustawień detektora, każde z opisem): czułość i odstęp uderzeń, wymagana
  czystość odczytu, czas potwierdzania legato / niskiej nuty / skoku o oktawę, traktowanie pierwszego odczytu,
  zamrażanie wysokości w końcówce, granie poprzedniej nuty do rozpoznania nowej. Trzy profile:
  Fast solo, Balanced (domyślny), Careful / let ring. Na dwóch prawdziwych nagraniach Balanced dał sekwencję
  nut zgodną z nagraniem, Fast solo dodał 6-7 fałszywych krótkich nut w zamian za szybsze przejścia.
  Własne ustawienia detektora można zapisywać, eksportować i importować (CSV) - niżej.
- **Scatter / Converge**: każda nowa nuta zaczyna się od głosów rozrzuconych maksymalnie o Scatter centów
  (1200 = oktawa) i zjeżdżają się w nutę w czasie Converge - każdy głos we własnym tempie (od ok. połowy
  do półtora raza tego czasu). Chmura, która rozwiązuje się w jeden dźwięk, jak ściana wolno chodzących
  oscylatorów w trakcie strojenia. Wzorowane na
  [KiloDrone (LOOK MUM NO COMPUTER)](https://www.youtube.com/watch?v=c3wk9WWTfNs) - 1000 oscylatorów,
  każdy z własną krzywą strojenia, które spotykają się w jednym punkcie. Presety **Drone: Oscillator Wall**:
  scatter to unison (rozrzut 700 ct, zjazd ok. 6 s), slow chaos (oktawa, ok. 16 s), in tune (bez rozrzutu,
  sama gęsta ściana). Filtr jest w nich wyłączony: jasność zmierzona na nagraniu KiloDrone (środek ciężkości
  widma ok. 500-650 Hz) wychodzi przy 8 piłach/prostokątach bez filtra.
- **MIDI out**: rozpoznane nuty jako note on/off z velocity, do sterowania innym instrumentem (bez pitch
  bendu). Nuty i czasy są sprawdzone w teście silnika; wysyłka przez hosta nie była sprawdzana w REAPERZE.
- wskaźnik granej nuty z odchyłką w centach, poziom wejścia z zaznaczonym progiem Gate
- presety fabryczne i własne, eksport / import CSV, Random, A/B - jak w Swarm Chorus

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

## Instalacja (Windows)

Skopiuj `release/Swarm Synth.vst3` do `C:\Program Files\Common Files\VST3\` i w REAPERZE zrób
Options > Preferences > Plug-ins > VST > Re-scan. Wstaw wtyczkę na torze gitary (przed wzmacniaczem
i efektami - detektor najlepiej działa na czystym sygnale).

Presety użytkownika zapisują się w `%APPDATA%\Swarm Synth\user-presets.csv`.

## Własna fala

Zapis tekstowy (kolumna `wave` w CSV, stan `wave` w projekcie):

- `n:x,y;x,y;...` - punkty połączone gładką krzywą (x = miejsce w okresie 0..1, y = 0..1, czyli dół..góra)
- `l:x,y;x,y;...` - punkty połączone odcinkami
- `f:wzór` - np. `f:sin(2*pi*x) + 0.5*sin(4*pi*x)`; `x` biegnie 0..1 przez jeden okres
- `w:...` - jeden okres wczytany z pliku WAV (256 próbek 16-bit, base64). Cały plik jest traktowany jako
  jeden okres, więc nadają się pliki typu single-cycle (PCM 8/16/24/32 bit albo float 32 bit).

We wzorze: `+ - * / ^`, nawiasy, `pi`, `e`, funkcje `sin cos tan asin acos atan sinh cosh tanh abs sqrt exp ln log10
floor ceil round frac sign min max pow mod clamp mix step`, oraz gotowe przebiegi o okresie 1: `tri saw sqr`.
Składowa stała jest usuwana, a fala skalowana do pełnej wysokości.

## Format CSV presetów

Jeden wiersz = jeden preset. Pierwsza kolumna to nazwa, kolejne to parametry (nagłówek zawiera ich symbole,
np. `mode`, `sp_detune`, `v1_wave`, `v3_detune`), ostatnia to `wave`. Separator `;`, kropka dziesiętna.
Import przyjmuje też `,` lub tabulator, przecinek dziesiętny, dowolną kolejność kolumn i brakujące kolumny.
Duplikaty: ta sama nazwa i ustawienia -> pominięty; ta sama nazwa, inne ustawienia -> `Nazwa (2)`.

## Kod

Silnik i detektor są wspólne z Guitar to MIDI Live i leżą w [`shared/guitar-engine/`](../../shared/guitar-engine):

- `Params.hpp`   - lista parametrów, kształty fal, presety fabryczne
- `Wave.hpp`     - własna fala: punkty, parser wzorów, próbki z WAV, kompilacja do tablicy
- `Tracker.hpp`  - detektor wysokości (NSDF z oknem rosnącym razem z okresem, doprecyzowanie na pełnej
  częstotliwości próbkowania)
- `Engine.hpp`   - DSP: oba tryby, oscylatory, filtr, obwiednie, MIDI (bez zależności od frameworka)
- `DetectorPresets.hpp` - zapisane ustawienia detektora i ich CSV

W tym katalogu:

- `src/PresetIO.hpp` - biblioteka presetów, CSV, wczytywanie WAV
- `src/PluginDSP.cpp`, `src/PluginUI.cpp` - wtyczka i interfejs (DPF + Dear ImGui)
- `tests/`           - testy uruchamiane bez hosta


## Testy

    g++ -O2 -std=c++17 tests/engine_test.cpp -o engine_test && ./engine_test
    g++ -O2 -std=c++17 tests/wave_preset_test.cpp -o wave_preset_test && ./wave_preset_test
    g++ -O2 -std=c++17 tests/scatter_test.cpp -o scatter_test && ./scatter_test

Pierwszy mierzy dokładność i opóźnienie detektora, sprawdza oba tryby na szarpanych nutach (także z drugą
harmoniczną mocniejszą od podstawy), interwały, legato i podciągnięcia, 44.1 / 48 / 96 kHz, wszystkie presety
(NaN, poziom, obciążenie) i nuty MIDI. Drugi sprawdza własną falę, wczytywanie WAV i CSV.
Test zapisanych ustawień detektora jest w `shared/guitar-engine/tests/`.
`tests/jump_test.cpp` i `tests/tail_test.cpp` liczą przeskoki wysokości na trudniejszych sygnałach
(słaba podstawa, wybrzmiewająca poprzednia nuta, przydźwięk, długie wybrzmiewanie).
`tests/host.cpp` to minimalny test ładowania pliku `.vst3` (Windows / Wine).

## Budowanie

Wtyczki buduje się razem z całego repozytorium - instrukcja w [głównym README](../../README.md#budowanie).
Tylko ta wtyczka (Linux -> Windows, tak powstał dołączony plik):

    cmake -S ../.. -B ../../build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build ../../build-win --target swarm-synth-vst3

## Licencja

Kod wtyczki jest na licencji MIT - patrz [LICENSE](../../LICENSE). Cudzy kod (DPF, DPF-Widgets, Dear ImGui,
font DejaVu Sans) leży w `shared/` na własnych licencjach - szczegóły w [głównym README](../../README.md#licencja).

## Znaki towarowe

VST jest znakiem towarowym Steinberg Media Technologies GmbH.
