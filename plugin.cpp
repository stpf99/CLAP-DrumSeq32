// plugin.cpp
//
// Implementacja DrumSeqPlugin — instrument CLAP z wbudowanym sequencerem
// 32 kroków x 4 instrumenty (drumseq::Pattern) i odtwarzaniem sampli WAV.
// Sequencer synchronizuje się z transportem hosta (clap_event_transport /
// clap_process.transport), jeden krok = jedna szesnastka.

#include "plugin.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "gui_gtk.h"

namespace drumseq {

namespace {

// ---------------------------------------------------------------------
// clap_plugin_t static thunks: CLAP is a C ABI, so every vtable entry is a
// free function that recovers `this` from plugin->plugin_data.
// ---------------------------------------------------------------------

DrumSeqPlugin *self(const clap_plugin_t *p) {
   return static_cast<DrumSeqPlugin *>(p->plugin_data);
}

bool clap_init(const clap_plugin_t *p) { return self(p)->init(); }
void clap_destroy(const clap_plugin_t *p) { self(p)->destroy(); }
bool clap_activate(const clap_plugin_t *p, double sr, uint32_t minF, uint32_t maxF) {
   return self(p)->activate(sr, minF, maxF);
}
void clap_deactivate(const clap_plugin_t *p) { self(p)->deactivate(); }
bool clap_start_processing(const clap_plugin_t *p) { return self(p)->startProcessing(); }
void clap_stop_processing(const clap_plugin_t *p) { self(p)->stopProcessing(); }
void clap_on_main_thread(const clap_plugin_t *p) { self(p)->onMainThread(); }
clap_process_status clap_process_fn(const clap_plugin_t *p, const clap_process_t *proc) {
   return self(p)->process(proc);
}
const void *clap_get_extension(const clap_plugin_t *p, const char *id) {
   return self(p)->getExtension(id);
}

// ---- note-ports extension ----
uint32_t np_count(const clap_plugin_t *p, bool is_input) {
   return self(p)->notePortCount(is_input);
}
bool np_get(const clap_plugin_t *p, uint32_t idx, bool is_input, clap_note_port_info_t *info) {
   return self(p)->notePortInfo(idx, is_input, info);
}
const clap_plugin_note_ports_t kNotePortsExt = {np_count, np_get};

// ---- audio-ports extension ----
uint32_t ap_count(const clap_plugin_t *p, bool is_input) {
   return self(p)->audioPortCount(is_input);
}
bool ap_get(const clap_plugin_t *p, uint32_t idx, bool is_input, clap_audio_port_info_t *info) {
   return self(p)->audioPortInfo(idx, is_input, info);
}
const clap_plugin_audio_ports_t kAudioPortsExt = {ap_count, ap_get};

// ---- params extension ----
uint32_t pr_count(const clap_plugin_t *p) { return self(p)->paramCount(); }
bool pr_get_info(const clap_plugin_t *p, uint32_t idx, clap_param_info_t *info) {
   return self(p)->paramInfo(idx, info);
}
bool pr_get_value(const clap_plugin_t *p, clap_id id, double *out) {
   return self(p)->paramGetValue(id, out);
}
bool pr_value_to_text(const clap_plugin_t *p, clap_id id, double value, char *buf, uint32_t cap) {
   return self(p)->paramValueToText(id, value, buf, cap);
}
bool pr_text_to_value(const clap_plugin_t *p, clap_id id, const char *text, double *out) {
   return self(p)->paramTextToValue(id, text, out);
}
void pr_flush(const clap_plugin_t *p, const clap_input_events_t *in, const clap_output_events_t *out) {
   self(p)->paramFlush(in, out);
}
const clap_plugin_params_t kParamsExt = {pr_count,       pr_get_info,   pr_get_value,
                                          pr_value_to_text, pr_text_to_value, pr_flush};

// ---- state extension ----
bool st_save(const clap_plugin_t *p, const clap_ostream_t *stream) { return self(p)->stateSave(stream); }
bool st_load(const clap_plugin_t *p, const clap_istream_t *stream) { return self(p)->stateLoad(stream); }
const clap_plugin_state_t kStateExt = {st_save, st_load};

// ---- gui extension (floating-only, see gui.h decision rationale) ----
bool gui_is_api_supported(const clap_plugin_t *p, const char *api, bool is_floating) {
   return self(p)->guiIsApiSupported(api, is_floating);
}
bool gui_get_preferred_api(const clap_plugin_t *, const char **api, bool *is_floating) {
   *api = CLAP_WINDOW_API_X11;
   *is_floating = true;
   return true;
}
bool gui_create(const clap_plugin_t *p, const char *api, bool is_floating) {
   return self(p)->guiCreate(api, is_floating);
}
void gui_destroy(const clap_plugin_t *p) { self(p)->guiDestroy(); }
bool gui_set_scale(const clap_plugin_t *, double) { return false; }
bool gui_get_size(const clap_plugin_t *, uint32_t *w, uint32_t *h) {
   *w = 360;
   *h = 220;
   return true;
}
bool gui_can_resize(const clap_plugin_t *) { return false; }
bool gui_get_resize_hints(const clap_plugin_t *, clap_gui_resize_hints_t *) { return false; }
bool gui_adjust_size(const clap_plugin_t *, uint32_t *, uint32_t *) { return false; }
bool gui_set_size(const clap_plugin_t *, uint32_t, uint32_t) { return false; }
bool gui_set_parent(const clap_plugin_t *, const clap_window_t *) { return false; } // floating-only
bool gui_set_transient(const clap_plugin_t *p, const clap_window_t *w) {
   return self(p)->guiSetTransient(w);
}
void gui_suggest_title(const clap_plugin_t *, const char *) {}
bool gui_show(const clap_plugin_t *p) { return self(p)->guiShow(); }
bool gui_hide(const clap_plugin_t *p) { return self(p)->guiHide(); }
const clap_plugin_gui_t kGuiExt = {
    gui_is_api_supported, gui_get_preferred_api, gui_create,      gui_destroy,
    gui_set_scale,        gui_get_size,          gui_can_resize,  gui_get_resize_hints,
    gui_adjust_size,      gui_set_size,          gui_set_parent,  gui_set_transient,
    gui_suggest_title,    gui_show,              gui_hide,
};

const char *kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_DRUM_MACHINE,
                            CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
    {CLAP_VERSION_MAJOR, CLAP_VERSION_MINOR, CLAP_VERSION_REVISION},
    "com.tomasz.drumseq32",
    "DrumSeq32",
    "Tomasz",
    "",
    "",
    "",
    "0.1.0",
    "32-step, 4-instrument drum sequencer. Sample playback only \342\200\224 no built-in FX.",
    kFeatures,
};

} // namespace

