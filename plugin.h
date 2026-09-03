// plugin.h
//
// Główna klasa pluginu CLAP: instrument, note-in -> audio-out, z
// wbudowanym 32-krokowym sequencerem (drumseq::Pattern/randomize_pattern)
// i odtwarzaniem 4 sampli WAV wczytanych z dysku (dr_wav). Bez efektów —
// to robi DAW. Minimalne floating GUI (GTK) tylko do wyboru plików sampli;
// reszta parametrów przez generic UI hosta (clap.params).

#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <random>
#include <string>

#include <clap/clap.h>

#include "param_ids.h"
#include "sample.h"
#include "sequencer.h"
#include "voice.h"

namespace drumseq {

class GtkSampleLoaderWindow; // fwd decl, defined in gui_gtk.cpp

class DrumSeqPlugin {
public:
   explicit DrumSeqPlugin(const clap_host_t *host);
   ~DrumSeqPlugin();

   // Builds the clap_plugin_t vtable pointing back at `this`.
   const clap_plugin_t *clapPlugin() { return &plugin_; }

   static const clap_plugin_descriptor_t *descriptor();

   // ---- clap_plugin_t entry points (called via static thunks) ----
   bool init();
   void destroy();
   bool activate(double sample_rate, uint32_t min_frames, uint32_t max_frames);
   void deactivate();
   bool startProcessing();
   void stopProcessing();
   void onMainThread();
   clap_process_status process(const clap_process_t *process);
   const void *getExtension(const char *id);

   // ---- clap.note-ports ----
   uint32_t notePortCount(bool is_input) const;
   bool notePortInfo(uint32_t index, bool is_input, clap_note_port_info_t *info) const;

   // ---- clap.audio-ports ----
   uint32_t audioPortCount(bool is_input) const;
   bool audioPortInfo(uint32_t index, bool is_input, clap_audio_port_info_t *info) const;

   // ---- clap.params ----
   uint32_t paramCount() const { return kNumTotalParams; }
   bool paramInfo(uint32_t index, clap_param_info_t *info) const;
   bool paramGetValue(clap_id id, double *out) const;
   bool paramValueToText(clap_id id, double value, char *buf, uint32_t cap) const;
   bool paramTextToValue(clap_id id, const char *text, double *out) const;
   void paramFlush(const clap_input_events_t *in, const clap_output_events_t *out);

   // ---- clap.state ----
   bool stateSave(const clap_ostream_t *stream) const;
   bool stateLoad(const clap_istream_t *stream);

   // ---- clap.gui ----
   bool guiIsApiSupported(const char *api, bool is_floating) const;
   bool guiCreate(const char *api, bool is_floating);
   void guiDestroy();
   bool guiSetTransient(const clap_window_t *window);
   bool guiShow();
   bool guiHide();

   // Called from the GUI thread when the user picks a sample file.
   // Loads the WAV and swaps it into the audio-thread-visible slot
   // (behind samplesMutex_).
   void loadSampleForInstrument(int instrument, const std::string &path);
   std::string samplePathForInstrument(int instrument) const;

private:
   void applyParamValue(clap_id id, double value, bool notifyHost);
   void handleInputEvents(const clap_input_events_t *in);
   void pushParamValueEvent(const clap_output_events_t *out, clap_id id, double value);
   void triggerInstrument(int instrument, float gain);
   void advanceSequencer(uint32_t numFrames, const clap_process_t *process,
                          const clap_output_events_t *outEvents);
   void runRandomize();
   void runClear();
   void reloadSamplesFromFolder();
   void pushNoteEvent(const clap_output_events_t *out, uint32_t time,
                      uint16_t type, int16_t key, double velocity, int32_t note_id);

   clap_plugin_t plugin_{};
   const clap_host_t *host_ = nullptr;

   double sampleRate_ = 48000.0;
   bool active_ = false;

   // ---- sequencer / pattern state (audio + main thread; guarded lightly
   // since only main-thread param edits and audio-thread process() touch
   // it, never concurrently per CLAP's threading model) ----
   Pattern pattern_;
   RandomizeSettings randomizeSettings_;
   int lengthSteps_ = 16;
   std::mt19937 rng_{std::random_device{}()};

   std::array<int16_t, kNumInstruments> midiNotes_ = kDefaultMidiNotes;

   // Transport-synced step clock. We track the last known song position in
   // beats (from host transport) and derive step boundaries from it, one
   // step == a 16th note, matching the original UI's grid.
   double lastSongPosBeats_ = 0.0;
   bool haveTransport_ = false;
   int lastStepIndex_ = -1;
   int32_t nextNoteId_ = 1;

   // Momentary "button" params: we track previous raw value to detect a
   // rising edge, since the host just delivers absolute values.
   double randomizeTriggerPrev_ = 0.0;
   double clearTriggerPrev_ = 0.0;
   double reloadSamplesTriggerPrev_ = 0.0;

   // ---- samples ----
   mutable std::mutex samplesMutex_;
   std::array<SampleBuffer, kNumInstruments> samples_;

   VoiceEngine voiceEngine_;

   // ---- gui ----
   GtkSampleLoaderWindow *guiWindow_ = nullptr;
};

} // namespace drumseq
