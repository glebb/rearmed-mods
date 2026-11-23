// Pitch Shifter (not in RSMods): the guitar a few semitones lower, or higher, before the game hears it, so a song in
// another tuning is played without retuning (PitchShifter = on, PitchShifterSemitones: -1 is E flat on a guitar in E).
// The game is given the shifted guitar as its input: its note detection, tuner and tones all follow. The down and up
// keys change the shift by a semitone, and the shift shows for a moment when it changes.
//
// A song off concert pitch (E Standard at A428, say) is met by PitchShifterA4, the A the guitar is to sound at in
// place of its own 440 Hz: Control with the down or up key moves it a hertz, and the shift is the semitones and that
// together. A key without Control first drops it again, for the nearest semitone to where the shift stood, and
// changes the shift by a semitone from the next press on.
//
// PitchShifterAuto sets both for each song, from the song's tuning and the guitar's own (PitchShifterGuitarTuning,
// semitones from standard: 0 a guitar in E, -1 one in E flat): as the song's preview plays in a song list, so the
// tuner before the song already hears the shifted guitar. Which arrangement will be played isn't known then: it goes
// by the lead's, or the bass's with a bass tone. That matters where a song's lead and rhythm are tuned apart, and
// there the game says which twice: the tuner before the song reads the picked arrangement's tuning as it opens
// (the mid point seven_tk, Play_Tuning_ReadStringOffsets, as Seven-String Guitar's tuner; the song's chart is only
// loaded after the tuner), and then the chart that is loaded is one arrangement's (rsm_song_played). The later of
// the two counts, the tuner's only in a song's own menus: the tuning list's tuner reads its targets the same way. It
// sets the shift when what the song calls for changes, not every frame, so the keys still change it for the song in
// play. What it follows is in pitch_auto.h. The idea is RocksmithAudioBridge's (github.com/Cheesewizard/
// RocksmithAudioBridge, MIT, by Cheesewizard), whose Drop Pedal sets its shift from the song's tuning and a base
// tuning of the guitar; no code of it is here.
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
// (rsm_pitch_shifter_semitones).
// Needs the hook point si_io: the Mac edition, and Learn & Play from the RS Re-ARMed after 1.17.1. With an older one
// the mod stays off there.
#include <CoreAudio/CoreAudio.h>
#include <dlfcn.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

#include "pitch_auto.h"
#include "rsmods.h"
#include "splice_shift.h"

#define INPUTS 12     // guitar inputs at a time: the game opens every input it lists, up to ten
#define SILENT 1e-4f  // an input under this (80 dB down)
#define REST 24000    // for this many samples is not shifted: longer than a shifter remembers
#define CHANNELS 2    // of a device's buffer that are shifted
#define MOST_FRAMES 4096
#define RANGE 12      // semitones either way
#define A4 440        // Hz: the A a guitar is tuned to
#define A4_LOW 415    // PitchShifterA4's range: a semitone either way
#define A4_HIGH 466
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
static atomic_int g_full;    // an input came that there was no place for (the I/O thread's word to tick)
static int g_hooked, g_on, g_semitones, g_a4 = A4;
static double g_set = 1000;  // what the shifters were last told
static double g_changed = -100;
static int g_held = -1, g_fine;  // the key that is down, and whether Control has been down with it
static char g_auto_for[128];     // the song the shift was last set for by its tuning,
static int g_auto_semitones, g_auto_a4;  // and what it was set to
// The tuning a tuner last read as its target (the game thread's): for which song, and after which load of a song
static char g_tuner_for[128], *g_strings;  // "string0" to "string5" in the game's memory, 8 bytes each
static int g_tuner[6], g_tuner_load, g_load;
// The tuners before a song, and the dialog a tuner ends with ("in tune"), which is up between it and the song
static const char *const SONG_TUNERS[] = {"LearnASong_PreSongTuner", "LearnASong_PreSongTunerMP",
                                          "NonStopPlay_PreSongTuner", "NonStopPlay_PreSongTunerMP",
                                          "ScoreAttack_PreSongTuner", "Duet_PreSongTuner", "H2H_PreSongTuner",
                                          "SelectionListDialog", NULL};

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
  if (!in) {
    int none = 0;
    atomic_compare_exchange_strong_explicit(&g_full, &none, 1, memory_order_relaxed, memory_order_relaxed);
    return 0;
  }
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
static void give_shifters(double semitones) {
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
        rsm_log("pitch shifter: no memory for a shifter");
        return;
      }
      sp_set_semitones(shift, semitones);
      atomic_store_explicit(&in->shift[ch], shift, memory_order_release);
      rsm_log("pitch shifter: a guitar input at %g Hz%s", rate, ch ? ", its second channel" : "");
    }
  }
  g_set = semitones;
}