// ===========================================================================
// DrumSeqPlugin
// ===========================================================================

DrumSeqPlugin::DrumSeqPlugin(const clap_host_t *host) : host_(host) {
   plugin_.desc = &kDescriptor;
   plugin_.plugin_data = this;
   plugin_.init = clap_init;
   plugin_.destroy = clap_destroy;
   plugin_.activate = clap_activate;
   plugin_.deactivate = clap_deactivate;
   plugin_.start_processing = clap_start_processing;
   plugin_.stop_processing = clap_stop_processing;
   plugin_.reset = [](const clap_plugin_t *) {}; // no persistent audio state to reset beyond voices
   plugin_.process = clap_process_fn;
   plugin_.get_extension = clap_get_extension;
   plugin_.on_main_thread = clap_on_main_thread;
}

DrumSeqPlugin::~DrumSeqPlugin() = default;

const clap_plugin_descriptor_t *DrumSeqPlugin::descriptor() { return &kDescriptor; }

bool DrumSeqPlugin::init() {
   reloadSamplesFromFolder();
   return true;
}

void DrumSeqPlugin::destroy() {
   guiDestroy();
   delete this;
}

bool DrumSeqPlugin::activate(double sample_rate, uint32_t, uint32_t) {
   sampleRate_ = sample_rate;
   active_ = true;
   voiceEngine_.reset();
   lastStepIndex_ = -1;
   haveTransport_ = false;
   return true;
}

void DrumSeqPlugin::deactivate() { active_ = false; }

bool DrumSeqPlugin::startProcessing() { return true; }
void DrumSeqPlugin::stopProcessing() {}
void DrumSeqPlugin::onMainThread() {}

