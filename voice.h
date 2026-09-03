// voice.h
//
// Minimalny, audio-thread-safe silnik odtwarzania próbek "one-shot".
// Zero ADSR/FX (celowo — host robi to we własnym łańcuchu efektów po tym
// pluginie). Każde uruchomienie (trigger) startuje jeden voice od klatki 0
// próbki; voice kończy się, gdy dotrze do końca bufora próbki. Proste
// obcinanie (hard stop) — bez fade-out, żeby nie wprowadzać ukrytego
// przetwarzania dźwięku.

#pragma once

#include <array>
#include <cstdint>

#include "sample.h"

namespace drumseq {

inline constexpr int kMaxVoices = 32;

struct Voice {
   bool active = false;
   const SampleBuffer *sample = nullptr;
   uint64_t position_frames = 0; // odtworzona pozycja w klatkach próbki
   float gain = 1.0f;            // z velocity nuty (0..1)
};

class VoiceEngine {
public:
   // Uruchamia nowy voice grający `sample` z danym gain (0..1).
   // Jeśli brak wolnych voice'ów, kradnie najstarszy (voice stealing).
   void trigger(const SampleBuffer &sample, float gain) {
      if (!sample.valid())
         return;

      int slot = -1;
      for (int i = 0; i < kMaxVoices; ++i) {
         if (!voices_[i].active) {
            slot = i;
            break;
         }
      }
      if (slot < 0) {
         // voice stealing: nadpisz voice o najniższym indeksie (najstarszy
         // z grubsza, bo triggerujemy w kolejności FIFO po slotach)
         slot = nextSteal_;
         nextSteal_ = (nextSteal_ + 1) % kMaxVoices;
      }

      voices_[slot].active = true;
      voices_[slot].sample = &sample;
      voices_[slot].position_frames = 0;
      voices_[slot].gain = gain;
   }

   // Miksuje wszystkie aktywne voice'y do bufora stereo (interleaved,
   // outL/outR wskazują na osobne kanały planarne float*, numFrames próbek).
   // Sample o innej liczbie kanałów niż 1/2 są zredukowane/duplikowane
   // najprostszym możliwym sposobem (mono->stereo duplikacja, >2 kanały:
   // bierzemy tylko pierwsze dwa).
   void render(float *outL, float *outR, uint32_t numFrames) {
      for (auto &v : voices_) {
         if (!v.active || v.sample == nullptr)
            continue;

         const SampleBuffer &s = *v.sample;
         const uint32_t ch = s.channels;

         for (uint32_t i = 0; i < numFrames; ++i) {
            if (v.position_frames >= s.frame_count) {
               v.active = false;
               break;
            }

            const size_t base = static_cast<size_t>(v.position_frames) * ch;
            float l, r;
            if (ch == 1) {
               l = r = s.interleaved[base];
            } else {
               l = s.interleaved[base];
               r = s.interleaved[base + 1];
            }

            outL[i] += l * v.gain;
            outR[i] += r * v.gain;
            v.position_frames++;
         }
      }
   }

   void reset() {
      for (auto &v : voices_)
         v.active = false;
   }

private:
   std::array<Voice, kMaxVoices> voices_{};
   int nextSteal_ = 0;
};

} // namespace drumseq
