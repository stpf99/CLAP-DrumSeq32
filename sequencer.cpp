#include "sequencer.h"

#include <algorithm>

namespace drumseq {

namespace {

// Rzut monetą z zadanym prawdopodobieństwem p (0..1), odpowiednik
// random.random() < p w Pythonie.
bool chance(std::mt19937 &rng, double p) {
   if (p <= 0.0)
      return false;
   if (p >= 1.0)
      return true;
   std::uniform_real_distribution<double> dist(0.0, 1.0);
   return dist(rng) < p;
}

bool coin_flip(std::mt19937 &rng) {
   std::uniform_int_distribution<int> dist(0, 1);
   return dist(rng) == 1;
}

// -----------------------------------------------------------------
// Tryb Groove: 1:1 port oryginalnego randomize_pattern() z Pythona.
// Oryginał (drum-sampler-v2.py, funkcja randomize_pattern, gałąź "else"
// dla trybu prostego — advanced_sequencer_mode jest tu pominięty, bo
// zdecydowaliśmy się na model on/off bez rhythm_type):
//
//   Stopa:  random.choice([1,0]) na każdym kroku i%4==0, inaczej 0
//   Werbel: 1 na każdym kroku i%4==2 (bez losowości — zawsze backbeat)
//   Talerz: random.choice([0,1]) na każdym kroku i%2==0, inaczej 0
//   TomTom: random.choice([0,1]) na każdym kroku i%8==7, inaczej 0
// -----------------------------------------------------------------
void generate_groove(Pattern &pattern, int length, std::mt19937 &rng) {
   for (int i = 0; i < length; ++i) {
      pattern.steps[static_cast<int>(Instrument::Kick)][i] =
          (i % 4 == 0) ? coin_flip(rng) : false;

      pattern.steps[static_cast<int>(Instrument::Snare)][i] = (i % 4 == 2);

      pattern.steps[static_cast<int>(Instrument::HiHat)][i] =
          (i % 2 == 0) ? coin_flip(rng) : false;

      pattern.steps[static_cast<int>(Instrument::Tom)][i] =
          (i % 8 == 7) ? coin_flip(rng) : false;
   }
}

// -----------------------------------------------------------------
// Sparse: niska, stała gęstość na każdym instrumencie niezależnie,
// skalowana parametrem density (0..1). Przydatne do zwrotek/wyciszonych
// fragmentów — brak wzorca metrycznego, czysty rzut kością na każdym
// kroku ze stosunkowo niskim p.
// -----------------------------------------------------------------
void generate_sparse(Pattern &pattern, const RandomizeSettings &s,
                      int length, std::mt19937 &rng) {
   // Skalujemy density w dół (max ~0.25 przy density=1.0), żeby "Sparse"
   // pozostawało rzeczywiście rzadkie nawet przy maksymalnym ustawieniu
   // suwaka — użytkownik chcący gęściej ma od tego tryb Dense.
   const double p = s.density * 0.25;
   for (int inst = 0; inst < kNumInstruments; ++inst) {
      for (int i = 0; i < length; ++i)
         pattern.steps[inst][i] = chance(rng, p);
   }
}

// -----------------------------------------------------------------
// Intro: gęstość rośnie liniowo od 0 (krok 0) do density (ostatni krok).
// Sensowne do budowania napięcia w wprowadzeniu utworu.
// -----------------------------------------------------------------
void generate_intro(Pattern &pattern, const RandomizeSettings &s,
                     int length, std::mt19937 &rng) {
   if (length <= 1) {
      generate_sparse(pattern, s, length, rng);
      return;
   }
   for (int inst = 0; inst < kNumInstruments; ++inst) {
      for (int i = 0; i < length; ++i) {
         const double progress = static_cast<double>(i) / (length - 1);
         const double p = progress * s.density;
         pattern.steps[inst][i] = chance(rng, p);
      }
   }
}

// -----------------------------------------------------------------
// Build: gęstość rośnie schodkowo co 1/4 długości wzorca (4 "sekcje"),
// z dodatkowym wzmocnieniem w ostatniej 1/8 długości (typowy fill przed
// dropem). Kick i Snare trzymają się siatki (żeby build nie rozjeżdżał
// pulsu), Talerz/Tom dostają czystą gęstość probabilistyczną.
// -----------------------------------------------------------------
void generate_build(Pattern &pattern, const RandomizeSettings &s,
                     int length, std::mt19937 &rng) {
   const int quarter = std::max(1, length / 4);
   const int last_eighth_start = length - std::max(1, length / 8);

   for (int i = 0; i < length; ++i) {
      const int section = std::min(3, i / quarter);
      // Gęstość sekcji: 25%, 50%, 75%, 100% wartości density.
      const double section_density = s.density * (0.25 * (section + 1));
      const bool in_fill_zone = i >= last_eighth_start;
      const double boosted = in_fill_zone
                                  ? std::min(1.0, section_density + 0.35)
                                  : section_density;

      // Kick trzyma się siatki co 4 (jak w Groove), ale prawdopodobieństwo
      // rośnie z sekcją zamiast być stałym coin-flipem.
      pattern.steps[static_cast<int>(Instrument::Kick)][i] =
          (i % 4 == 0) ? chance(rng, boosted) : false;

      // Snare: backbeat zawsze aktywny od 2. sekcji wzwyż, wcześniej
      // probabilistycznie.
      pattern.steps[static_cast<int>(Instrument::Snare)][i] =
          (i % 4 == 2) && (section >= 1 || chance(rng, boosted));

      pattern.steps[static_cast<int>(Instrument::HiHat)][i] =
          chance(rng, boosted);

      pattern.steps[static_cast<int>(Instrument::Tom)][i] =
          in_fill_zone ? chance(rng, boosted * 0.8) : chance(rng, boosted * 0.3);
   }
}

// -----------------------------------------------------------------
// Fill: reszta wzorca rzadka (jak Sparse), ostatnia 1/4 długości gęsta
// (jak Dense) — klasyczny fill przed powtórzeniem pętli/przejściem do
// kolejnej sekcji.
// -----------------------------------------------------------------
void generate_fill(Pattern &pattern, const RandomizeSettings &s,
                    int length, std::mt19937 &rng) {
   const int fill_start = length - std::max(1, length / 4);
   for (int inst = 0; inst < kNumInstruments; ++inst) {
      for (int i = 0; i < length; ++i) {
         const bool in_fill = i >= fill_start;
         const double p = in_fill ? std::min(1.0, s.density + 0.4)
                                   : s.density * 0.15;
         pattern.steps[inst][i] = chance(rng, p);
      }
   }
}

// -----------------------------------------------------------------
// Dense: wysoka gęstość na wszystkich instrumentach, skalowana density.
// -----------------------------------------------------------------
void generate_dense(Pattern &pattern, const RandomizeSettings &s,
                     int length, std::mt19937 &rng) {
   const double p = 0.5 + s.density * 0.5; // zakres 0.5..1.0
   for (int inst = 0; inst < kNumInstruments; ++inst) {
      for (int i = 0; i < length; ++i)
         pattern.steps[inst][i] = chance(rng, p);
   }
}

} // namespace

void randomize_pattern(Pattern &pattern,
                        const RandomizeSettings &settings,
                        int length_steps,
                        std::mt19937 &rng) {
   length_steps = std::clamp(length_steps, 1, kMaxSteps);

   // Zeruj cały wzorzec (łącznie z krokami poza length_steps — tak jak
   // oryginał, który iteruje tylko po range(pattern_length) i zostawia
   // resztę bez zmian; tu jednak czyścimy całość, żeby stary, dłuższy
   // wzorzec nie "wyciekał" zza granicy length_steps przy odtwarzaniu).
   pattern.clear();

   switch (settings.mode) {
   case RandomizeMode::Groove:
      generate_groove(pattern, length_steps, rng);
      break;
   case RandomizeMode::Sparse:
      generate_sparse(pattern, settings, length_steps, rng);
      break;
   case RandomizeMode::Intro:
      generate_intro(pattern, settings, length_steps, rng);
      break;
   case RandomizeMode::Build:
      generate_build(pattern, settings, length_steps, rng);
      break;
   case RandomizeMode::Fill:
      generate_fill(pattern, settings, length_steps, rng);
      break;
   case RandomizeMode::Dense:
      generate_dense(pattern, settings, length_steps, rng);
      break;
   }

   // Odpowiednik: self.randomize_instruments(None) wywoływanego na końcu
   // randomize_pattern() w oryginale.
   swap_instruments(pattern, settings.swap_probability, length_steps, rng);
}

void swap_instruments(Pattern &pattern,
                       double swap_probability,
                       int length_steps,
                       std::mt19937 &rng) {
   if (swap_probability <= 0.0)
      return;

   length_steps = std::clamp(length_steps, 1, kMaxSteps);

   std::uniform_int_distribution<int> inst_dist(0, kNumInstruments - 1);

   for (int step = 0; step < length_steps; ++step) {
      if (!chance(rng, swap_probability))
         continue;

      // random.sample(self.instruments, 2): dwa RÓŻNE instrumenty.
      int inst1 = inst_dist(rng);
      int inst2 = inst_dist(rng);
      while (inst2 == inst1)
         inst2 = inst_dist(rng);

      std::swap(pattern.steps[inst1][step], pattern.steps[inst2][step]);
   }
}

} // namespace drumseq