const void *DrumSeqPlugin::getExtension(const char *id) {
   if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
      return &kNotePortsExt;
   if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
      return &kAudioPortsExt;
   if (!std::strcmp(id, CLAP_EXT_PARAMS))
      return &kParamsExt;
   if (!std::strcmp(id, CLAP_EXT_STATE))
      return &kStateExt;
   if (!std::strcmp(id, CLAP_EXT_GUI))
      return &kGuiExt;
   return nullptr;
}

// ---------------------------------------------------------------------
// note-ports: 1x in (live pads / piano roll) + 1x out (sequencer emits
// NOTE_ON/OFF so host can record the pattern as MIDI if the routing allows).
// ---------------------------------------------------------------------

uint32_t DrumSeqPlugin::notePortCount(bool is_input) const { return 1; }

bool DrumSeqPlugin::notePortInfo(uint32_t index, bool is_input, clap_note_port_info_t *info) const {
   if (index != 0)
      return false;
   info->id = is_input ? 0 : 1;
   info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
   info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
   std::snprintf(info->name, CLAP_NAME_SIZE, is_input ? "Note In" : "Sequencer Out");
   return true;
}

// ---------------------------------------------------------------------
// audio-ports: single stereo output, no audio input (pure sample player).
// ---------------------------------------------------------------------

uint32_t DrumSeqPlugin::audioPortCount(bool is_input) const { return is_input ? 0 : 1; }

bool DrumSeqPlugin::audioPortInfo(uint32_t index, bool is_input, clap_audio_port_info_t *info) const {
   if (is_input || index != 0)
      return false;
   info->id = 0;
   std::snprintf(info->name, CLAP_NAME_SIZE, "Output");
   info->flags = CLAP_AUDIO_PORT_IS_MAIN;
   info->channel_count = 2;
   info->port_type = CLAP_PORT_STEREO;
   info->in_place_pair = CLAP_INVALID_ID;
   return true;
}

// ---------------------------------------------------------------------
// params
// ---------------------------------------------------------------------

bool DrumSeqPlugin::paramInfo(uint32_t index, clap_param_info_t *info) const {
   std::memset(info, 0, sizeof(*info));
   info->cookie = nullptr;

   if (index < kParamStepCount) {
      const int instrument = static_cast<int>(index / kMaxSteps);
      const int step = static_cast<int>(index % kMaxSteps);
      info->id = stepParamId(instrument, step);
      info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
      std::snprintf(info->name, CLAP_NAME_SIZE, "%s Step %02d",
                    kInstrumentNames[instrument], step + 1);
      std::snprintf(info->module, CLAP_PATH_SIZE, "Pattern/%s", kInstrumentNames[instrument]);
      info->min_value = 0.0;
      info->max_value = 1.0;
      info->default_value = 0.0;
      return true;
   }

   index -= kParamStepCount;
   if (index < kNumControlParams) {
      switch (index) {
      case 0:
         info->id = kParamLengthSteps;
         info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
         std::snprintf(info->name, CLAP_NAME_SIZE, "Length (steps)");
         std::snprintf(info->module, CLAP_PATH_SIZE, "Sequencer");
         info->min_value = 1;
         info->max_value = kMaxSteps;
         info->default_value = 16;
         return true;
      case 1:
         info->id = kParamRandomizeMode;
         info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM | CLAP_PARAM_IS_AUTOMATABLE;
         std::snprintf(info->name, CLAP_NAME_SIZE, "Randomize Mode");
         std::snprintf(info->module, CLAP_PATH_SIZE, "Sequencer/Randomize");
         info->min_value = 0;
         info->max_value = kNumRandomizeModes - 1;
         info->default_value = 0;
         return true;
      case 2:
         info->id = kParamRandomizeDensity;
         info->flags = CLAP_PARAM_IS_AUTOMATABLE;
         std::snprintf(info->name, CLAP_NAME_SIZE, "Randomize Density");
         std::snprintf(info->module, CLAP_PATH_SIZE, "Sequencer/Randomize");
         info->min_value = 0.0;
         info->max_value = 1.0;
         info->default_value = 0.5;
         return true;
      case 3:
         info->id = kParamSwapProbability;
         info->flags = CLAP_PARAM_IS_AUTOMATABLE;
         std::snprintf(info->name, CLAP_NAME_SIZE, "Instrument Swap Probability");
         std::snprintf(info->module, CLAP_PATH_SIZE, "Sequencer/Randomize");
         info->min_value = 0.0;
         info->max_value = 1.0;
         info->default_value = 0.0;
         return true;
      case 4:
         info->id = kParamRandomizeTrigger;
         info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
         std::snprintf(info->name, CLAP_NAME_SIZE, "Randomize (trigger)");
         std::snprintf(info->module, CLAP_PATH_SIZE, "Sequencer/Randomize");
         info->min_value = 0;
         info->max_value = 1;
         info->default_value = 0;
         return true;
      case 5:
         info->id = kParamClearTrigger;
         info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
         std::snprintf(info->name, CLAP_NAME_SIZE, "Clear Pattern (trigger)");
         std::snprintf(info->module, CLAP_PATH_SIZE, "Sequencer");
         info->min_value = 0;
         info->max_value = 1;
         info->default_value = 0;
         return true;
      case 6:
         info->id = kParamReloadSamples;
         info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
         std::snprintf(info->name, CLAP_NAME_SIZE, "Reload Samples (folder)");
         std::snprintf(info->module, CLAP_PATH_SIZE, "Samples");
         info->min_value = 0;
         info->max_value = 1;
         info->default_value = 0;
         return true;
      }
   }

   index -= kNumControlParams;
   if (index < kNumMidiNoteParams) {
      const int instrument = static_cast<int>(index);
      info->id = kParamMidiNoteBase + instrument;
      info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
      std::snprintf(info->name, CLAP_NAME_SIZE, "%s MIDI Note", kInstrumentNames[instrument]);
      std::snprintf(info->module, CLAP_PATH_SIZE, "MIDI Mapping");
      info->min_value = 0;
      info->max_value = 127;
      info->default_value = kDefaultMidiNotes[instrument];
      return true;
   }

   return false;
}