static int within(long v) { return (int)(v < -RANGE ? -RANGE : v > RANGE ? RANGE : v); }
static int a4_within(long v) { return (int)(v < A4_LOW ? A4_LOW : v > A4_HIGH ? A4_HIGH : v); }

// The shift in semitones: the whole ones, and what the A away from 440 Hz comes to
static double shift_of(int semitones, int a4) { return semitones + 12 * log2(a4 / (double)A4); }

double rsm_pitch_shifter_semitones(void) { return g_hooked && g_on ? shift_of(g_semitones, g_a4) : 0; }

// Keeps the shift in RSMods.ini, and holds it until the file has been read back. Both values each time: what the
// settings say of the A may be from before a change that isn't read back yet (two songs passed within a second).
static void keep(void) {
  g_changed = rsm_now();
  char v[8];
  snprintf(v, sizeof v, "%d", g_semitones);
  rsm_set("Mod Settings", "PitchShifterSemitones", v);
  snprintf(v, sizeof v, "%d", g_a4);
  rsm_set("Mod Settings", "PitchShifterA4", v);
}

// PitchShifterAuto: the shift for the song whose preview last played, once its tuning is known
static void follow_song(void) {
  if (!g_on || !rsm_on("PitchShifterAuto")) {
    g_auto_for[0] = 0;
    return;
  }
  const char *song = rsm_song_key();
  rsm_arrangement arr[16], one;
  int n = rsm_song_tunings(song, arr, 16), played = rsm_song_played(song, &one);
  int bass = rsm_tone_bass(), guitar = within(rsm_long("PitchShifterGuitarTuning", 0));
  if (played) g_load = played;
  const char *how = "";
  // out of the song's menus the tuner's is over: the next tuner reads its own, a moment after its menu is up
  if (!rsm_menu_in(SONG_TUNERS) && !rsm_menu_in(RSM_SONG_MODES)) g_tuner_for[0] = 0;
  if (*song && !strcmp(g_tuner_for, song) && g_tuner_load == g_load) {
    // the tuner's six strings; the name and the cents of the arrangement that has them, or of the song's first
    const rsm_arrangement *a = n > 0 ? pitch_auto_with(arr, n, bass, g_tuner) : NULL;
    one = a ? *a : n > 0 ? arr[0] : (rsm_arrangement){.name = "?"};
    if (!a && n > 0) snprintf(one.name, sizeof one.name, "?");
    memcpy(one.tuning, g_tuner, sizeof one.tuning);
    how = ", the tuner's";
  } else if (played)
    how = ", the arrangement loaded";
  if (*how) arr[0] = one, n = 1, bass = rsm_is_bass(&one);
  int semitones, a4;
  if (n <= 0 || !pitch_auto_shift(arr, n, bass, guitar, RANGE, &semitones, &a4)) return;
  a4 = a4_within(a4);
  if (!strcmp(g_auto_for, song) && semitones == g_auto_semitones && a4 == g_auto_a4) return;
  snprintf(g_auto_for, sizeof g_auto_for, "%s", song);
  g_auto_semitones = semitones, g_auto_a4 = a4;
  const rsm_arrangement *a = pitch_auto_arrangement(arr, n, bass);
  rsm_log("pitch shifter: %s (%s%s) is at %+d with %.0f cents and the guitar at %+d: %+d, A%d", song, a->name,
          how, pitch_auto_tuning(a, guitar), a->cents, guitar, semitones, a4);
  if (semitones == g_semitones && a4 == g_a4) return;
  g_semitones = semitones, g_a4 = a4;
  keep();
}

