// param_ids.h
//
// Stabilne (nigdy nie zmieniane po wydaniu!) identyfikatory parametrów CLAP.
// 32 kroki x 4 instrumenty = 128 parametrów on/off (generic UI hosta pokaże
// je jako listę przełączników — brzydko, ale w pełni automatyzowalne, co
// było wymaganiem: "biblioteka w dowolny sposób").

#pragma once

#include <cstdint>
#include "sequencer.h"

namespace drumseq {

enum ParamId : uint32_t {
   // 0 .. 127: step on/off. id = kParamStepBase + instrument * kMaxSteps + step
   kParamStepBase = 0,
   kParamStepCount = kMaxSteps * kNumInstruments, // 128

   // Sequencer-wide controls
   kParamLengthSteps = 1000,       // 1..32, stepped
   kParamRandomizeMode = 1001,     // 0..5 enum, stepped
   kParamRandomizeDensity = 1002,  // 0..1
   kParamSwapProbability = 1003,   // 0..1
   kParamRandomizeTrigger = 1004,  // 0/1 momentary "button" (rising edge -> randomize)
   kParamClearTrigger = 1005,      // 0/1 momentary "button" (rising edge -> clear pattern)
   kParamReloadSamples = 1006,     // 0/1 momentary: przeładuj WAV z ~/DrumSeq32/

   // Per-instrument incoming MIDI note mapping (for manual/live triggering
   // via note-in, and so host-side MIDI mapping matches what the user
   // already had configured elsewhere).
   kParamMidiNoteBase = 2000, // + instrument (0..3), 0..127, stepped
};

inline constexpr uint32_t kNumControlParams = 7;
inline constexpr uint32_t kNumMidiNoteParams = kNumInstruments;
inline constexpr uint32_t kNumTotalParams =
    kParamStepCount + kNumControlParams + kNumMidiNoteParams;

inline uint32_t stepParamId(int instrument, int step) {
   return kParamStepBase + static_cast<uint32_t>(instrument) * kMaxSteps +
          static_cast<uint32_t>(step);
}

inline bool isStepParam(uint32_t id, int *instrument, int *step) {
   if (id >= kParamStepBase && id < kParamStepBase + kParamStepCount) {
      const uint32_t rel = id - kParamStepBase;
      *instrument = static_cast<int>(rel / kMaxSteps);
      *step = static_cast<int>(rel % kMaxSteps);
      return true;
   }
   return false;
}

} // namespace drumseq