bool DrumSeqPlugin::paramGetValue(clap_id id, double *out) const {
   int instrument = 0, step = 0;
   if (isStepParam(id, &instrument, &step)) {
      *out = pattern_.steps[instrument][step] ? 1.0 : 0.0;
      return true;
   }
   switch (id) {
   case kParamLengthSteps:
      *out = lengthSteps_;
      return true;
   case kParamRandomizeMode:
      *out = static_cast<double>(randomizeSettings_.mode);
      return true;
   case kParamRandomizeDensity:
      *out = randomizeSettings_.density;
      return true;
   case kParamSwapProbability:
      *out = randomizeSettings_.swap_probability;
      return true;
   case kParamRandomizeTrigger:
      *out = randomizeTriggerPrev_;
      return true;
   case kParamClearTrigger:
      *out = clearTriggerPrev_;
      return true;
   }
   if (id >= kParamMidiNoteBase && id < kParamMidiNoteBase + kNumMidiNoteParams) {
      *out = midiNotes_[id - kParamMidiNoteBase];
      return true;
   }
   return false;
}

bool DrumSeqPlugin::paramValueToText(clap_id id, double value, char *buf, uint32_t cap) const {
   int instrument = 0, step = 0;
   if (isStepParam(id, &instrument, &step)) {
      std::snprintf(buf, cap, value >= 0.5 ? "On" : "Off");
      return true;
   }
   if (id == kParamRandomizeMode) {
      const int mode = std::clamp(static_cast<int>(value), 0, kNumRandomizeModes - 1);
      std::snprintf(buf, cap, "%s", kRandomizeModeNames[mode]);
      return true;
   }
   if (id == kParamRandomizeTrigger || id == kParamClearTrigger) {
      std::snprintf(buf, cap, value >= 0.5 ? "Triggered" : "Idle");
      return true;
   }
   std::snprintf(buf, cap, "%.3f", value);
   return true;
}

bool DrumSeqPlugin::paramTextToValue(clap_id, const char *text, double *out) const {
   // Accept plain numeric text; enough for automation lanes / typed entry.
   char *end = nullptr;
   const double v = std::strtod(text, &end);
   if (end == text)
      return false;
   *out = v;
   return true;
}

