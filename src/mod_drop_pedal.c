// Drop Pedal (not in RSMods): the guitar a few semitones lower, or higher, before the game hears it, so a song in
// another tuning is played without retuning (DropPedal = on, DropPedalSemitones: -1 is E flat on a guitar in E).
// The game is given the shifted guitar as its input: its note detection, tuner and tones all follow. The down and up
// keys change the shift by a semitone, and the shift shows for a moment when it changes.
//
// Every buffer of a guitar input passes RS Re-ARMed's hook point si_io on the device's I/O thread, before the game
// has it. In the Mac game that is after RS Re-ARMed's own hook has put the chosen channel in a mono buffer of its own
// (audio_chan.c); with nothing set there the buffer is the device's, of one or two channels, and the game takes one
// of them. In Learn & Play the capture stream runs the point, with a mono buffer. The samples are shifted in place
// (pitch/splice_shift.c), each channel of each input by a shifter of its own. The game opens every input it lists
// when it starts, and each passes here: a shifter is made on the game's thread once the mod is on and the input's
// channel has been seen, for the rate its device runs at, and an input that has been silent for half a second is
// left alone until it sounds again. With the mod off again the shifters stay and shift by nothing, which keeps what
// they remember current.
//
// Tone Host reads the guitar from the device itself, so it shifts it itself: mod_tone_host.c tells it by how much
// (rsm_drop_pedal_semitones).
// Needs the hook point si_io: the Mac edition, and Learn & Play from the RS Re-ARMed after 1.17.1. With an older one
// the mod stays off there.
#include <CoreAudio/CoreAudio.h>
#include <stdatomic.h>
#include <stdio.h>

#include "rsmods.h"
#include "splice_shift.h"

#define INPUTS 12     // guitar inputs at a time: the game opens every input it lists, up to ten
#define SILENT 1e-4f  // an input under this (80 dB down)
#define REST 24000    // for this many samples is not shifted: longer than a shifter remembers
#define CHANNELS 2    // of a device's buffer that are shifted
#define MOST_FRAMES 4096
#define RANGE 12      // semitones either way
#define KEEP_FOR 2.0  // seconds a key's change stands before RSMods.ini is believed again (it is read back in one)
#define SHOW_SECONDS 3.0

typedef struct {
  _Atomic(const uint8_t *) self;  // the SoundInput this slot belongs to
  _Atomic uint32_t dev;           // the device its buffers come from, as the game names it
  atomic_int channels;            // the most channels a buffer of it has had
  _Atomic(sp *) shift[CHANNELS];  // a channel's shifter, once there is one
  uint32_t quiet[CHANNELS];       // samples a channel has been silent for (the I/O thread's)
} input;

static input g_in[INPUTS];
static atomic_int g_wanted;  // the mod has been on: inputs are given shifters
static int g_hooked, g_on, g_semitones, g_set = 1000;  // g_set: what the shifters were last told
static double g_changed = -100;

static input *slot_of(const uint8_t *self) {
  for (int i = 0; i < INPUTS; i++) {
    const uint8_t *s = atomic_load_explicit(&g_in[i].self, memory_order_acquire);
    if (s == self) return &g_in[i];
    if (!s && atomic_compare_exchange_strong_explicit(&g_in[i].self, &s, self, memory_order_acq_rel, memory_order_acquire))
      return &g_in[i];
    if (s == self) return &g_in[i];  // another thread took it for the same input
  }
  return NULL;
}

// (SoundInput *self, uint32 device, const AudioBufferList *input): one I/O buffer of a guitar input
static int before_io(rc_ctx *c, void *user) {
  (void)user;
  if (!atomic_load_explicit(&g_wanted, memory_order_relaxed)) return 0;
  OV_ARGS(a, c);
  const uint8_t *self = OV_PTR(a, const uint8_t);
  uint32_t dev = OV_U32(a);
  AudioBufferList *list = OV_PTR(a, AudioBufferList);
  if (!self || !list || !list->mNumberBuffers) return 0;
  input *in = slot_of(self);
  if (!in) return 0;
  atomic_store_explicit(&in->dev, dev, memory_order_relaxed);
  AudioBuffer *b = &list->mBuffers[0];
  uint32_t channels = b->mNumberChannels;
  if (!b->mData || channels < 1 || channels > CHANNELS) return 0;
  if ((int)channels > atomic_load_explicit(&in->channels, memory_order_relaxed))
    atomic_store_explicit(&in->channels, (int)channels, memory_order_relaxed);
  uint32_t frames = b->mDataByteSize / (channels * (uint32_t)sizeof(float));
  if (!frames || frames > MOST_FRAMES) return 0;
  float *x = b->mData, one[MOST_FRAMES];
  for (uint32_t ch = 0; ch < channels; ch++) {
    sp *shift = atomic_load_explicit(&in->shift[ch], memory_order_acquire);
    if (!shift) continue;
    float peak = 0;
    for (uint32_t i = 0; i < frames; i++) {
      one[i] = x[(size_t)i * channels + ch];
      float v = one[i] < 0 ? -one[i] : one[i];
      if (v > peak) peak = v;
    }
    in->quiet[ch] = peak < SILENT ? in->quiet[ch] + frames : 0;
    if (in->quiet[ch] > REST) {
      in->quiet[ch] = REST + 1;
      continue;
    }
    sp_process(shift, one, one, (int)frames);
    for (uint32_t i = 0; i < frames; i++) x[(size_t)i * channels + ch] = one[i];
  }
  return 0;
}

