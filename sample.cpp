// sample.cpp
//
// Implementacja load_wav_file() z sample.h przy użyciu dr_wav.h
// (header-only, third_party/dr_wav.h). Cały plik dekodowany jest do
// pamięci jako float32 interleaved — brak strumieniowania, bo próbki
// perkusyjne są krótkie (pojedyncze uderzenia, nie całe utwory).

#include "sample.h"

#define DR_WAV_IMPLEMENTATION
#include "third_party/dr_wav.h"

namespace drumseq {

bool load_wav_file(const std::string &path, SampleBuffer &out) {
   out.clear();

   drwav wav;
   if (!drwav_init_file(&wav, path.c_str(), nullptr))
      return false;

   if (wav.channels == 0 || wav.totalPCMFrameCount == 0) {
      drwav_uninit(&wav);
      return false;
   }

   const uint64_t frameCount = wav.totalPCMFrameCount;
   const uint32_t channels = wav.channels;

   out.interleaved.resize(static_cast<size_t>(frameCount) * channels);

   const uint64_t framesRead =
       drwav_read_pcm_frames_f32(&wav, frameCount, out.interleaved.data());

   drwav_uninit(&wav);

   if (framesRead == 0) {
      out.clear();
      return false;
   }

   // Jeżeli host WAV dał mniej klatek niż deklarował nagłówek, przytnij
   // bufor do tego, co faktycznie zostało wczytane, zamiast zostawiać
   // ciszę/śmieci na końcu.
   if (framesRead != frameCount)
      out.interleaved.resize(static_cast<size_t>(framesRead) * channels);

   out.channels = channels;
   out.sample_rate = wav.sampleRate;
   out.frame_count = framesRead;
   out.source_path = path;

   return out.valid();
}

} // namespace drumseq