void DrumSeqPlugin::applyParamValue(clap_id id, double value, bool notifyHost) {
   int instrument = 0, step = 0;
   if (isStepParam(id, &instrument, &step)) {
      pattern_.steps[instrument][step] = value >= 0.5;
      return;
   }

   switch (id) {
   case kParamLengthSteps:
      lengthSteps_ = std::clamp(static_cast<int>(std::lround(value)), 1, kMaxSteps);
      return;
   case kParamRandomizeMode:
      randomizeSettings_.mode = static_cast<RandomizeMode>(std::clamp(
          static_cast<int>(std::lround(value)), 0, kNumRandomizeModes - 1));
      return;
   case kParamRandomizeDensity:
      randomizeSettings_.density = std::clamp(value, 0.0, 1.0);
      return;
   case kParamSwapProbability:
      randomizeSettings_.swap_probability = std::clamp(value, 0.0, 1.0);
      return;
   case kParamRandomizeTrigger: {
      const bool rising = value >= 0.5 && randomizeTriggerPrev_ < 0.5;
      randomizeTriggerPrev_ = value;
      if (rising) {
         runRandomize();
         (void)notifyHost; // host param-rescan for step values is pushed from process()/flush()
      }
      return;
   }
   case kParamClearTrigger: {
      const bool rising = value >= 0.5 && clearTriggerPrev_ < 0.5;
      clearTriggerPrev_ = value;
      if (rising)
         runClear();
      return;
   }
   case kParamReloadSamples: {
      const bool rising = value >= 0.5 && reloadSamplesTriggerPrev_ < 0.5;
      reloadSamplesTriggerPrev_ = value;
      if (rising)
         reloadSamplesFromFolder();
      return;
   }
   }

   if (id >= kParamMidiNoteBase && id < kParamMidiNoteBase + kNumMidiNoteParams) {
      midiNotes_[id - kParamMidiNoteBase] =
          static_cast<int16_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 127));
   }
}

void DrumSeqPlugin::runRandomize() {
   randomize_pattern(pattern_, randomizeSettings_, lengthSteps_, rng_);
}

void DrumSeqPlugin::runClear() { pattern_.clear(); }

void DrumSeqPlugin::reloadSamplesFromFolder() {
   // Szukamy WAV w ~/DrumSeq32/ pod standardowymi nazwami.
   // Działa bez floating GUI — wystarczy wrzucić pliki do folderu.
   const char *home = std::getenv("HOME");
   if (!home)
      return;
   const std::string dir = std::string(home) + "/DrumSeq32";
   static const char *names[kNumInstruments] = {
       "hihat.wav", "kick.wav", "snare.wav", "tom.wav",
   };
   // alternatywne nazwy (polskie / ogólne)
   static const char *alts[kNumInstruments][3] = {
       {"talerz.wav", "hh.wav", "hat.wav"},
       {"stopa.wav", "bd.wav", "bassdrum.wav"},
       {"werbel.wav", "sd.wav", "snr.wav"},
       {"tomtom.wav", "floor.wav", "tom1.wav"},
   };
   for (int inst = 0; inst < kNumInstruments; ++inst) {
      std::string path = dir + "/" + names[inst];
      SampleBuffer buf;
      if (load_wav_file(path, buf)) {
         std::lock_guard<std::mutex> lock(samplesMutex_);
         samples_[inst] = std::move(buf);
         continue;
      }
      bool loaded = false;
      for (int a = 0; a < 3; ++a) {
         path = dir + "/" + alts[inst][a];
         if (load_wav_file(path, buf)) {
            std::lock_guard<std::mutex> lock(samplesMutex_);
            samples_[inst] = std::move(buf);
            loaded = true;
            break;
         }
      }
      (void)loaded;
   }
}

void DrumSeqPlugin::pushParamValueEvent(const clap_output_events_t *out, clap_id id, double value) {
   if (!out)
      return;
   clap_event_param_value_t ev{};
   ev.header.size = sizeof(ev);
   ev.header.time = 0;
   ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
   ev.header.type = CLAP_EVENT_PARAM_VALUE;
   ev.header.flags = 0;
   ev.param_id = id;
   ev.cookie = nullptr;
   ev.note_id = -1;
   ev.port_index = -1;
   ev.channel = -1;
   ev.key = -1;
   ev.value = value;
   out->try_push(out, &ev.header);
}