// The rate a device runs at; 48 kHz, the game's own, for one the system doesn't know by that name
static double rate_of(uint32_t dev) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  Float64 rate = 0;
  UInt32 size = sizeof rate;
  if (!dev || AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &rate) || rate < 8000 || rate > 192000) return 48000;
  return rate;
}

// Shifters for the channels that have been seen and have none, and every shifter told the shift
static void give_shifters(int semitones) {
  for (int i = 0; i < INPUTS; i++) {
    input *in = &g_in[i];
    int channels = atomic_load_explicit(&in->channels, memory_order_relaxed);
    for (int ch = 0; ch < channels && ch < CHANNELS; ch++) {
      sp *shift = atomic_load_explicit(&in->shift[ch], memory_order_relaxed);
      if (shift) {
        if (semitones != g_set) sp_set_semitones(shift, semitones);
        continue;
      }
      double rate = rate_of(atomic_load_explicit(&in->dev, memory_order_relaxed));
      if (!(shift = sp_new(rate, NULL))) {
        rsm_log("drop pedal: no memory for a shifter");
        return;
      }
      sp_set_semitones(shift, semitones);
      atomic_store_explicit(&in->shift[ch], shift, memory_order_release);
      rsm_log("drop pedal: a guitar input at %g Hz%s", rate, ch ? ", its second channel" : "");
    }
  }
  g_set = semitones;
}

static int within(long v) { return (int)(v < -RANGE ? -RANGE : v > RANGE ? RANGE : v); }

int rsm_drop_pedal_semitones(void) { return g_hooked && g_on ? g_semitones : 0; }

static void tick(void) {
  if (!g_hooked) return;
  double now = rsm_now();
  int was = rsm_drop_pedal_semitones();
  g_on = rsm_on("DropPedal");
  if (now - g_changed >= KEEP_FOR) g_semitones = within(rsm_long("DropPedalSemitones", -1));
  int semitones = rsm_drop_pedal_semitones();
  if (g_on) atomic_store_explicit(&g_wanted, 1, memory_order_relaxed);
  if (atomic_load_explicit(&g_wanted, memory_order_relaxed)) give_shifters(semitones);
  if (semitones != was) {
    if (semitones)
      rsm_log("drop pedal: the guitar is shifted by %+d semitones before the game hears it", semitones);
    else
      rsm_log("drop pedal: off");
  }
  if (g_on && now - g_changed < SHOW_SECONDS) {
    // What a string tuned to E sounds as
    static const char *const NAMES[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    char txt[64];
    if (g_semitones)
      snprintf(txt, sizeof txt, "Drop Pedal: %+d (E plays as %s)", g_semitones, NAMES[((4 + g_semitones) % 12 + 12) % 12]);
    else
      snprintf(txt, sizeof txt, "Drop Pedal: 0");
    rsm_text(txt, 0.5f, 1.f / 54, 0.04f, RSM_ALIGN_CENTER, 0xffffffff);
  }
}

static void key(const rsm_key_event *e) {
  if (e->down || !g_hooked || !rsm_on("DropPedal")) return;
  int by = e->keycode == rsm_key("DropPedalDownKey") ? -1 : e->keycode == rsm_key("DropPedalUpKey") ? 1 : 0;
  if (!by) return;
  char v[8];
  g_semitones = within(g_semitones + by);
  g_changed = rsm_now();
  snprintf(v, sizeof v, "%d", g_semitones);
  rsm_set("Mod Settings", "DropPedalSemitones", v);
}

__attribute__((constructor)) static void init(void) { g_hooked = !rsm_hook("si_io", before_io, NULL); }

const rsm_mod rsm_mod_drop_pedal = {"drop_pedal", tick, key, NULL, NULL};
