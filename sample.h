// sample.h
//
// Wczytywanie plików audio (WAV, przez dr_wav.h) do pamięci jako bufor
// float32 interleaved. Zastępuje pydub.AudioSegment + soundfile z oryginału
// — tu nie ma żadnej obróbki (ADSR/FX), tylko surowe dane audio gotowe do
// odtworzenia jako one-shot voice.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace drumseq {

struct SampleBuffer {
   std::vector<float> interleaved; // dane audio, interleaved per kanał
   uint32_t channels = 0;
   uint32_t sample_rate = 0;
   uint64_t frame_count = 0; // liczba klatek (nie liczba float'ów)
   std::string source_path;  // ścieżka, z której wczytano (do zapisu w state)

   bool valid() const { return channels > 0 && frame_count > 0; }

   void clear() {
      interleaved.clear();
      channels = 0;
      sample_rate = 0;
      frame_count = 0;
      source_path.clear();
   }
};

// Wczytuje plik WAV spod podanej ścieżki. Zwraca true i wypełnia `out` przy
// sukcesie; przy błędzie zwraca false i zostawia `out` w stanie clear().
// Nie rzuca wyjątków (audio-safe caller expectations); wszystkie błędy
// dr_wav są sprowadzone do zwracanej wartości bool.
bool load_wav_file(const std::string &path, SampleBuffer &out);

} // namespace drumseq