void DrumSeqPlugin::handleInputEvents(const clap_input_events_t *in) {
   if (!in)
      return;
   const uint32_t n = in->size(in);
   for (uint32_t i = 0; i < n; ++i) {
      const clap_event_header_t *hdr = in->get(in, i);
      if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID)
         continue;

      if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
         const auto *ev = reinterpret_cast<const clap_event_param_value_t *>(hdr);
         applyParamValue(ev->param_id, ev->value, false);
      } else if (hdr->type == CLAP_EVENT_NOTE_ON) {
         const auto *ev = reinterpret_cast<const clap_event_note_t *>(hdr);
         for (int inst = 0; inst < kNumInstruments; ++inst) {
            if (midiNotes_[inst] == ev->key) {
               triggerInstrument(inst, static_cast<float>(std::clamp(ev->velocity, 0.0, 1.0)));
            }
         }
      }
   }
}

void DrumSeqPlugin::triggerInstrument(int instrument, float gain) {
   std::lock_guard<std::mutex> lock(samplesMutex_);
   const SampleBuffer &s = samples_[instrument];
   if (s.valid())
      voiceEngine_.trigger(s, gain);
}

// ---------------------------------------------------------------------
// The internal sequencer clock: one step == one 16th note. We derive the
// current step from the host transport's song position in beats. This
// only advances while the host reports CLAP_TRANSPORT_IS_PLAYING; a host
// with no transport info (transport == nullptr) means the sequencer stays
// silent (it has no notion of "playing" to hook onto), which matches how
// a DAW-hosted step sequencer is expected to behave.
// ---------------------------------------------------------------------

void DrumSeqPlugin::pushNoteEvent(const clap_output_events_t *out, uint32_t time,
                                   uint16_t type, int16_t key, double velocity, int32_t note_id) {
   if (!out)
      return;
   clap_event_note_t ev{};
   ev.header.size = sizeof(ev);
   ev.header.time = time;
   ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
   ev.header.type = type;
   ev.header.flags = 0;
   ev.note_id = note_id;
   ev.port_index = 0; // Sequencer Out
   ev.channel = 0;
   ev.key = key;
   ev.velocity = velocity;
   out->try_push(out, &ev.header);
}

void DrumSeqPlugin::advanceSequencer(uint32_t numFrames, const clap_process_t *process,
                                      const clap_output_events_t *outEvents) {
   const clap_event_transport_t *transport = process->transport;
   if (!transport || !(transport->flags & CLAP_TRANSPORT_IS_PLAYING) ||
       !(transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)) {
      haveTransport_ = false;
      return;
   }

   const double songPosBeatsStart =
       static_cast<double>(transport->song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR);
   const double tempo = transport->tempo > 0.0 ? transport->tempo : 120.0;
   const double beatsPerSecond = tempo / 60.0;
   const double beatsPerBuffer = beatsPerSecond * (static_cast<double>(numFrames) / sampleRate_);
   const double stepsPerBeat = 4.0; // 1 step = 1 sixteenth note

   haveTransport_ = true;

   const double startStepF = songPosBeatsStart * stepsPerBeat;
   const double endStepF = (songPosBeatsStart + beatsPerBuffer) * stepsPerBeat;

   const int startStep = static_cast<int>(std::floor(startStepF));
   const int endStep = static_cast<int>(std::floor(endStepF));

   // Krótka nuta ~1/32 nuty (pół kroku) — widać „ślad” przy nagrywaniu MIDI.
   const double framesPerStep = sampleRate_ / (beatsPerSecond * stepsPerBeat);
   const uint32_t noteLen = std::max<uint32_t>(1, static_cast<uint32_t>(framesPerStep * 0.5));

   for (int s = startStep; s <= endStep; ++s) {
      if (s == lastStepIndex_)
         continue;
      const int wrapped = ((s % lengthSteps_) + lengthSteps_) % lengthSteps_;

      // Sample offset w buforze dla tej granicy kroku
      double frac = 0.0;
      if (endStepF > startStepF)
         frac = (static_cast<double>(s) - startStepF) / (endStepF - startStepF);
      const uint32_t tOn = std::min<uint32_t>(
          numFrames > 0 ? numFrames - 1 : 0,
          static_cast<uint32_t>(frac * numFrames));
      const uint32_t tOff = std::min(numFrames > 0 ? numFrames - 1 : 0, tOn + noteLen);

      for (int inst = 0; inst < kNumInstruments; ++inst) {
         if (!pattern_.steps[inst][wrapped])
            continue;
         triggerInstrument(inst, 1.0f);
         // Wyślij MIDI na note-out (host może to nagrać / podejrzeć)
         const int32_t nid = nextNoteId_++;
         pushNoteEvent(outEvents, tOn, CLAP_EVENT_NOTE_ON, midiNotes_[inst], 1.0, nid);
         pushNoteEvent(outEvents, tOff, CLAP_EVENT_NOTE_OFF, midiNotes_[inst], 0.0, nid);
      }
      lastStepIndex_ = s;
   }
}

