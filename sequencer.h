// sequencer.h
//
// Model danych 32-krokowego sequencera dla 4 instrumentów perkusyjnych
// oraz mapowanie na identyfikatory parametrów CLAP.
//
// To jest port logiki sequencera/randomize z drum-sampler-v2.py (GTK/pygame).
// Efekty audio (ADSR, echo, reverb, pitch, pan) NIE są portowane — host DAW
// realizuje je we własnym łańcuchu efektów po tym pluginie. Ten plugin robi
// wyłącznie: sekwencjonowanie kroków + odtwarzanie surowych próbek WAV
// jako CLAP note events / audio output.

#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>

namespace drumseq {

// ---------------------------------------------------------------------
// Stałe geometrii sequencera
// ---------------------------------------------------------------------

// Maksymalna długość wzorca. Oryginał (drum-sampler-v2.py) miał sztywne 16
// kroków w UI (Gtk.Grid budowany dla range(16)); tu rozszerzamy do 32,
// zgodnie z wymaganiem. length_steps (parametr) wybiera aktywną długość
// w zakresie [1, kMaxSteps].
inline constexpr int kMaxSteps = 32;

// 4 instrumenty perkusyjne, w tej samej kolejności co self.instruments
// w oryginale: ['Talerz', 'Stopa', 'Werbel', 'TomTom'].
inline constexpr int kNumInstruments = 4;

enum class Instrument : int {
   HiHat = 0,  // Talerz
   Kick = 1,   // Stopa
   Snare = 2,  // Werbel
   Tom = 3,    // TomTom
};

inline constexpr std::array<const char *, kNumInstruments> kInstrumentNames = {
    "Hi-Hat (Talerz)",
    "Kick (Stopa)",
    "Snare (Werbel)",
    "Tom (TomTom)",
};

// Domyślne mapowanie na numery nut MIDI, przeniesione 1:1 z self.midi_notes
// w oryginale (GM drum map: kick=36, snare=38, hihat closed=49→42 typowo,
// oryginał użył 49 (crash) dla Talerza — zachowujemy dokładnie te wartości,
// żeby zachowanie było zgodne z tym, co użytkownik już miał skonfigurowane
// w swoim DAW/MIDI mapping).
inline constexpr std::array<int16_t, kNumInstruments> kDefaultMidiNotes = {
    49, // Talerz
    36, // Stopa
    38, // Werbel
    45, // TomTom
};

// ---------------------------------------------------------------------
// Tryby randomizacji.
//
// Oryginalny randomize_pattern() miał JEDEN hardkodowany schemat gęstości
// per instrument (Stopa co 4. krok, Werbel offbeat, Talerz co 2. krok,
// TomTom rzadkie wypełnienia). Tu wydzielamy to jako "Groove" mode i
// dodajemy warianty żądane przez użytkownika (intro/build/fill/sparse),
// oraz zachowujemy randomize_instruments (zamiana wzorców między
// instrumentami z zadanym prawdopodobieństwem) jako osobny, składalny
// krok wykonywany PO wygenerowaniu bazowego wzorca.
// ---------------------------------------------------------------------

enum class RandomizeMode : int {
   Groove = 0, // odpowiednik oryginalnego randomize_pattern(): stały
               // schemat rytmiczny per instrument, z losowością na
               // dozwolonych pozycjach (tak jak w Pythonie).
   Sparse = 1, // niska gęstość, duże odstępy — do zwrotek/ambientowych
               // fragmentów.
   Intro = 2,  // gęstość rosnąca liniowo od kroku 0 do końca wzorca —
               // do buildupów/intro.
   Build = 3,  // gęstość rosnąca "schodkowo" co 1/4 długości wzorca,
               // z akcentem na ostatnią 1/8 (typowy buildup przed dropem).
   Fill = 4,   // wypełnienie skoncentrowane w ostatniej 1/4 wzorca
               // (fill przed powtórzeniem pętli), reszta rzadka.
   Dense = 5,  // wysoka gęstość na wszystkich instrumentach — do
               // najbardziej intensywnych fragmentów.
};

inline constexpr int kNumRandomizeModes = 6;

inline constexpr std::array<const char *, kNumRandomizeModes> kRandomizeModeNames = {
    "Groove", "Sparse", "Intro", "Build", "Fill", "Dense",
};

// ---------------------------------------------------------------------
// Stan wzorca. Uproszczone (zgodnie z decyzją użytkownika) do on/off per
// krok, bez rhythm_type — host realizuje wszelkie zróżnicowanie brzmienia
// (swing, akcenty, wielokrotne uderzenia) we własnym łańcuchu FX/MIDI.
// ---------------------------------------------------------------------

struct Pattern {
   // steps[instrument][step] == true, gdy krok jest aktywny.
   std::array<std::array<bool, kMaxSteps>, kNumInstruments> steps{};

   void clear() {
      for (auto &row : steps)
         row.fill(false);
   }
};

// ---------------------------------------------------------------------
// Parametry sterujące randomizacją, odpowiadające randomize_probability_spin
// z oryginału (0..100%) oraz nowo dodanej gęstości bazowej dla trybów
// innych niż Groove.
// ---------------------------------------------------------------------

struct RandomizeSettings {
   RandomizeMode mode = RandomizeMode::Groove;

   // Prawdopodobieństwo swap między dwoma instrumentami na danym kroku,
   // odpowiednik randomize_instruments() / probability = spin.get_value()/100.
   double swap_probability = 0.0; // 0..1

   // Gęstość bazowa (0..1) używana przez tryby Sparse/Intro/Build/Fill/Dense.
   // Groove ignoruje ten parametr (ma własny stały schemat, tak jak oryginał).
   double density = 0.5;
};

// Generuje wzorzec zgodnie z trybem. length_steps ogranicza generację do
// pierwszych length_steps kroków (reszta zostaje wyzerowana), analogicznie
// do pattern_length = int(self.length_spinbutton.get_value()) w oryginale.
void randomize_pattern(Pattern &pattern,
                        const RandomizeSettings &settings,
                        int length_steps,
                        std::mt19937 &rng);

// Odpowiednik randomize_instruments(): dla każdego aktywnego kroku, z danym
// prawdopodobieństwem zamienia miejscami stan dwóch losowo wybranych
// instrumentów. Wywoływane jako osobny krok, tak jak w oryginale
// (randomize_pattern kończy się wywołaniem self.randomize_instruments(None)).
void swap_instruments(Pattern &pattern,
                       double swap_probability,
                       int length_steps,
                       std::mt19937 &rng);

} // namespace drumseq