static void tick(void) {
  if (!g_hooked) return;
  double now = rsm_now();
  double was = rsm_pitch_shifter_semitones();
  g_on = rsm_on("PitchShifter");
  if (now - g_changed >= KEEP_FOR) {
    g_semitones = within(rsm_long("PitchShifterSemitones", -1));
    g_a4 = a4_within(rsm_long("PitchShifterA4", A4));
  }
  follow_song();
  double semitones = rsm_pitch_shifter_semitones();
  if (g_on) atomic_store_explicit(&g_wanted, 1, memory_order_relaxed);
  if (atomic_load_explicit(&g_wanted, memory_order_relaxed)) give_shifters(semitones);
  if (atomic_load_explicit(&g_full, memory_order_relaxed) == 1) {
    atomic_store_explicit(&g_full, 2, memory_order_relaxed);  // said once
    rsm_log("pitch shifter: more than %d guitar inputs have been opened: the ones after them are not shifted", INPUTS);
  }
  if (semitones != was) {
    if (semitones)
      rsm_log("pitch shifter: the guitar is shifted by %+.2f semitones before the game hears it (%+d, A%d)", semitones,
              g_semitones, g_a4);
    else
      rsm_log("pitch shifter: off");
  }
  if (g_on && now - g_changed < SHOW_SECONDS) {
    // What a string tuned to E sounds as
    static const char *const NAMES[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    char txt[64], a4[16] = "";
    if (g_a4 != A4) snprintf(a4, sizeof a4, ", A%d", g_a4);
    if (g_semitones)
      snprintf(txt, sizeof txt, "Pitch Shifter: %+d%s (E plays as %s%s)", g_semitones, a4,
               NAMES[((4 + g_semitones) % 12 + 12) % 12], a4);
    else
      snprintf(txt, sizeof txt, "Pitch Shifter: 0%s", a4);
    rsm_text(txt, 0.5f, 1.f / 54, 0.04f, RSM_ALIGN_CENTER, 0xffffffff);
  }
}

static void key(const rsm_key_event *e) {
  if (!g_hooked || !rsm_on("PitchShifter")) return;
  int by = e->keycode == rsm_key("PitchShifterDownKey") ? -1 : e->keycode == rsm_key("PitchShifterUpKey") ? 1 : 0;
  if (!by) return;
  // A press acts when the key comes up, and is Control's if Control was down at any time of it: let go of Control
  // first and the press is still not a plain one, which would drop the hertz just set
  if (e->down) {
    if (g_held != e->keycode) g_held = e->keycode, g_fine = 0;
    if (e->ctrl) g_fine = 1;
    return;
  }
  int fine = e->ctrl || (g_held == e->keycode && g_fine);
  g_held = -1;
  if (fine)
    g_a4 = a4_within(g_a4 + by);
  else if (g_a4 != A4)
    g_semitones = within(lround(shift_of(g_semitones, g_a4))), g_a4 = A4;
  else
    g_semitones = within(g_semitones + by);
  keep();
}

// The six strings of a tuning's JSON object (string0 to string5, numbers), through the game's JSON vtable
static int strings_of(void *json, int t[6]) {
  const uint64_t width = rc_guest_ptr_size == 8 ? ~(uint64_t)0 : 0xffffffffu;
  for (int s = 0; s < 6; s++) {
    if (!json) return 0;
    uint64_t node = OV_VCALL((uint64_t)(uintptr_t)json, (int)RC_SLOT(json_value_find), (uint64_t)(uintptr_t)(g_strings + 8 * s));
    if (!(node &= width)) return 0;
    uint64_t num = OV_VCALL((uint64_t)(uintptr_t)RC_HOST(node), (int)RC_SLOT(json_value_number));
    if (!(num &= width)) return 0;
    double d = RC_LD(double, (uint8_t *)RC_HOST(num) + RC_OFF(json_number_value));
    if (!(d > -25.0 && d < 25.0)) return 0;
    t[s] = (int)d;
  }
  return 1;
}

// A tuner is about to read its target tuning (holder: the address of its JSON object). The calls into the game
// change eax, ecx and edx, which the game's function still needs: they are given their values back, as
// seven_tuner.c's hook on this point does (without it Learn & Play crashed in the function, 2026-11-23).
static void tuner_reads(rc_mid_frame *f, void *user) {
  (void)user;
  static int v_holder = -2, v_offsets, v_eax, v_edx;
  if (v_holder == -2) {
    v_offsets = rc_mid_value(f->point, "offsets"), v_eax = rc_mid_value(f->point, "eax");
    v_edx = rc_mid_value(f->point, "edx"), v_holder = rc_mid_value(f->point, "holder");
  }
  if (v_holder < 0 || v_offsets < 0 || v_eax < 0 || v_edx < 0 || !g_strings || !g_on) return;
  uint64_t offsets = rc_mid_get(f, v_offsets), eax = rc_mid_get(f, v_eax), edx = rc_mid_get(f, v_edx);
  uint8_t *holder = (uint8_t *)(uintptr_t)rc_mid_get(f, v_holder);
  int t[6], have = strings_of(holder ? rc_mod_ptr(holder) : NULL, t);
  rc_mid_set(f, v_offsets, offsets);
  rc_mid_set(f, v_eax, eax);
  rc_mid_set(f, v_edx, edx);
  if (!have) return;
  memcpy(g_tuner, t, sizeof g_tuner);
  snprintf(g_tuner_for, sizeof g_tuner_for, "%s", rsm_song_key());
  g_tuner_load = g_load;
}

__attribute__((constructor)) static void init(void) {
  g_hooked = !rsm_hook("si_io", before_io, NULL);
  // the tuner's target, where this RS Re-ARMed has the point: without it the shift follows the loaded chart alone
  if (!g_hooked || !dlsym(RTLD_DEFAULT, "rc_mid_add_name") || !RC_HAS_OFF(json_number_value)) return;
  if (rc_mid_add_name("seven_tk", tuner_reads, NULL) < 0 || !(g_strings = rc_guest_alloc(6 * 8))) return;
  for (int s = 0; s < 6; s++) snprintf(g_strings + 8 * s, 8, "string%d", s);
}

const rsm_mod rsm_mod_pitch_shifter = {"pitch_shifter", tick, key, NULL, NULL};