clap_process_status DrumSeqPlugin::process(const clap_process_t *process) {
   handleInputEvents(process->in_events);

   if (process->audio_outputs_count < 1 || process->audio_outputs[0].channel_count < 2)
      return CLAP_PROCESS_CONTINUE;

   float *outL = process->audio_outputs[0].data32[0];
   float *outR = process->audio_outputs[0].data32[1];
   std::fill(outL, outL + process->frames_count, 0.0f);
   std::fill(outR, outR + process->frames_count, 0.0f);

   advanceSequencer(process->frames_count, process, process->out_events);

   {
      std::lock_guard<std::mutex> lock(samplesMutex_);
      voiceEngine_.render(outL, outR, process->frames_count);
   }

   return CLAP_PROCESS_CONTINUE;
}

void DrumSeqPlugin::paramFlush(const clap_input_events_t *in, const clap_output_events_t *) {
   handleInputEvents(in);
}

// ---------------------------------------------------------------------
// state: length, randomize settings, midi note map, 128 pattern bits, and
// the 4 sample file paths (so the host reloads the same samples on
// project reopen — the plugin re-reads them from disk on load).
// ---------------------------------------------------------------------

namespace {
template <typename T> bool writeRaw(const clap_ostream_t *s, const T &v) {
   return s->write(s, &v, sizeof(v)) == sizeof(v);
}
template <typename T> bool readRaw(const clap_istream_t *s, T &v) {
   return s->read(s, &v, sizeof(v)) == sizeof(v);
}
bool writeString(const clap_ostream_t *s, const std::string &str) {
   const uint32_t len = static_cast<uint32_t>(str.size());
   if (!writeRaw(s, len))
      return false;
   if (len == 0)
      return true;
   return s->write(s, str.data(), len) == static_cast<int64_t>(len);
}
bool readString(const clap_istream_t *s, std::string &str) {
   uint32_t len = 0;
   if (!readRaw(s, len))
      return false;
   str.resize(len);
   if (len == 0)
      return true;
   return s->read(s, str.data(), len) == static_cast<int64_t>(len);
}
} // namespace

bool DrumSeqPlugin::stateSave(const clap_ostream_t *stream) const {
   const uint32_t kMagic = 0x53445231; // "SDR1"
   if (!writeRaw(stream, kMagic))
      return false;
   if (!writeRaw(stream, lengthSteps_))
      return false;
   if (!writeRaw(stream, randomizeSettings_.mode))
      return false;
   if (!writeRaw(stream, randomizeSettings_.density))
      return false;
   if (!writeRaw(stream, randomizeSettings_.swap_probability))
      return false;
   if (!writeRaw(stream, midiNotes_))
      return false;
   for (int inst = 0; inst < kNumInstruments; ++inst)
      for (int step = 0; step < kMaxSteps; ++step) {
         const uint8_t bit = pattern_.steps[inst][step] ? 1 : 0;
         if (!writeRaw(stream, bit))
            return false;
      }
   {
      std::lock_guard<std::mutex> lock(samplesMutex_);
      for (int inst = 0; inst < kNumInstruments; ++inst)
         if (!writeString(stream, samples_[inst].source_path))
            return false;
   }
   return true;
}

bool DrumSeqPlugin::stateLoad(const clap_istream_t *stream) {
   uint32_t magic = 0;
   if (!readRaw(stream, magic) || magic != 0x53445231)
      return false;
   if (!readRaw(stream, lengthSteps_))
      return false;
   if (!readRaw(stream, randomizeSettings_.mode))
      return false;
   if (!readRaw(stream, randomizeSettings_.density))
      return false;
   if (!readRaw(stream, randomizeSettings_.swap_probability))
      return false;
   if (!readRaw(stream, midiNotes_))
      return false;
   for (int inst = 0; inst < kNumInstruments; ++inst)
      for (int step = 0; step < kMaxSteps; ++step) {
         uint8_t bit = 0;
         if (!readRaw(stream, bit))
            return false;
         pattern_.steps[inst][step] = bit != 0;
      }
   std::array<std::string, kNumInstruments> paths;
   for (int inst = 0; inst < kNumInstruments; ++inst)
      if (!readString(stream, paths[inst]))
         return false;

   for (int inst = 0; inst < kNumInstruments; ++inst) {
      if (paths[inst].empty())
         continue;
      SampleBuffer buf;
      if (load_wav_file(paths[inst], buf)) {
         std::lock_guard<std::mutex> lock(samplesMutex_);
         samples_[inst] = std::move(buf);
      }
   }
   return true;
}

// ---------------------------------------------------------------------
// gui: floating window only (see rationale — embedding X11 is a lot of
// extra work for what is just a 4-button file loader; generic UI hosts
// render all the CLAP params, including the 128 step toggles, natively).
// ---------------------------------------------------------------------

bool DrumSeqPlugin::guiIsApiSupported(const char *api, bool is_floating) const {
   return is_floating && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
}

bool DrumSeqPlugin::guiCreate(const char *api, bool is_floating) {
   if (!guiIsApiSupported(api, is_floating))
      return false;
   if (guiWindow_)
      return true;
   guiWindow_ = createGtkSampleLoaderWindow(this);
   return guiWindow_ != nullptr;
}

void DrumSeqPlugin::guiDestroy() {
   if (guiWindow_) {
      destroyGtkSampleLoaderWindow(guiWindow_);
      guiWindow_ = nullptr;
   }
}

bool DrumSeqPlugin::guiSetTransient(const clap_window_t *) { return true; }

bool DrumSeqPlugin::guiShow() {
   if (!guiWindow_)
      return false;
   showGtkSampleLoaderWindow(guiWindow_);
   return true;
}

bool DrumSeqPlugin::guiHide() {
   if (!guiWindow_)
      return false;
   hideGtkSampleLoaderWindow(guiWindow_);
   return true;
}

void DrumSeqPlugin::loadSampleForInstrument(int instrument, const std::string &path) {
   if (instrument < 0 || instrument >= kNumInstruments)
      return;
   SampleBuffer buf;
   if (!load_wav_file(path, buf))
      return;
   std::lock_guard<std::mutex> lock(samplesMutex_);
   samples_[instrument] = std::move(buf);
}

std::string DrumSeqPlugin::samplePathForInstrument(int instrument) const {
   if (instrument < 0 || instrument >= kNumInstruments)
      return {};
   std::lock_guard<std::mutex> lock(samplesMutex_);
   return samples_[instrument].source_path;
}

} // namespace drumseq

// ===========================================================================
// CLAP entry point + factory (required for hosts to load the .clap)
// ===========================================================================

namespace {

uint32_t factory_get_plugin_count(const clap_plugin_factory_t *) { return 1; }

const clap_plugin_descriptor_t *
factory_get_plugin_descriptor(const clap_plugin_factory_t *, uint32_t index) {
   if (index != 0)
      return nullptr;
   return drumseq::DrumSeqPlugin::descriptor();
}

const clap_plugin_t *factory_create_plugin(const clap_plugin_factory_t *,
                                           const clap_host_t *host,
                                           const char *plugin_id) {
   if (!host || !plugin_id)
      return nullptr;
   if (std::strcmp(plugin_id, "com.tomasz.drumseq32") != 0)
      return nullptr;

   auto *p = new drumseq::DrumSeqPlugin(host);
   return p->clapPlugin();
}

const clap_plugin_factory_t s_factory = {
    factory_get_plugin_count,
    factory_get_plugin_descriptor,
    factory_create_plugin,
};

bool entry_init(const char * /*plugin_path*/) { return true; }
void entry_deinit(void) {}

const void *entry_get_factory(const char *factory_id) {
   if (std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
      return &s_factory;
   return nullptr;
}

} // namespace

extern "C" {

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    entry_init,
    entry_deinit,
    entry_get_factory,
};

} // extern "C"
