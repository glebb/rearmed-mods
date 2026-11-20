// rs-tonehost: plays the guitar through an Audio Unit in place of the game's tones. A process of its own, so a plug-in's
// licence check, window or crash stays out of the game. The mod (tonehost_mod.c) reports the gear of each tone as the
// game applies it, about two seconds ahead of the change in the songs tried. Two instances take turns: one plays, the
// other gets the next tone's saved state, and a crossfade joins them, because a load is audible and an instance
// standing by is not. Nothing here knows a plug-in: a template is a saved state, parameter values and a gain. Where a
// plug-in's engine takes a preset from outside, as Helix Native's does (helix/engine.h), a template can have that preset
// in the state's place, and the host hands it over with the tone's values in it.
//
//   rs-tonehost --config FILE     FILE is JSON: see README.md
//
// Commands, one per line, on the FIFO named by "fifo" in the config:
//   gear KEY=VALUE ...            a tone's knobs as the game sets them (the mod writes this)
//   use TEMPLATE                  play a template of the config by hand
//   mute                          fade everything out
//   quit
#import <Cocoa/Cocoa.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <CoreAudioKit/CoreAudioKit.h>
#include <CommonCrypto/CommonDigest.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include <math.h>
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "gate_floor.h"
#include "helix/engine.h"
#include "preset_places.h"
#include "splice_shift.h"
#include "tone_gain.h"
#include "tone_level.h"

#define SLOTS 4  // at most: "slots" in the config says how many, 2 if it doesn't
#define MAX_FRAMES 1024
#define RING 8192  // input samples, a power of two
#define RATE 48000.0

typedef struct {
  AudioUnit unit;
  _Atomic int ready;     // the audio thread renders it
  _Atomic float gain;    // linear
  _Atomic int probing;   // hears the reference guitar in place of the player's
  _Atomic uint32_t probe_count;
  double probe_sum;      // audio thread: output power while probing, weighted as the ear hears it
  double probe_weigh[4];
  uint32_t probe_pos;
  double sample_time;
  float left[MAX_FRAMES], right[MAX_FRAMES];
} slot_t;

static slot_t slots[SLOTS];
static AudioComponentDescription g_plugin;
static float g_ring[RING];
static _Atomic uint64_t g_ring_write;
static uint64_t g_ring_read;
static int g_channel;               // input channel, from 0
static float g_input_gain = 1.f;
static float g_gain_offset;  // the commands' thread alone
static const float *g_block;        // the input of the block being rendered
static _Atomic int g_target = -1;   // the slot asked for; -1 none
static int g_current = -1;          // audio thread: the slot playing
static int g_previous = -1;         // audio thread: the slot fading out
static uint32_t g_fade_left, g_fade_frames = 960;  // 20 ms
static _Atomic uint32_t g_late_blocks, g_empty_blocks, g_peak_in, g_peak_out;  // peaks: float bits
// How long a buffer takes to make, for the log: the longest since the last line, the shifter's share of the longest
// it took, the buffers that took over three quarters of the time one lasts, and the overloads the system told of.
// A buffer that takes longer than it lasts is a gap in the sound.
static _Atomic uint64_t g_busy_most, g_shift_most;  // in the clock's ticks
static _Atomic uint32_t g_busy_long, g_overloads;
static mach_timebase_info_data_t g_clock;
static uint32_t g_target_fill = 128;
static _Atomic int g_quit;
static _Atomic float g_system_gain = 1.f, g_mixer_gain = 1.f;  // the system's volume, and the game's guitar mixer
static float g_master = 1.f;        // audio thread: both, smoothed
static float g_probe_peak = 0.25f;  // how hard the reference guitar plays: "probe_peak_db"
static float g_phrase[LEVEL_LOOP];  // the reference guitar (tone_level.h), made in main
static _Atomic uint64_t g_last_heard;  // when the mod last wrote, in seconds; 0: not yet
static _Atomic int g_standby = -1;  // the slot being made ready; it renders though nobody hears it

// A gate on the input, ahead of the plug-in: a high-gain amp makes a loud hiss of a quiet string or a hum. How much noise
// there is depends on the guitar, so it learns that: the floor is the quietest 100 ms lately, and the gate opens a margin
// above it. How tight it is depends on the tone: a clean tone keeps its note tails, a high-gain one stops between riffs.
static int g_gate_on;
static float g_gate_env, g_gate_gain = 1.f, g_gate_attack, g_window_peak;
static gate_floor_t g_floor = {.db = -70.f};
static float g_gate_margin = 12.f, g_gate_tight_db = 8.f, g_gate_min = -72.f, g_gate_max = -40.f;
static uint32_t g_gate_hold_left, g_window_left;
static int g_gate_is_open;
static float g_hold_loose = 150.f, g_hold_tight = 40.f, g_release_loose = 120.f, g_release_tight = 30.f;
static _Atomic float g_tightness, g_gate_at_least = -200.f;  // of the tone: 0 to 1, and the game's own gate, if it has one
static _Atomic float g_open_db = -60.f;

static void say(const char *format, ...) {
  va_list a;
  va_start(a, format);
  fputs("[tonehost] ", stderr);
  vfprintf(stderr, format, a);
  fputc('\n', stderr);
  va_end(a);
}

// ---- Devices ----
static AudioObjectID find_device(NSString *name) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &size)) return 0;
  AudioObjectID ids[size / sizeof(AudioObjectID)];
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, ids)) return 0;
  for (UInt32 i = 0; i < size / sizeof(AudioObjectID); i++) {
    AudioObjectPropertyAddress n = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef s = NULL;
    UInt32 ss = sizeof s;
    if (AudioObjectGetPropertyData(ids[i], &n, 0, NULL, &ss, &s) || !s) continue;
    BOOL same = [[(__bridge NSString *)s stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] isEqualToString:name];  // some end in a space
    CFRelease(s);
    if (same) return ids[i];
  }
  return 0;
}

static AudioObjectID default_output(void) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectID id = 0;
  UInt32 size = sizeof id;
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, &id);
  return id;
}

static void set_buffer(AudioObjectID dev, UInt32 frames) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectSetPropertyData(dev, &a, 0, NULL, sizeof frames, &frames);
  UInt32 got = 0, size = sizeof got;
  AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &got);
  say("device %u: buffer %u frames", dev, got);
}

static void set_rate(AudioObjectID dev) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  Float64 r = RATE;
  AudioObjectSetPropertyData(dev, &a, 0, NULL, sizeof r, &r);  // the game asks for the same
}

static double device_rate(AudioObjectID dev) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  Float64 r = 0;
  UInt32 size = sizeof r;
  AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &r);
  return r;
}

// ---- Audio ----
static sp *g_shift;  // the shifter behind Pitch Shifter (pitch/splice_shift.c), told by the game's "shift"; made in main

// "keep_guitar_s": the guitar as it comes in (after the input's gain, before the shifter and the gate), the last so
// many seconds of it, written to guitar.wav beside the config when the host quits. For looking into a sound that only
// the player's own guitar makes: off unless the config asks.
static float *g_kept;
static uint64_t g_kept_len;  // samples, a power of two
static _Atomic uint64_t g_kept_at;
static NSString *g_kept_path;

static void keep_guitar(void) {
  if (!g_kept) return;
  uint64_t at = atomic_load(&g_kept_at), n = at < g_kept_len ? at : g_kept_len;
  FILE *f = n ? fopen(g_kept_path.fileSystemRepresentation, "wb") : NULL;
  if (!f) return;
  uint32_t bytes = (uint32_t)(n * 4), rate = (uint32_t)RATE;
  uint32_t head[11] = {0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00010003, rate, rate * 4, 0x00200004, 0x61746164, bytes};
  fwrite(head, 4, 11, f);
  for (uint64_t i = at - n; i < at; i++) fwrite(&g_kept[i & (g_kept_len - 1)], 4, 1, f);
  fclose(f);
  say("the last %.0f s of guitar are in %s", n / RATE, g_kept_path.UTF8String);
}

static OSStatus input_proc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *in_time,
                           AudioBufferList *out, const AudioTimeStamp *out_time, void *user) {
  // Finds the buffer that holds the channel: a device may give one interleaved buffer or several
  int base = 0;
  for (UInt32 b = 0; b < in->mNumberBuffers; b++) {
    const AudioBuffer *buf = &in->mBuffers[b];
    int ch = (int)buf->mNumberChannels;
    if (g_channel < base + ch) {
      const float *s = (const float *)buf->mData + (g_channel - base);
      uint32_t frames = buf->mDataByteSize / (uint32_t)(ch * sizeof(float));
      uint64_t w = atomic_load_explicit(&g_ring_write, memory_order_relaxed);
      for (uint32_t i = 0; i < frames; i++) g_ring[w++ & (RING - 1)] = s[i * ch] * g_input_gain;
      atomic_store_explicit(&g_ring_write, w, memory_order_release);
      break;
    }
    base += ch;
  }
  return noErr;
}

// What a tone's loudness is measured with: the reference guitar, a phrase of half a second going round, heard by nobody
static void probe_signal(slot_t *s, float *x, UInt32 n) {
  for (UInt32 i = 0; i < n; i++) x[i] = g_probe_peak * g_phrase[s->probe_pos++ % LEVEL_LOOP];
}

static OSStatus plugin_input(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *t, UInt32 bus, UInt32 n, AudioBufferList *io) {
  slot_t *s = ref;
  if (atomic_load_explicit(&s->probing, memory_order_relaxed)) {
    probe_signal(s, io->mBuffers[0].mData, n);
    for (UInt32 c = 1; c < io->mNumberBuffers; c++) memcpy(io->mBuffers[c].mData, io->mBuffers[0].mData, n * sizeof(float));
  } else
    for (UInt32 c = 0; c < io->mNumberBuffers; c++) memcpy(io->mBuffers[c].mData, g_block, n * sizeof(float));
  return noErr;
}

static void render_slot(slot_t *s, UInt32 n) {
  struct { UInt32 count; AudioBuffer b[2]; } list = {2, {{1, n * 4, s->left}, {1, n * 4, s->right}}};
  AudioTimeStamp ts = {0};
  ts.mFlags = kAudioTimeStampSampleTimeValid;
  ts.mSampleTime = s->sample_time;
  s->sample_time += n;
  AudioUnitRenderActionFlags flags = 0;
  AudioUnitRender(s->unit, &flags, &ts, 0, n, (AudioBufferList *)&list);
  if (atomic_load_explicit(&s->probing, memory_order_relaxed)) {
    for (UInt32 i = 0; i < n; i++) {
      double v = level_weigh(s->probe_weigh, s->left[i]);
      if (!isfinite(v)) { memset(s->probe_weigh, 0, sizeof s->probe_weigh); v = 0; }  // or the filter would keep it for good
      s->probe_sum += v * v;
    }
    atomic_fetch_add_explicit(&s->probe_count, n, memory_order_relaxed);
  }
}

static void gate(float *x, UInt32 n) {
  if (!g_gate_on) return;
  float tight = atomic_load_explicit(&g_tightness, memory_order_relaxed);
  float open_db = fminf(fmaxf(g_floor.db + g_gate_margin + tight * g_gate_tight_db, g_gate_min), g_gate_max);
  open_db = fmaxf(open_db, atomic_load_explicit(&g_gate_at_least, memory_order_relaxed));
  atomic_store_explicit(&g_open_db, open_db, memory_order_relaxed);
  float open = powf(10.f, open_db / 20.f), close = open * 0.5f;
  uint32_t hold = (uint32_t)((g_hold_loose + (g_hold_tight - g_hold_loose) * tight) * RATE / 1000);
  float release = 1.f - expf(-1000.f / ((g_release_loose + (g_release_tight - g_release_loose) * tight) * (float)RATE));
  for (UInt32 i = 0; i < n; i++) {
    float a = fabsf(x[i]);
    if (a > g_window_peak) g_window_peak = a;
    if (!g_window_left--) {
      gate_floor_window(&g_floor, 20.f * log10f(g_window_peak + 1e-9f), open_db);
      g_window_peak = 0;
      g_window_left = (uint32_t)(RATE / 10);
    }
    g_gate_env = a > g_gate_env ? a : g_gate_env * 0.9995f;
    if (g_gate_env > open) { g_gate_is_open = 1; g_gate_hold_left = hold; }
    else if (g_gate_env < close) {
      if (g_gate_hold_left) g_gate_hold_left--;
      else g_gate_is_open = 0;
    }
    g_gate_gain += ((float)g_gate_is_open - g_gate_gain) * (g_gate_is_open ? g_gate_attack : release);
    x[i] *= g_gate_gain;
  }
}

static void note_peak(_Atomic uint32_t *peak, const float *x, UInt32 n, int stride) {
  float m = 0;
  for (UInt32 i = 0; i < n; i++) { float a = fabsf(x[i * stride]); if (a > m) m = a; }
  uint32_t bits, old = atomic_load_explicit(peak, memory_order_relaxed);
  memcpy(&bits, &m, 4);
  if (bits > old) atomic_store_explicit(peak, bits, memory_order_relaxed);  // positive floats order as integers
}

static OSStatus output_proc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *in_time,
                            AudioBufferList *out, const AudioTimeStamp *out_time, void *user) {
  uint64_t began = mach_absolute_time();
  AudioBuffer *ob = &out->mBuffers[0];
  int och = (int)ob->mNumberChannels;
  UInt32 n = ob->mDataByteSize / (UInt32)(och * sizeof(float));
  if (n > MAX_FRAMES) n = MAX_FRAMES;
  for (UInt32 b = 0; b < out->mNumberBuffers; b++) memset(out->mBuffers[b].mData, 0, out->mBuffers[b].mDataByteSize);
  // The input, kept just ahead of the output: clocks of two devices drift apart
  static float block[MAX_FRAMES];
  uint64_t w = atomic_load_explicit(&g_ring_write, memory_order_acquire);
  uint64_t fill = w - g_ring_read;
  if (fill > g_target_fill + n + 256) { g_ring_read = w - g_target_fill - n; fill = w - g_ring_read; atomic_fetch_add(&g_late_blocks, 1); }
  if (fill < n) { memset(block, 0, sizeof block); atomic_fetch_add(&g_empty_blocks, 1); }
  else for (UInt32 i = 0; i < n; i++) block[i] = g_ring[g_ring_read++ & (RING - 1)];
  if (g_kept) {
    uint64_t at = atomic_load_explicit(&g_kept_at, memory_order_relaxed);
    for (UInt32 i = 0; i < n; i++) g_kept[at++ & (g_kept_len - 1)] = block[i];
    atomic_store_explicit(&g_kept_at, at, memory_order_release);
  }
  uint64_t before_shift = mach_absolute_time();
  if (g_shift) sp_process(g_shift, block, block, (int)n);  // Pitch Shifter: nothing at 0 semitones
  uint64_t shift_took = mach_absolute_time() - before_shift;
  if (shift_took > atomic_load_explicit(&g_shift_most, memory_order_relaxed)) atomic_store_explicit(&g_shift_most, shift_took, memory_order_relaxed);
  note_peak(&g_peak_in, block, n, 1);
  gate(block, n);
  g_block = block;

  int want = atomic_load_explicit(&g_target, memory_order_acquire);
  if (want != g_current) { g_previous = g_current; g_current = want; g_fade_left = g_fade_frames; }
  for (int i = 0; i < SLOTS; i++)  // one never made isn't ready
    if (atomic_load_explicit(&slots[i].ready, memory_order_acquire) &&
        (i == g_current || (i == g_previous && g_fade_left) || i == atomic_load_explicit(&g_standby, memory_order_relaxed)))
      render_slot(&slots[i], n);

  float *dst = ob->mData;
  float *second = out->mNumberBuffers > 1 && och == 1 ? out->mBuffers[1].mData : NULL;  // a buffer for each channel
  float gc = g_current >= 0 ? atomic_load_explicit(&slots[g_current].gain, memory_order_relaxed) : 0.f;
  float gp = g_previous >= 0 ? atomic_load_explicit(&slots[g_previous].gain, memory_order_relaxed) : 0.f;
  float want_master = atomic_load_explicit(&g_system_gain, memory_order_relaxed) * atomic_load_explicit(&g_mixer_gain, memory_order_relaxed);
  for (UInt32 i = 0; i < n; i++) {
    float l = 0, r = 0;
    double t = g_fade_left ? 1.0 - (double)g_fade_left / g_fade_frames : 1.0;
    if (g_fade_left) g_fade_left--;
    g_master += (want_master - g_master) * 0.002f;
    if (g_current >= 0) { float a = (float)sin(t * M_PI_2) * gc * g_master; l += a * slots[g_current].left[i]; r += a * slots[g_current].right[i]; }
    if (g_previous >= 0 && t < 1.0) { float a = (float)cos(t * M_PI_2) * gp * g_master; l += a * slots[g_previous].left[i]; r += a * slots[g_previous].right[i]; }
    dst[i * och] = l;
    if (och > 1) dst[i * och + 1] = r;
    if (second) second[i] = r;
  }
  note_peak(&g_peak_out, dst, n, och);
  uint64_t took = mach_absolute_time() - began;
  if (took > atomic_load_explicit(&g_busy_most, memory_order_relaxed)) atomic_store_explicit(&g_busy_most, took, memory_order_relaxed);
  if ((double)took * g_clock.numer / g_clock.denom > 0.75e9 * n / RATE) atomic_fetch_add_explicit(&g_busy_long, 1, memory_order_relaxed);
  return noErr;
}

// Input and output on one device: one callback, one clock, and nothing held between them
static OSStatus duplex_proc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *in_time,
                            AudioBufferList *out, const AudioTimeStamp *out_time, void *user) {
  input_proc(dev, now, in, in_time, out, out_time, user);
  return output_proc(dev, now, in, in_time, out, out_time, user);
}

static AudioDeviceIOProc g_proc = output_proc;

static UInt32 device_frames(AudioObjectID dev, UInt32 selector, UInt32 scope) {
  AudioObjectPropertyAddress a = {selector, scope, kAudioObjectPropertyElementMain};
  UInt32 v = 0, size = sizeof v;
  AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &v);
  return v;
}

// With no "output" in the config the sound follows the system's output, as the game does.
static AudioObjectID g_out_dev;
static AudioDeviceIOProcID g_out_id;
static UInt32 g_frames;

static void start_output(AudioObjectID dev) {
  if (g_out_dev) { AudioDeviceStop(g_out_dev, g_out_id); AudioDeviceDestroyIOProcID(g_out_dev, g_out_id); g_out_id = NULL; }
  g_out_dev = dev;
  set_rate(dev);
  set_buffer(dev, g_frames);
  if (AudioDeviceCreateIOProcID(dev, g_proc, NULL, &g_out_id)) { say("device %u: no output", dev); return; }
  // The system says when a buffer was not ready in time: counted for the log
  AudioObjectPropertyAddress overload = {kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectAddPropertyListenerBlock(dev, &overload, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0),
                                      ^(UInt32 count, const AudioObjectPropertyAddress *which) { atomic_fetch_add(&g_overloads, 1); });
  AudioDeviceStart(dev, g_out_id);
  say("output on device %u", dev);
}

static void follow_output(void) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &a, dispatch_queue_create("output", NULL),
    ^(UInt32 count, const AudioObjectPropertyAddress *addresses) {
      AudioObjectID dev = default_output();
      if (dev && dev != g_out_dev) start_output(dev);
    });
}

// The volume of the system's output, as a gain: a device with no volume of its own counts as full. Where the host plays
// through that very device, the device turns the guitar down itself, and here as well would be twice
static float system_gain(void) {
  AudioObjectID dev = default_output();
  if (dev == g_out_dev) return 1.f;
  for (UInt32 element = 0; element < 3; element++) {
    AudioObjectPropertyAddress a = {kAudioDevicePropertyVolumeDecibels, kAudioObjectPropertyScopeOutput, element};
    Float32 db = 0, scalar = 1;
    UInt32 size = sizeof db;
    if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &db)) continue;
    a.mSelector = kAudioDevicePropertyVolumeScalar;
    AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &scalar);
    return scalar <= 0 ? 0.f : powf(10.f, db / 20.f);
  }
  return 1.f;
}

// ---- Plug-in instances ----
static int make_slot(slot_t *s) {
  AudioComponent c = AudioComponentFindNext(NULL, &g_plugin);
  if (!c || AudioComponentInstanceNew(c, &s->unit)) return -1;
  AudioStreamBasicDescription f = {RATE, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
  AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
  AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  UInt32 max = MAX_FRAMES;
  AudioUnitSetProperty(s->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max, sizeof max);
  AURenderCallbackStruct cb = {plugin_input, s};
  AudioUnitSetProperty(s->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
  if (AudioUnitInitialize(s->unit)) return -1;
  s->gain = 1.f;
  return 0;
}

static void load_state(int n, const char *path) {
  slot_t *s = &slots[n];
  atomic_store(&s->ready, 0);
  usleep(30000);  // the audio thread is past any render of it
  NSData *data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];
  id plist = data ? [NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL] : nil;
  if (!plist) { say("no state in %s", path); return; }
  CFPropertyListRef p = (__bridge CFPropertyListRef)plist;
  OSStatus st = AudioUnitSetProperty(s->unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &p, sizeof p);
  if (st) { say("slot %d: state refused (%d)", n, (int)st); return; }
  atomic_store(&s->ready, 1);
  say("slot %d: %s", n, path);
}

// ---- Tones ----
// A template of the config: a saved state, parameter values and an output gain. Which template a tone gets, and what
// of the game's knobs reaches which parameter, is the "map" of the config, so another plug-in needs no code.
static NSDictionary *g_templates, *g_map, *g_config;
static NSString *g_dir, *g_plugin_dir, *g_loaded[SLOTS], *g_config_stamp;  // the stamp: a hash of what a measured level depends on
static int g_playing = -1, g_slots = 2;
static NSString *g_playing_key, *g_playing_name;  // the tone in g_playing
static NSDictionary *g_playing_gear;
static float g_target_db = -200.f;

static float number_of(NSDictionary *gear, NSString *suffix, NSString *prefix, NSString *contains) {
  float best = -FLT_MAX;
  for (NSString *k in gear)
    if ([k hasSuffix:suffix] && (!prefix || [k hasPrefix:prefix]) && (!contains || [k rangeOfString:contains].location != NSNotFound))
      best = fmaxf(best, [gear[k] floatValue]);
  return best;
}

// The game's gain knob of the amp (a Plexi has two), 0 to 1, as the song's correction leaves it
static float amp_gain(NSDictionary *gear) {
  float g = -1;
  for (NSString *k in gear)
    if ([k rangeOfString:@"Amp_"].location != NSNotFound)
      for (NSString *s in g_map[@"gain_knobs"])
        if ([k hasSuffix:s]) g = fmaxf(g, [gear[k] floatValue]);
  return g < 0 ? 0 : corrected_gain(g / 100.f, g_gain_offset);
}

static NSString *pick(NSDictionary *gear) {
  for (NSString *k in gear)
    for (NSString *prefix in g_map[@"by_prefix"])
      if ([k hasPrefix:prefix]) return g_map[@"by_prefix"][prefix];
  float g = amp_gain(gear);
  for (NSString *k in gear)
    for (NSString *p in g_map[@"drive_pedals"])
      if ([k hasPrefix:p]) return g_map[@"driven"];
  return g >= [g_map[@"driven_from_gain"] floatValue] ? g_map[@"driven"] : g_map[@"clean"];
}

// A tone's parameters and their values, in the order they are set, a later one over an earlier: the template's own, then
// what the rules make of the game's knobs
static NSArray<NSArray *> *tone_params(NSDictionary *t, NSDictionary *gear) {
  NSMutableArray<NSArray *> *out = [NSMutableArray array];
  NSDictionary *fixed = t[@"params"];
  for (NSString *id in fixed) [out addObject:@[@(id.intValue), @([fixed[id] floatValue])]];
  // {"param": P, "from": "_Gain", "scale": 0.01, "offset": 0}: a knob of the game onto a parameter. The "map" has the
  // rules every template shares; a template can add its own.
  float gain = amp_gain(gear);
  for (NSArray *rules in @[g_map[@"follow"] ?: @[], t[@"follow"] ?: @[]])
    for (NSDictionary *f in rules) {
      if (f[@"present"]) {  // a switch that is on when the tone has one of this gear
        BOOL on = NO;
        for (NSString *k in gear)
          for (NSString *p in f[@"present"]) on = on || [k hasPrefix:p];
        [out addObject:@[@([f[@"param"] intValue]), @(on ? 1.f : 0.f)]];
        continue;
      }
      BOOL has = NO;
      for (NSString *k in gear)
        for (NSString *p in f[@"with"] ?: f[@"without"]) has = has || [k hasPrefix:p];
      if ((f[@"with"] && !has) || (f[@"without"] && has)) continue;
      BOOL is_gain = [f[@"from"] isEqual:@"amp_gain"];
      float v = is_gain ? gain : number_of(gear, f[@"from"], f[@"prefix"], f[@"contains"]);
      if (v == -FLT_MAX) continue;
      float to = v * [f[@"scale"] floatValue] + [f[@"offset"] floatValue];
      if ([f[@"clamp"] boolValue]) to = fminf(fmaxf(to, 0.f), 1.f);  // a parameter that takes no more than its range
      if (f[@"least"]) to = fmaxf(to, [f[@"least"] floatValue]);     // or one that mustn't go under something
      [out addObject:is_gain || ![f[@"prefix"] isKindOfClass:NSString.class] ? @[@([f[@"param"] intValue]), @(to)] : @[@([f[@"param"] intValue]), @(to), f[@"prefix"]]];
    }
  return out;
}

static void apply(int n, NSDictionary *t, NSDictionary *gear) {
  for (NSArray *p in tone_params(t, gear)) AudioUnitSetParameter(slots[n].unit, [p[0] unsignedIntValue], kAudioUnitScope_Global, 0, [p[1] floatValue], 0);
}

// Where no two rounds agreed, as with an octave pedal that follows the note in its own time: what the last four came to
static float unsettled_level(const double *power, int rounds) {
  double sum = 0;
  int from = rounds > 4 ? rounds - 4 : 0;
  for (int i = from; i < rounds; i++) sum += power[i];
  return level_db(sum, rounds - from > 0 ? rounds - from : 1);
}

// Plays the reference guitar through a slot nobody hears and returns how loud it comes out: the loudness of recorded
// guitar through the tone, within a dB or two (README.md has the measurement).
// A plug-in ramps a knob that was moved, Helix over about 0.3 s, and a clean tone's level follows the ramp where a
// distorted one's doesn't: so one round of the phrase after another, until two agree
static float measure(int n) {
  slot_t *s = &slots[n];
  atomic_store(&s->probing, 1);
  float level = -120.f, before = 0.f;
  double power[6] = {0};
  int i = 0, agreed = 0;
  for (; i < 6 && !agreed; i++) {
    atomic_store(&s->probe_count, 0);
    s->probe_sum = 0;
    usleep((useconds_t)(LEVEL_LOOP / RATE * 1e6));  // one round
    double sum = s->probe_sum;
    uint32_t count = atomic_load(&s->probe_count);
    before = level;
    power[i] = count ? sum / count : 0;
    level = level_db(power[i], 1);
    agreed = i && fabsf(level - before) < 0.5f;
  }
  atomic_store(&s->probing, 0);
  return agreed ? level : unsettled_level(power, i);
}

// The same guitar rendered here, one round of it, and its level: for a slot that no audio thread renders
static float round_level(slot_t *s) {
  atomic_store(&s->probing, 1);
  atomic_store(&s->probe_count, 0);
  s->probe_sum = 0;
  for (int n = 0; n < LEVEL_LOOP; n += 250) render_slot(s, 250);
  atomic_store(&s->probing, 0);
  return level_db(s->probe_sum, LEVEL_LOOP);
}

// As measure does: one round after another, until two agree
static float settled_level(slot_t *s) {
  float level = -120.f, before;
  double power[6] = {0};
  int i = 0, agreed = 0;
  for (; i < 6 && !agreed; i++) {
    before = level;
    level = round_level(s);
    power[i] = s->probe_sum / LEVEL_LOOP;
    agreed = i && fabsf(level - before) < 0.5f;
  }
  return agreed ? level : unsettled_level(power, i);
}

// What makes a tone's level, and a slot ready for it: its template, its knobs and the song's correction
static NSString *tone_key(NSString *name, NSDictionary *gear) {
  NSMutableString *key = [NSMutableString stringWithFormat:@"%@ %g", name, g_gain_offset];
  for (NSString *k in [gear.allKeys sortedArrayUsingSelector:@selector(compare:)]) [key appendFormat:@" %@=%@", k, gear[k]];
  return key;
}

static NSString *g_held[SLOTS];  // the tone a slot is ready for: state loaded, knobs set and their ramps over
static unsigned g_used[SLOTS], g_uses;

// A slot nobody hears: the one that had this tone last, which is ready as it is, or else the one unused longest
static int slot_for(NSString *key) {
  int n = -1;
  for (int i = 0; i < g_slots; i++)
    if (i != g_playing && (n < 0 || [key isEqual:g_held[i]] || (![key isEqual:g_held[n]] && g_used[i] < g_used[n]))) n = i;
  g_used[n] = ++g_uses;
  return n;
}

// A template has a saved state, or, where the plug-in's engine takes presets, it may have a preset alone
static BOOL has_state(NSDictionary *t) { return [t isKindOfClass:NSDictionary.class] && [t[@"state"] isKindOfClass:NSString.class] && [t[@"state"] length]; }

static BOOL any_state(NSDictionary *templates) {
  for (NSString *name in templates)
    if (has_state(templates[name])) return YES;
  return NO;
}

// A template's state is beside the plug-in's profile, or beside the config where the config has the plug-in itself
static NSString *state_path(NSDictionary *t) {
  NSString *state = [t isKindOfClass:NSDictionary.class] && [t[@"state"] isKindOfClass:NSString.class] ? t[@"state"] : @"";
  return [state hasPrefix:@"/"] ? state : [g_plugin_dir stringByAppendingPathComponent:state];
}

// ---- A tone's preset, built for the plug-in's engine ----
// A profile that names the plug-in's engine ("engine": "helix") has a "frame", a preset with nothing in it, the "spots"
// in it where a block can go, in the signal's order, and its "blocks" in the order of the chain: each a file, the
// parameter that turns it on ("on") and the places of its parameters' values in it (preset_places.h), counted from the
// block's start. {"amp": true} among them stands for the template's own "block". A tone's preset is the frame with
// the amp's block and every block the tone's rules turn on, each in the next spot, and the tone's values in their
// places: a block the tone has no gear for isn't there at all. Where the tone says its chain ("@chain", the game's gear
// in the signal's order, which the mod has for a song's tones), the blocks go in that order, each where the first
// of its "gear" is, and the amp where the chain's amp is. A template's "places" are its amp's knobs, each with
// the block it is in before it: "amp", or another block's name where the amp lends that block a knob. The preset is
// handed to the engine: no state is loaded for it and no knob moved, so nothing ramps. The parameters that have no
// place in any block are set on the plug-in as ever. Such templates need no states, and a profile whose templates
// have none always goes this way. Where they have both, "direct" chooses, a slot's first tone still loads its state,
// once, and --check-sound says whether the blocks are the plug-in's own, which only a state can tell. Only presets
// that --check-sound has passed are played.
static BOOL g_direct;
static int g_engine[SLOTS];         // a slot's instance in the engine
static NSData *g_in_engine[SLOTS];  // the preset a slot's engine was handed last

static BOOL builds_presets(NSDictionary *c) { return [c[@"engine"] isEqual:@"helix"] && [c[@"frame"] isKindOfClass:NSString.class]; }

static NSString *engine_file(id name) {
  NSString *file = [name isKindOfClass:NSString.class] ? name : @"";
  return [file hasPrefix:@"/"] ? file : [g_plugin_dir stringByAppendingPathComponent:file];
}

static int place_of(NSArray<NSNumber *> *place, double at[3]) {
  if (![place isKindOfClass:NSArray.class] || place.count > 3) return 0;
  for (NSUInteger i = 0; i < place.count; i++) at[i] = [place[i] doubleValue];
  return (int)place.count;
}

// A place counted from a block's start, as one counted from the preset's: the block starts at `start`
static NSArray *place_from(NSArray *place, NSUInteger skip, NSUInteger start) {
  if (![place isKindOfClass:NSArray.class] || place.count <= skip || ![place[skip] isKindOfClass:NSNumber.class]) return nil;
  NSMutableArray *out = [[place subarrayWithRange:NSMakeRange(skip, place.count - skip)] mutableCopy];
  out[0] = @([place[skip] doubleValue] + (double)start);
  return out;
}

// A tone's preset for a template and its parameters' values, or nil if the profile can't make one. A value is a
// parameter and its number, and where a rule read it from one gear's knob, that gear's prefix: with a chain, a block
// is there once for each of its gear the tone has, a compressor pedal and a compressor rack each their own, and such a
// value goes to its own gear's block. `places`, where asked for, gets where each parameter of the blocks in it has
// its value. A value with no place in any block of the profile is set on the unit, where there is one
static NSMutableData *composed(NSDictionary *c, NSDictionary *t, NSArray<NSArray *> *values, NSString *chain, AudioUnit unit, NSMutableDictionary *places) {
  BOOL still = [chain isKindOfClass:NSString.class] && [chain hasPrefix:@"still:"];  // without the "moving" blocks: see ready_level
  if (still) chain = [chain substringFromIndex:6];
  NSMutableData *preset = [NSMutableData dataWithContentsOfFile:engine_file(c[@"frame"])];
  NSArray *spots = c[@"spots"], *profiles = c[@"blocks"];
  if (!preset.length || ![spots isKindOfClass:NSArray.class] || ![profiles isKindOfClass:NSArray.class]) return nil;
  for (NSDictionary *b in profiles)
    if (![b isKindOfClass:NSDictionary.class]) return nil;
  // The blocks in the tone's own order where it says one, each with the gear it is there for, then the rest in the
  // profile's, for no gear in particular
  NSMutableArray<NSArray *> *blocks = [NSMutableArray array];
  NSMutableSet *there = [NSMutableSet set];
  for (NSString *gear in [chain isKindOfClass:NSString.class] ? [chain componentsSeparatedByString:@","] : @[])
    for (NSDictionary *b in profiles) {
      BOOL amp = [b[@"amp"] boolValue];
      if (amp ? [gear rangeOfString:@"Amp_"].location == NSNotFound || [there containsObject:b] : ![b[@"gear"] isKindOfClass:NSArray.class] || ![b[@"gear"] containsObject:gear]) continue;
      [blocks addObject:@[b, gear]];
      [there addObject:b];
    }
  for (NSDictionary *b in profiles)
    if (![there containsObject:b]) [blocks addObject:@[b, @""]];
  NSMutableDictionary<NSString *, NSNumber *> *last = [NSMutableDictionary dictionary], *start = [NSMutableDictionary dictionary];
  NSMutableSet<NSString *> *known = [NSMutableSet set];  // every parameter with a place in some block, in this tone or not
  NSMutableArray<NSArray *> *where = [NSMutableArray array];  // a block in the tone: its gear's prefix, or "", and its parameters' places
  for (NSArray *v in values) last[[v[0] stringValue]] = v[1];
  NSUInteger spot = 0;
  for (NSArray *entry in blocks) {
    NSDictionary *b = entry[0];
    BOOL amp = [b[@"amp"] boolValue];
    NSDictionary *own = !amp && [b[@"places"] isKindOfClass:NSDictionary.class] ? b[@"places"] : @{};
    [known addObjectsFromArray:own.allKeys];
    if (!amp && ([last[[b[@"on"] description]] floatValue] < 0.5f || (still && [b[@"moving"] boolValue]))) continue;
    NSData *block = [NSData dataWithContentsOfFile:engine_file(amp ? t[@"block"] : b[@"file"])];
    if (!block.length || spot >= spots.count) return nil;
    NSUInteger at = [spots[spot++] unsignedIntegerValue];
    if (at > preset.length || block.length > preset.length - at) return nil;
    [preset replaceBytesInRange:NSMakeRange(at, block.length) withBytes:block.bytes];
    NSString *name = amp ? @"amp" : [b[@"name"] description];
    if (!start[name]) start[name] = @(at);  // the first of its kind is the one an amp lends a knob to
    NSMutableDictionary *its = [NSMutableDictionary dictionary];
    for (NSString *id in own) its[id] = place_from(own[id], 0, at);
    [where addObject:@[[entry[1] length] ? [entry[1] stringByAppendingString:@"_"] : @"", its]];
  }
  NSDictionary *mine = [t[@"places"] isKindOfClass:NSDictionary.class] ? t[@"places"] : @{};
  NSMutableDictionary *amps = [NSMutableDictionary dictionary];
  for (NSString *id in mine) {  // the amp's knobs: in the amp, or in the block it lends one to, if the tone has that
    NSArray *place = [mine[id] isKindOfClass:NSArray.class] && [mine[id] count] ? mine[id] : nil;
    NSNumber *at = place ? start[[place[0] description]] : nil;
    [known addObject:id];
    if (at) amps[id] = place_from(place, 1, at.unsignedIntegerValue);
    for (NSArray *w in where) [w[1] removeObjectForKey:id];  // a knob the amp has is not a block's own
  }
  [where addObject:@[@"", amps]];
  for (NSArray *v in values) {
    NSString *id = [v[0] stringValue], *from = v.count > 2 ? v[2] : @"";
    BOOL placed = NO;
    for (NSArray *w in where) {
      NSArray *place = w[1][id];
      if (!place || ([w[0] length] && from.length && ![w[0] isEqual:from])) continue;  // another gear's block of this kind
      double at[3] = {0};
      if (place_value(preset.mutableBytes, preset.length, at, place_of(place, at), [v[1] floatValue])) return nil;
      placed = YES;
    }
    if (!placed && unit && ![known containsObject:id]) AudioUnitSetParameter(unit, [v[0] unsignedIntValue], kAudioUnitScope_Global, 0, [v[1] floatValue], 0);
  }
  for (NSArray *w in where) [places addEntriesFromDictionary:w[1]];
  return preset;
}

// Whether a preset of ours is the one the plug-in has, but for the last digits of a knob
static BOOL same_preset(NSMutableData *ours, NSData *theirs, NSDictionary *places) {
  if (!ours || ours.length != theirs.length) return NO;
  for (NSString *id in places) {
    double at[3] = {0};
    if (!place_same(ours.mutableBytes, theirs.bytes, ours.length, at, place_of(places[id], at))) return NO;
  }
  return [ours isEqualToData:theirs];
}

// Whether slot n is this instance of the engine: silent with that one muted, and not without. The engine numbers its
// instances as they are made, and so are the slots, but a wrong one would change the tone that is playing
static BOOL engine_is(int n, int instance) {
  slot_t *s = &slots[n];
  if (helix_engine_mute(instance, 1)) return NO;
  round_level(s);
  float muted = round_level(s);
  helix_engine_mute(instance, 0);
  round_level(s);
  return muted < -90.f && round_level(s) > -90.f;
}

// The tone's preset in the slot's engine. NO if that can't be: the tone then goes by its state. `still`: the tone
// without its "moving" blocks, for measuring its level
static BOOL load_direct(int n, NSString *name, NSDictionary *t, NSDictionary *gear, BOOL still) {
  slot_t *s = &slots[n];
  if (!g_loaded[n] && has_state(t)) load_state(n, state_path(t).UTF8String);  // once, where there is one
  g_loaded[n] = name;
  NSData *preset = composed(g_config, t, tone_params(t, gear), still ? [@"still:" stringByAppendingString:gear[@"@chain"] ?: @""] : gear[@"@chain"], s->unit, nil);
  if (!preset) { say("no preset could be made for %s", name.UTF8String); return NO; }
  if ([preset isEqualToData:g_in_engine[n]]) return YES;
  atomic_store(&s->ready, 0);
  usleep(30000);  // the audio thread is past any render of it
  int refused = helix_engine_load(g_engine[n], preset.bytes, preset.length);
  atomic_store(&s->ready, 1);
  if (refused) { say("slot %d: the engine refused %s's preset (%d)", n, name.UTF8String, refused); return NO; }
  g_in_engine[n] = preset;
  return YES;
}

// The state and the knobs, in a slot that renders by now. NO if it had the tone already: then there is nothing to wait for
static BOOL make_ready(int n, NSString *name, NSDictionary *gear, NSString *key) {
  NSDictionary *t = g_templates[name];
  if (g_direct && !load_direct(n, name, t, gear, NO)) {
    if (has_state(t)) {
      say("tones go by their saved states from here");
      g_direct = NO;
      for (int i = 0; i < SLOTS; i++) g_loaded[i] = g_held[i] = nil, g_in_engine[i] = nil;  // no slot's plug-in knows what its engine has
    } else
      say("slot %d keeps the tone it had: %s has no state to go by", n, name.UTF8String);
  }
  if (!g_direct) {
    if (![g_loaded[n] isEqual:name]) {
      NSString *path = state_path(t);
      load_state(n, path.UTF8String);
      g_loaded[n] = name;
      g_held[n] = nil;
    }
    apply(n, t, gear);
  }
  BOOL had = [key isEqual:g_held[n]];
  g_held[n] = key;
  return !had;
}

// A slot, rendering by now, made ready for a tone, and the level the tone comes out at. A level is measured once and
// kept in levels.plist, for as long as the config's templates and rules stay as they are: measuring takes a second
// or more. All that takes time about a tone is here, so that it can be done before the tone is due.
static float ready_level(int n, NSString *name, NSDictionary *gear, NSString *key, BOOL *measured) {
  static NSMutableDictionary<NSString *, NSNumber *> *levels;
  static NSString *file;
  if (!levels) {
    file = [g_plugin_dir stringByAppendingPathComponent:@"levels.plist"];
    NSDictionary *saved = [NSDictionary dictionaryWithContentsOfFile:file];
    levels = [saved[@"config"] isEqual:g_config_stamp] ? [saved[@"levels"] mutableCopy] : nil;
    if (!levels) levels = [NSMutableDictionary dictionary];
  }
  NSNumber *known = levels[key];
  *measured = !known;
  if (!known && g_direct && load_direct(n, name, g_templates[name], gear, YES)) {
    // A tone plays louder and quieter as a flanger, a chorus or a delay in it moves, by ten dB with a slow flanger, and
    // a level measured over a round or two is then anything in between. So its level is the one it has without them:
    // measured that way first, and then the tone itself is made ready
    g_held[n] = nil;
    float level = measure(n);
    if (level > -100) {
      levels[key] = @(level);
      [@{@"config": g_config_stamp, @"levels": levels} writeToFile:file atomically:YES];
    }
    if (make_ready(n, name, gear, key)) usleep(100000);
    return level;
  }
  BOOL changed = make_ready(n, name, gear, key);
  if (known) {
    // The knobs' ramps. A preset handed to the engine has its values from the start, but for some 60 to 80 ms after any
    // load the engine plays under its level
    if (changed) usleep(g_direct ? 100000 : 300000);
    return known.floatValue;
  }
  float level = measure(n);
  if (level > -100) {  // not a slot whose state didn't load
    levels[key] = @(level);
    [@{@"config": g_config_stamp, @"levels": levels} writeToFile:file atomically:YES];
  }
  return level;
}

static void use_template(NSString *name, NSDictionary *gear) {
  NSDictionary *t = g_templates[name];
  if (!t) { say("no template %s", name.UTF8String); return; }
  NSString *key = tone_key(name, gear);
  if (g_playing >= 0 && [key isEqual:g_playing_key]) return;  // the same again while it plays is nothing to do
  CFAbsoluteTime began = CFAbsoluteTimeGetCurrent();
  int n = slot_for(key);
  atomic_store(&g_standby, n);
  BOOL measured;
  float level = ready_level(n, name, gear, key, &measured), out = [t[@"output_db"] floatValue];
  if (g_target_db > -100) out = g_target_db + [t[@"trim_db"] floatValue] - level;
  // {"db": 1.5, "with": ["Pedal_X"], "clean": true}: louder or quieter for a tone with this gear, or for a clean one, which
  // is one with no drive pedal and its amp's gain under driven_from_gain
  for (NSDictionary *r in g_map[@"trim"]) {
    BOOL has = NO, driven = amp_gain(gear) >= [g_map[@"driven_from_gain"] floatValue];
    for (NSString *k in gear) {
      for (NSString *g in r[@"with"]) has = has || [k hasPrefix:g];
      for (NSString *p in g_map[@"drive_pedals"]) driven = driven || [k hasPrefix:p];
    }
    if (has || ([r[@"clean"] boolValue] && !driven)) out += [r[@"db"] floatValue];
  }
  atomic_store(&slots[n].gain, powf(10.f, out / 20.f));
  usleep(30000);  // the guitar reaches it before anyone hears it
  float game_gate = number_of(gear, @"_Thresh", @"Pedal_NoiseGate", nil);
  atomic_store(&g_gate_at_least, game_gate == -FLT_MAX ? -200.f : game_gate);
  atomic_store(&g_tightness, fminf(amp_gain(gear) * [t[@"tight"] ?: @1 floatValue], 1.f));
  atomic_store(&g_target, n);
  g_playing = n;
  g_playing_key = key;
  g_playing_name = name;
  g_playing_gear = gear;
  int took = (int)((CFAbsoluteTimeGetCurrent() - began) * 1000);
  usleep((useconds_t)(g_fade_frames / RATE * 1e6) + 50000);
  atomic_store(&g_standby, -1);
  say("play %s in slot %d: %s %.1f dB, output %+.1f dB, after %d ms", name.UTF8String, n, measured ? "measured" : "known", level, out, took);
}

// ---- A song's changes, ahead of the game ----
// The game says a tone as it changes, and a state's load and its knobs' ramps take half a second after that. But the mod
// sends where the song is, so the host keeps where in a song each tone came (changes.plist) and, the next time the song is
// played, has that tone ready in the slot nobody hears and plays it "early_ms" before its place. A change is one of a
// song's only with the tone the song began with and the tone before it, as a song's parts have tones of their own. The
// game's own word then finds the tone playing; if it doesn't come, the change was another part's: back to the tone before.
static NSMutableDictionary<NSString *, NSMutableArray<NSDictionary *> *> *g_changes;
static NSString *g_song, *g_first;  // the song, and the tone that played as it began or went back
static double g_song_at = -1, g_early = 0.1;
static NSDictionary *g_awaited, *g_back;  // a change played ahead that the game hasn't said yet, and the tone before it

static NSString *changes_file(void) { return [g_dir stringByAppendingPathComponent:@"changes.plist"]; }

static BOOL is_ours(NSDictionary *e) { return [e[@"first"] isEqual:g_first] && [e[@"before"] isEqual:g_playing_key]; }

// The plan of a song, from the mod as the song loads: the tone of each slot the song uses (0 the one it begins with) and
// its changes, each a time and a slot. With it the host needs nothing learned, and a song's first play is as any other.
static NSString *g_plan_song;
static NSMutableDictionary<NSNumber *, NSDictionary *> *g_plan_tones;
static NSMutableArray<NSArray<NSNumber *> *> *g_plan_at;
static BOOL g_plan_whole;
static int g_plan_played = -1;  // the last change played ahead: none at or before it again, until the song goes back

static BOOL has_plan(void) { return g_plan_whole && [g_plan_song isEqual:g_song]; }

static NSString *plan_key(NSNumber *slot) {
  NSDictionary *gear = g_plan_tones[slot];
  return gear ? tone_key(pick(gear), gear) : nil;
}

// A tone made ready in a slot nobody hears, measured if it never was
static void make_tone_ready(NSString *name, NSDictionary *gear, double at) {
  if (!g_templates[name] || g_playing < 0) return;
  NSString *key = tone_key(name, gear);
  if ([key isEqual:g_playing_key]) return;
  int n = slot_for(key);
  BOOL had = [key isEqual:g_held[n]], measured;
  atomic_store(&g_standby, n);
  float level = ready_level(n, name, gear, key, &measured);
  atomic_store(&g_standby, -1);
  if (!had) say("ready for %s at %.1f s in slot %d%s", name.UTF8String, at, n, measured ? [[NSString stringWithFormat:@": measured %.1f dB", level] UTF8String] : "");
}

// The next change from here has its tone made ready
static void ready_next(void) {
  if (has_plan()) {
    NSString *now = plan_key(@0);  // the tone the plan has at this place
    for (NSArray<NSNumber *> *c in g_plan_at) {
      NSString *key = plan_key(c[1]);
      if (!key) continue;
      if (c[0].doubleValue > g_song_at && ![key isEqual:now]) {
        make_tone_ready(pick(g_plan_tones[c[1]]), g_plan_tones[c[1]], c[0].doubleValue);
        return;
      }
      now = key;
    }
    return;
  }
  NSDictionary *next = nil;
  for (NSDictionary *e in g_changes[g_song])
    if (is_ours(e) && [e[@"at"] doubleValue] > g_song_at && (!next || [e[@"at"] doubleValue] < [next[@"at"] doubleValue])) next = e;
  if (next) make_tone_ready(next[@"name"], next[@"gear"], [next[@"at"] doubleValue]);
}

static void forget(NSDictionary *e) {
  [g_changes[g_song] removeObject:e];
  [g_changes writeToFile:changes_file() atomically:YES];
}

// "time SONG SECONDS" from the mod, while a song plays
static void song_time(NSString *song, double at) {
  if (!g_changes) {
    g_changes = [NSMutableDictionary dictionary];
    NSDictionary *saved = [NSDictionary dictionaryWithContentsOfFile:changes_file()];
    for (NSString *k in saved) g_changes[k] = [saved[k] mutableCopy];
  }
  BOOL began = ![song isEqual:g_song] || g_song_at < 0 || at < g_song_at - 1;  // a new song, or back in this one
  g_song = song;
  g_song_at = at;
  if (began) {
    g_first = g_playing_key;
    g_awaited = nil;
    g_plan_played = -1;
    ready_next();
    return;
  }
  if (has_plan()) {
    for (int i = 0; i < (int)g_plan_at.count; i++) {
      double due = g_plan_at[i][0].doubleValue;
      NSDictionary *gear = g_plan_tones[g_plan_at[i][1]];
      if (i <= g_plan_played || !gear || at < due - g_early || at >= due + 0.3) continue;
      g_plan_played = i;
      if ([plan_key(g_plan_at[i][1]) isEqual:g_playing_key]) continue;
      say("ahead of the game: the song's change at %.1f s", due);
      use_template(pick(gear), gear);
      ready_next();
      return;
    }
    return;
  }
  if (g_awaited) {
    if (at < [g_awaited[@"at"] doubleValue] + 1.5) return;
    say("the game didn't change the tone at %.1f s: back", [g_awaited[@"at"] doubleValue]);
    forget(g_awaited);
    NSDictionary *back = g_back;
    g_awaited = nil;
    use_template(back[@"name"], back[@"gear"]);
    return;
  }
  for (NSDictionary *e in g_changes[g_song]) {
    double due = [e[@"at"] doubleValue];
    if (!is_ours(e) || at < due - g_early || at >= due + 0.3 || !g_templates[e[@"name"]]) continue;
    say("ahead of the game: the change at %.1f s", due);
    g_back = @{@"name": g_playing_name, @"gear": g_playing_gear};
    g_awaited = e;
    use_template(e[@"name"], e[@"gear"]);
    ready_next();
    return;
  }
}

// The game's own word of a tone, at a place in a song: kept for the next time
static void learn(NSString *name, NSDictionary *gear) {
  NSString *key = tone_key(name, gear);
  if (has_plan()) return;
  if (g_awaited && [tone_key(g_awaited[@"name"], g_awaited[@"gear"]) isEqual:key]) { g_awaited = nil; return; }  // as expected
  if (g_awaited) forget(g_awaited);  // another tone than the one played ahead
  g_awaited = nil;
  if (!g_song || g_song_at < 0 || !g_first || !g_playing_key || [key isEqual:g_playing_key]) return;
  NSMutableArray *list = g_changes[g_song] ?: (g_changes[g_song] = [NSMutableArray array]);
  for (NSDictionary *e in list.copy)
    if ([e[@"first"] isEqual:g_first] && fabs([e[@"at"] doubleValue] - g_song_at) < 1) [list removeObject:e];
  [list addObject:@{@"at": @(g_song_at), @"first": g_first, @"before": g_playing_key, @"name": name, @"gear": gear}];
  [g_changes writeToFile:changes_file() atomically:YES];
}

static NSDictionary *gear_of(const char *text) {
  NSMutableDictionary *gear = [NSMutableDictionary dictionary];
  for (NSString *pair in [[NSString stringWithUTF8String:text] componentsSeparatedByString:@" "]) {
    NSArray *kv = [pair componentsSeparatedByString:@"="];
    if (kv.count == 2) gear[kv[0]] = kv[1];
  }
  return gear;
}

// A tone as the game says it has its knobs and not its chain. The song's plan has both: the plan's tone with these very
// knobs, where there is one
static NSDictionary *with_chain(NSDictionary *gear) {
  if (gear[@"@chain"]) return gear;
  for (NSDictionary *planned in g_plan_tones.allValues) {
    if (!planned[@"@chain"] || planned.count != gear.count + 1) continue;
    NSMutableDictionary *knobs = [planned mutableCopy];
    knobs[@"@chain"] = nil;
    if ([knobs isEqualToDictionary:gear]) return planned;
  }
  return gear;
}

static void command(char *line) {
  static NSDictionary *last_gear;
  static BOOL timed;  // the line before this one said where the song is: a tone after it is a change in the song
  BOOL was_timed = timed;
  timed = NO;
  if (!strncmp(line, "gear ", 5)) {
    NSDictionary *gear = with_chain(gear_of(line + 5));
    NSString *name = pick(gear);
    say("tone: gain %.2f, balance %s, %lu knobs -> %s%s%s", amp_gain(gear), [gear[@"Meter_Tone_Balance_Return"] UTF8String],
        (unsigned long)(gear.count - (gear[@"@chain"] ? 1 : 0)), name.UTF8String, gear[@"@chain"] ? ", chain " : "", [gear[@"@chain"] ?: @"" UTF8String]);
    last_gear = gear;
    if (was_timed) learn(name, gear);
    use_template(name, gear);
    if (was_timed) ready_next();
  } else if (!strncmp(line, "time ", 5)) {  // the song and where it is, from the mod; ahead of a tone too
    char *space = strchr(line + 5, ' ');
    if (space) song_time([[NSString alloc] initWithBytes:line + 5 length:(NSUInteger)(space - line - 5) encoding:NSUTF8StringEncoding], strtod(space + 1, NULL));
    timed = space != NULL;
    return;
  } else if (!strncmp(line, "plan ", 5)) {  // a song's tones and changes, from the mod as the song loads
    g_plan_song = [NSString stringWithUTF8String:line + 5];
    g_plan_tones = [NSMutableDictionary dictionary];
    g_plan_at = [NSMutableArray array];
    g_plan_whole = NO;
  } else if (!strncmp(line, "plantone ", 9)) {
    char *space = strchr(line + 9, ' ');
    if (space) g_plan_tones[@(atoi(line + 9))] = gear_of(space + 1);
  } else if (!strncmp(line, "planat", 6)) {
    for (NSString *pair in [[NSString stringWithUTF8String:line + 6] componentsSeparatedByString:@" "]) {
      NSArray<NSString *> *c = [pair componentsSeparatedByString:@":"];
      if (c.count == 2) [g_plan_at addObject:@[@(c[0].doubleValue), @(c[1].intValue)]];
    }
  } else if (!strncmp(line, "planend", 7)) {
    g_plan_whole = g_plan_tones[@0] != nil;
    g_plan_played = -1;
    say("the plan of %s: %lu tones, %lu changes", g_plan_song.UTF8String, (unsigned long)g_plan_tones.count, (unsigned long)g_plan_at.count);
    // A tone that came before its song's plan is playing without its chain: the same tone again, as the plan has it
    NSDictionary *planned = g_playing_gear ? with_chain(g_playing_gear) : nil;
    if (g_direct && planned && planned != g_playing_gear) use_template(pick(planned), planned);
  } else if (!strncmp(line, "offset ", 7)) {  // from RS Companion, as a song loads or its AI answer comes
    float v = strtof(line + 7, NULL);
    if (v == g_gain_offset) return;
    g_gain_offset = v;
    say("song's gain offset %+.2f", v);
    if (last_gear) use_template(pick(last_gear), last_gear);  // through the instance nobody hears, like any tone
  } else if (!strncmp(line, "shift ", 6)) {  // Pitch Shifter: the guitar that many semitones off, as the game hears it
    int v = atoi(line + 6);
    if (g_shift) sp_set_semitones(g_shift, v);
    say("pitch shifter %+d", v);
  } else if (!strncmp(line, "mixer ", 6)) {  // the game's guitar fader, 0 to 100
    float v = strtof(line + 6, NULL);
    atomic_store(&g_mixer_gain, v / 100.f);
    say("game's guitar mixer %.0f", v);
  } else if (!strncmp(line, "use ", 4)) use_template([NSString stringWithUTF8String:line + 4], @{});
  else if (!strncmp(line, "mute", 4)) { atomic_store(&g_target, -1); g_playing = -1; }
  else if (!strncmp(line, "quit", 4)) atomic_store(&g_quit, 1);
  else if (*line) say("unknown command: %s", line);
}

// 0 (no plug-in has it) for anything but four characters
static uint32_t fourcc(NSString *s) {
  const char *c = [s isKindOfClass:NSString.class] ? s.UTF8String : NULL;
  if (!c || strlen(c) != 4) return 0;
  return (uint32_t)c[0] << 24 | (uint32_t)c[1] << 16 | (uint32_t)c[2] << 8 | (uint32_t)c[3];
}

static NSDictionary *json_file(NSString *path) {
  NSData *data = [NSData dataWithContentsOfFile:path];
  id json = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
  return [json isKindOfClass:NSDictionary.class] ? json : nil;
}

static AudioComponentDescription plugin_of(NSDictionary *c) {
  NSDictionary *pl = [c[@"plugin"] isKindOfClass:NSDictionary.class] ? c[@"plugin"] : @{};
  return (AudioComponentDescription){fourcc(pl[@"type"]), fourcc(pl[@"subtype"]), fourcc(pl[@"manufacturer"]), 0, 0};
}

// What keeps a config from playing, found without opening the plug-in, or nil. Re-ARMed Mods' Tone Host settings
// check the same (ToneHostProfiles.swift), to offer only a plug-in that is set up
static NSString *config_problem(NSDictionary *c) {
  AudioComponentDescription d = plugin_of(c);
  if (!d.componentType || !d.componentSubType || !d.componentManufacturer) return @"it names no plug-in";
  if (!AudioComponentFindNext(NULL, &d)) return @"the plug-in isn't installed";
  NSDictionary *templates = c[@"templates"], *map = c[@"map"];
  if (![templates isKindOfClass:NSDictionary.class] || !templates.count) return @"it has no templates";
  if (![map isKindOfClass:NSDictionary.class]) return @"it has no map";
  NSMutableArray *named = [NSMutableArray array];
  for (NSString *k in @[@"clean", @"driven"]) {
    if (![map[k] isKindOfClass:NSString.class]) return [NSString stringWithFormat:@"the map has no \"%@\" template", k];
    [named addObject:map[k]];
  }
  if ([map[@"by_prefix"] isKindOfClass:NSDictionary.class]) [named addObjectsFromArray:[map[@"by_prefix"] allValues]];
  for (id name in named)
    if (![name isKindOfClass:NSString.class] || !templates[name]) return [NSString stringWithFormat:@"the map names a template that isn't there: %@", name];
  for (NSString *name in [templates.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
    NSDictionary *t = templates[name];
    if (!has_state(t)) {  // then a preset for the plug-in's engine
      if (!builds_presets(c) || ![t isKindOfClass:NSDictionary.class] || !composed(c, t, @[], nil, NULL, nil))
        return [NSString stringWithFormat:@"%@ has neither a state nor a block for a preset", name];
      continue;
    }
    NSString *path = state_path(t);
    NSDictionary *state = [NSDictionary dictionaryWithContentsOfFile:path];
    if (!state) return [NSString stringWithFormat:@"%@ has no state: %@", name, path];
    NSDictionary *codes = @{@"type": @(d.componentType), @"subtype": @(d.componentSubType), @"manufacturer": @(d.componentManufacturer)};
    for (NSString *k in codes)
      if (state[k] && ![state[k] isEqual:codes[k]]) return [NSString stringWithFormat:@"%@'s state is another plug-in's", name];
  }
  return nil;
}

// ---- Setting a sound: --edit ----
// The plug-in's own window, with the guitar playing through it as the config's interface has it. Closing the window
// saves what is set there as a state, and ends the host. Re-ARMed Mods' "Add a Plug-in" runs this for each sound.

static NSString *g_edit_file;

static int save_edit(void) {
  slot_t *s = &slots[0];
  atomic_store(&s->ready, 0);
  usleep(30000);  // the audio thread is past any render of it
  CFPropertyListRef state = NULL;
  UInt32 size = sizeof state;
  if (AudioUnitGetProperty(s->unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size) || !state) { say("the plug-in gave no state"); return 1; }
  NSData *data = [NSPropertyListSerialization dataWithPropertyList:CFBridgingRelease(state) format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
  if (![data writeToFile:g_edit_file atomically:YES]) { say("can't write %s", g_edit_file.UTF8String); return 1; }
  say("saved %s", g_edit_file.UTF8String);
  return 0;
}

@interface EditWindow : NSObject <NSWindowDelegate>
@end
@implementation EditWindow
- (void)windowWillClose:(NSNotification *)n {
  int failed = save_edit();
  AudioDeviceStop(g_out_dev, g_out_id);
  _exit(failed);
}
@end

// The plug-in's own view, or sliders for its parameters where it has none
static NSView *plugin_view(AudioUnit unit) {
  UInt32 size = 0;
  Boolean writable = 0;
  if (!AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable) && size >= sizeof(AudioUnitCocoaViewInfo)) {
    AudioUnitCocoaViewInfo *info = malloc(size);
    if (!AudioUnitGetProperty(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &size)) {
      NSBundle *bundle = [NSBundle bundleWithURL:(__bridge NSURL *)info->mCocoaAUViewBundleLocation];
      [bundle load];
      id<AUCocoaUIBase> factory = [[bundle classNamed:(__bridge NSString *)info->mCocoaAUViewClass[0]] new];
      NSView *view = [factory uiViewForAudioUnit:unit withSize:NSMakeSize(1100, 760)];
      if (view) return view;
    }
  }
  return [[AUGenericView alloc] initWithAudioUnit:unit];
}

static int edit_sound(NSString *file) {
  g_edit_file = file;
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  NSView *view = plugin_view(slots[0].unit);
  NSWindow *window = [[NSWindow alloc] initWithContentRect:view.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                                                   backing:NSBackingStoreBuffered defer:NO];
  static EditWindow *closer;
  closer = [EditWindow new];
  window.delegate = closer;
  window.title = @"Set the sound, then close this window to save it";
  window.contentView = view;
  // Command-V works through an Edit menu, in the plug-in's fields and in the file dialogs it opens; Command-Q closes
  // the window, so that it saves too
  NSMenu *bar = [NSMenu new], *app = [NSMenu new], *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
  [app addItemWithTitle:@"Save the Sound and Close" action:@selector(performClose:) keyEquivalent:@"q"].target = window;
  [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
  [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
  [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
  [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
  [bar addItemWithTitle:@"" action:NULL keyEquivalent:@""].submenu = app;
  [bar addItemWithTitle:@"Edit" action:NULL keyEquivalent:@""].submenu = edit;
  NSApp.mainMenu = bar;
  [window center];
  [window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  [NSApp run];
  return 0;
}

// What tells one set of saved states from another: a hash of every template's state, in the order of their names.
// ToneHostProfiles.swift makes the same
static NSString *states_stamp(NSDictionary *templates) {
  NSMutableData *all = [NSMutableData data];
  for (NSString *name in [templates.allKeys sortedArrayUsingComparator:^(NSString *a, NSString *b) { return [a compare:b options:NSLiteralSearch]; }]) {
    NSData *state = [NSData dataWithContentsOfFile:state_path(templates[name])] ?: [NSData data];
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(state.bytes, (CC_LONG)state.length, hash);
    [all appendBytes:hash length:sizeof hash];
  }
  unsigned char hash[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(all.bytes, (CC_LONG)all.length, hash);
  NSMutableString *hex = [NSMutableString string];
  for (size_t i = 0; i < sizeof hash; i++) [hex appendFormat:@"%02x", hash[i]];
  return hex;
}

// What tells one set of presets for the plug-in's engine from another: the profile, and every file of its presets
// folder with its name, in the order of the names. ToneHostProfiles.swift makes the same. nil if there is neither
static NSString *presets_stamp(void) {
  NSString *folder = [g_plugin_dir stringByAppendingPathComponent:@"presets"];
  NSMutableData *all = [NSMutableData dataWithContentsOfFile:[g_plugin_dir stringByAppendingPathComponent:@"profile.json"]];
  NSArray<NSString *> *names = [NSFileManager.defaultManager contentsOfDirectoryAtPath:folder error:NULL];
  NSUInteger files = 0;
  for (NSString *name in [names sortedArrayUsingComparator:^(NSString *a, NSString *b) { return [a compare:b options:NSLiteralSearch]; }]) {
    if ([name hasPrefix:@"."]) continue;
    [all appendData:[name dataUsingEncoding:NSUTF8StringEncoding]];
    [all appendData:[NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:name]] ?: [NSData data]];
    files++;
  }
  if (!all.length || !files) return nil;
  unsigned char hash[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(all.bytes, (CC_LONG)all.length, hash);
  NSMutableString *hex = [NSMutableString string];
  for (size_t i = 0; i < sizeof hash; i++) [hex appendFormat:@"%02x", hash[i]];
  return hex;
}

// What keeps the tones' presets from going straight to the plug-in's engine, or nil. The slots are open and no audio
// runs yet: finding a slot's instance in the engine renders it here
static NSString *direct_problem(NSDictionary *c) {
  NSString *stamp = presets_stamp();
  if (!builds_presets(c) || !stamp) return @"the profile has no presets for an engine";
  if (![json_file([g_plugin_dir stringByAppendingPathComponent:@"checked.json"])[@"presets"] isEqual:stamp]) return @"--check-sound hasn't passed these presets";
  if (!helix_engine_find()) return @"the plug-in's engine isn't there";
  for (int i = 0; i < g_slots; i++) {
    if (!engine_is(i, i)) return [NSString stringWithFormat:@"instance %d isn't the engine's %d", i, i];
    g_engine[i] = i;
  }
  return nil;
}

// A template's own parameters, and those with every block of the profile turned on
static NSMutableArray *own_params(NSDictionary *c, NSDictionary *t, BOOL every_block) {
  NSMutableArray *out = [NSMutableArray array];
  NSDictionary *own = t[@"params"];
  for (NSString *id in own) [out addObject:@[@(id.intValue), @([own[id] floatValue])]];
  for (NSDictionary *b in every_block ? c[@"blocks"] : @[])  // of the plug-in's own preset: the "more" ones are beside it
    if ([b isKindOfClass:NSDictionary.class] && b[@"on"] && ![b[@"more"] boolValue]) [out addObject:@[@([b[@"on"] intValue]), @1.f]];
  return out;
}

// What keeps a template's blocks from standing in for its state, or nil: put together they have to be the preset the
// plug-in itself has. The slot, which nothing else renders, has the template's state and its own parameters, and
// plays at `level`
static NSString *preset_problem(NSDictionary *c, NSString *name, NSDictionary *t, slot_t *s, float level) {
  // Every block on and every knob moved, on the plug-in and in a preset of ours: the two have to agree
  NSMutableDictionary *places = [NSMutableDictionary dictionary];
  NSMutableArray *moved = [NSMutableArray array];
  if (!composed(c, t, own_params(c, t, YES), nil, NULL, places)) return [NSString stringWithFormat:@"no preset could be made for %@", name];
  for (NSString *id in places) {
    float v = [places[id] count] == 1 ? 1.f : 0.25f;
    AudioUnitSetParameter(s->unit, (AudioUnitParameterID)id.intValue, kAudioUnitScope_Global, 0, v, 0);
    [moved addObject:@[@(id.intValue), @(v)]];
  }
  NSMutableData *ours = composed(c, t, moved, nil, NULL, places), *theirs = [NSMutableData dataWithLength:ours.length];
  if (helix_engine_read(0, theirs.mutableBytes, theirs.length) || !same_preset(ours, theirs, places))
    return [NSString stringWithFormat:@"%@'s blocks aren't the ones the plug-in has", name];
  // And with the amp alone, as a tone with no pedals has it, it plays as the state did with its effects off
  NSData *plain = composed(c, t, own_params(c, t, NO), nil, NULL, nil);
  if (!plain || helix_engine_load(0, plain.bytes, plain.length)) return [NSString stringWithFormat:@"the engine refused %@'s preset", name];
  float direct = settled_level(s);
  if (fabsf(direct - level) > 0.3f) return [NSString stringWithFormat:@"%@'s preset plays at %.1f dB, its state at %.1f", name, direct, level];
  return nil;
}

// What keeps the plug-in from playing a config that config_problem passes, or nil: it opens the plug-in, with no device
// and nothing heard, and sends the reference guitar through every template's state. A plug-in that isn't authorized, or a
// state it refuses, is silent here. `presets` gets what keeps the templates' presets from going straight to the
// plug-in's engine, or nil if they can: "" where the profile has none
static NSString *sound_problem(NSDictionary *c, NSString **presets) {
  NSDictionary *templates = c[@"templates"];
  NSMutableSet *different = [NSMutableSet set];
  NSUInteger with_state = 0;
  for (NSString *name in templates)
    if (has_state(templates[name])) {
      with_state++;
      [different addObject:[NSData dataWithContentsOfFile:state_path(templates[name])] ?: [NSData data]];
    }
  if (with_state > 1 && different.count == 1) return @"every template has the same state";
  g_plugin = plugin_of(c);
  if (c[@"probe_peak_db"]) g_probe_peak = powf(10.f, [c[@"probe_peak_db"] floatValue] / 20.f);
  slot_t *s = &slots[0];
  if (make_slot(s)) return @"the plug-in didn't open";
  NSString *problem = nil;
  *presets = !builds_presets(c) || !presets_stamp() ? @"" : !helix_engine_find() ? @"the plug-in's engine isn't there" : nil;
  BOOL first = YES;
  for (NSString *name in [templates.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
    if (!has_state(templates[name])) {
      // No state: the engine has to take the preset as it is, with every block and with the amp alone, and that has
      // to sound. Nothing says here that a block's values are in the engine's order: helix/verify.sh does, against
      // the plug-in's own presets
      NSDictionary *t = templates[name];
      NSMutableData *every = composed(c, t, own_params(c, t, YES), nil, NULL, nil), *ours = composed(c, t, own_params(c, t, NO), nil, s->unit, nil);
      NSMutableData *theirs = [NSMutableData dataWithLength:ours.length];
      if (*presets) problem = (*presets).length ? *presets : @"the profile names no engine for its presets";
      else if (first && !engine_is(0, 0)) problem = @"the plug-in's instance isn't the engine's first";
      else if (!every || !ours) problem = [NSString stringWithFormat:@"no preset could be made for %@", name];
      else if (helix_engine_load(0, every.bytes, every.length)) problem = [NSString stringWithFormat:@"the engine refused %@'s preset", name];
      for (NSDictionary *b in first && !problem ? c[@"blocks"] : @[]) {  // and each block besides those, with this amp
        if (![b isKindOfClass:NSDictionary.class] || ![b[@"more"] boolValue]) continue;
        NSMutableArray *with = own_params(c, t, NO);
        [with addObject:@[@([b[@"on"] intValue]), @1.f]];
        NSData *one = composed(c, t, with, nil, NULL, nil);
        if (!one || helix_engine_load(0, one.bytes, one.length)) problem = [NSString stringWithFormat:@"the engine refused the block %@", b[@"name"]];
      }
      if (!problem && helix_engine_load(0, ours.bytes, ours.length)) problem = [NSString stringWithFormat:@"the engine refused %@'s preset", name];
      if (!problem && (helix_engine_read(0, theirs.mutableBytes, theirs.length) || ![ours isEqualToData:theirs])) problem = [NSString stringWithFormat:@"the engine changed %@'s preset", name];
      if (problem) break;
      float level = settled_level(s);
      say("%s: %.1f dB", name.UTF8String, level);
      if (level < -90.f) { problem = [NSString stringWithFormat:@"%@ is silent", name]; break; }
      first = NO;
      continue;
    }
    load_state(0, state_path(templates[name]).UTF8String);
    if (!atomic_load(&s->ready)) { problem = [NSString stringWithFormat:@"the plug-in refused %@'s state", name]; break; }
    // The state as it was saved, and the template's own parameters: a rule would follow a knob of no tone, and an amp
    // with its gain at nothing is silent
    NSDictionary *fixed = [templates[name] objectForKey:@"params"];
    for (NSString *id in fixed) AudioUnitSetParameter(s->unit, id.intValue, kAudioUnitScope_Global, 0, [fixed[id] floatValue], 0);
    usleep(50000);  // a plug-in may take a state in its own time
    float level = settled_level(s);
    say("%s: %.1f dB", name.UTF8String, level);
    if (level < -90.f) { problem = [NSString stringWithFormat:@"%@ is silent", name]; break; }
    if (!*presets && first && !engine_is(0, 0)) *presets = @"the plug-in's instance isn't the engine's first";
    if (!*presets) *presets = preset_problem(c, name, templates[name], s, level);
    first = NO;
  }
  AudioUnitUninitialize(s->unit);
  AudioComponentInstanceDispose(s->unit);
  return problem;
}

int main(int argc, char **argv) { @autoreleasepool {
  // An installed plug-in's version, as a profile keeps it: nothing is opened
  if (argc == 5 && !strcmp(argv[1], "--plugin")) {
    AudioComponentDescription d = {fourcc(@(argv[2])), fourcc(@(argv[3])), fourcc(@(argv[4])), 0, 0};
    AudioComponent found = d.componentType && d.componentSubType && d.componentManufacturer ? AudioComponentFindNext(NULL, &d) : NULL;
    UInt32 version = 0;
    if (!found || AudioComponentGetVersion(found, &version)) { fprintf(stderr, "not installed\n"); return 1; }
    printf("%u\n", version);
    return 0;
  }
  BOOL sound = argc == 4 && !strcmp(argv[3], "--check-sound");
  BOOL check = sound || (argc == 4 && !strcmp(argv[3], "--check"));
  BOOL edit = argc == 8 && !strcmp(argv[3], "--edit");
  if ((argc != 3 && !check && !edit) || strcmp(argv[1], "--config")) {
    fprintf(stderr, "usage: rs-tonehost --config FILE [--check | --check-sound | --edit TYPE SUBTYPE MANUFACTURER STATE]\n       rs-tonehost --plugin TYPE SUBTYPE MANUFACTURER\n");
    return 2;
  }
  NSDictionary *c = json_file(@(argv[2]));
  if (!c) { say("can't read the config"); return 2; }
  level_phrase(g_phrase);
  g_dir = [@(argv[2]) stringByDeletingLastPathComponent];
  g_plugin_dir = g_dir;
  // The chosen plug-in's profile goes over the config: its codes, templates and map, and its states are beside it
  NSString *profile = [c[@"profile"] isKindOfClass:NSString.class] ? c[@"profile"] : @"";
  if (profile.length && !edit) {
    if ([profile hasPrefix:@"."] || [profile containsString:@"/"]) { say("not a profile's name: %s", profile.UTF8String); return 2; }
    g_plugin_dir = [[g_dir stringByAppendingPathComponent:@"plugins"] stringByAppendingPathComponent:profile];
    NSDictionary *p = json_file([g_plugin_dir stringByAppendingPathComponent:@"profile.json"]);
    if (!p) { say("can't read the profile of %s", profile.UTF8String); return 2; }
    NSMutableDictionary *m = [c mutableCopy];
    [m addEntriesFromDictionary:p];
    c = m;
    say("plug-in: %s", profile.UTF8String);
  }
  NSString *problem = edit ? nil : config_problem(c);
  if (problem) { say("not set up: %s", problem.UTF8String); return 1; }
  if (sound) {
    // checked.json says these states have played: Re-ARMed Mods offers the plug-in while it matches them
    NSString *stamp = [g_plugin_dir stringByAppendingPathComponent:@"checked.json"], *no_presets = nil;
    if ((problem = sound_problem(c, &no_presets))) {
      unlink(stamp.fileSystemRepresentation);
      say("not set up: %s", problem.UTF8String);
      return 1;
    }
    // And, where the profile has presets for the plug-in's engine, that they are the plug-in's own: "direct" needs that
    NSMutableDictionary *mark = [@{@"states": states_stamp(c[@"templates"])} mutableCopy];
    if (!no_presets) mark[@"presets"] = presets_stamp();
    if (no_presets.length) say("the presets can't go straight to the engine: %s", no_presets.UTF8String);
    else if (!no_presets) say(any_state(c[@"templates"]) ? "the presets are the plug-in's own: they can go straight to its engine" : "the presets play through the plug-in's engine");
    NSData *json = [NSJSONSerialization dataWithJSONObject:mark options:0 error:NULL];
    if (![json writeToFile:stamp atomically:YES]) { say("can't write %s", stamp.UTF8String); return 1; }
  }
  if (check) { say("set up: %lu templates", (unsigned long)[c[@"templates"] count]); return 0; }
  // The chosen interface, or with it unplugged the first of "setups" that is connected: every interface played before
  BOOL (^connected)(NSDictionary *) = ^BOOL(NSDictionary *s) {
    NSString *out = s[@"output"][@"device"];
    return find_device(s[@"input"][@"device"]) && (!out || find_device(out));
  };
  if (!connected(c))
    for (NSDictionary *s in c[@"setups"]) {
      if (!connected(s)) continue;
      NSMutableDictionary *m = [c mutableCopy];
      m[@"input"] = s[@"input"];
      m[@"output"] = s[@"output"];  // none: the system's output
      c = m;
      break;
    }
  say("interface: %s", [c[@"input"][@"device"] description].UTF8String);
  g_plugin = plugin_of(c);
  if (edit) g_plugin = (AudioComponentDescription){fourcc(@(argv[4])), fourcc(@(argv[5])), fourcc(@(argv[6])), 0, 0};
  g_channel = [c[@"input"][@"channel"] intValue];
  if (g_channel < 0) g_channel = 0;  // the audio thread counts buffers up to it
  g_input_gain = powf(10.f, [c[@"input"][@"gain_db"] floatValue] / 20.f);
  if (c[@"fade_ms"]) g_fade_frames = (uint32_t)([c[@"fade_ms"] doubleValue] * RATE / 1000);
  UInt32 frames = c[@"buffer"] ? [c[@"buffer"] unsignedIntValue] : 128;
  g_target_fill = frames;
  g_templates = c[@"templates"];
  g_map = c[@"map"];
  g_config = c;
  // Not the whole file: a new target_db leaves what was measured as it was. And what it was measured with: levels
  // from the low pluck that came before the phrase are no longer levels
  NSData *measured = [NSJSONSerialization dataWithJSONObject:@[c[@"templates"] ?: @{}, [c[@"map"] ?: @{} objectForKey:@"follow"] ?: @[],
                                                               [c[@"map"] ?: @{} objectForKey:@"gain_knobs"] ?: @[], c[@"probe_peak_db"] ?: @0, @"phrase"]
                                                     options:NSJSONWritingSortedKeys error:NULL];
  unsigned char hash[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(measured.bytes, (CC_LONG)measured.length, hash);
  g_config_stamp = [[NSData dataWithBytes:hash length:sizeof hash] base64EncodedStringWithOptions:0];
  if (c[@"target_db"]) g_target_db = [c[@"target_db"] floatValue];
  if (c[@"early_ms"]) g_early = [c[@"early_ms"] doubleValue] / 1000;
  if (c[@"probe_peak_db"]) g_probe_peak = powf(10.f, [c[@"probe_peak_db"] floatValue] / 20.f);

  // First of all: the instances take seconds to open, and a write before the FIFO exists would make an ordinary file
  int fd = -1;
  if (!edit) {  // setting a sound takes no commands, and the FIFO is a playing host's
    const char *fifo = [c[@"fifo"] UTF8String];
    unlink(fifo);
    if (mkfifo(fifo, 0600)) { say("no command FIFO: %s", strerror(errno)); return 1; }
    fd = open(fifo, O_RDONLY | O_NONBLOCK);
    int keep = open(fifo, O_WRONLY);  // a writer of our own: without one a read ends when the mod's goes away
    if (fd < 0 || keep < 0) { say("no command FIFO"); return 1; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
  }

  AudioObjectID in_dev = find_device(c[@"input"][@"device"]);
  AudioObjectID out_dev = c[@"output"][@"device"] ? find_device(c[@"output"][@"device"]) : default_output();
  if (!in_dev || !out_dev) { say("device not found (input %u, output %u)", in_dev, out_dev); return 1; }
  set_rate(in_dev);
  usleep(200000);
  if (device_rate(in_dev) != RATE) { say("the input must run at 48 kHz (%g)", device_rate(in_dev)); return 1; }
  if (!(g_shift = sp_new(RATE, NULL))) { say("no memory"); return 1; }
  mach_timebase_info(&g_clock);
  if (!edit && [c[@"keep_guitar_s"] doubleValue] > 0) {
    double seconds = MIN([c[@"keep_guitar_s"] doubleValue], 600);
    for (g_kept_len = 1 << 16; g_kept_len < seconds * RATE; g_kept_len <<= 1) ;
    g_kept_path = [g_dir stringByAppendingPathComponent:@"guitar.wav"];
    if ((g_kept = calloc(g_kept_len, sizeof *g_kept))) say("keeping the last %.0f s of guitar for guitar.wav", g_kept_len / RATE);
  }
  if (c[@"slots"]) g_slots = MAX(2, MIN(SLOTS, [c[@"slots"] intValue]));
  if (edit) g_slots = 1;
  for (int i = 0; i < g_slots; i++)
    if (make_slot(&slots[i])) { say("the plug-in didn't open"); return 1; }
  say("%d instances open", g_slots);
  BOOL states = any_state(c[@"templates"]);
  if (([c[@"direct"] boolValue] || !states) && !edit) {  // before any audio runs: finding a slot's instance in the engine renders it here
    NSString *why = direct_problem(c);
    g_direct = !why;
    if (why && !states) { say("not set up: %s", why.UTF8String); return 1; }
    if (why) say("direct is off, tones go by their saved states: %s", why.UTF8String);
    else say("direct: a tone's preset goes straight to the plug-in's engine");
  }
  if (edit) {  // the sound saved before, if there is one, playing from the start
    if ([NSFileManager.defaultManager fileExistsAtPath:@(argv[7])]) load_state(0, argv[7]);
    else atomic_store(&slots[0].ready, 1);
    atomic_store(&g_target, 0);
    g_playing = 0;
  }
  set_buffer(in_dev, frames);

  NSDictionary *gt = c[@"gate"];
  if (gt) {
    g_gate_on = 1;
    g_gate_attack = 1.f - expf(-1.f / (0.0005f * (float)RATE));
    if (gt[@"margin_db"]) g_gate_margin = [gt[@"margin_db"] floatValue];
    if (gt[@"tight_db"]) g_gate_tight_db = [gt[@"tight_db"] floatValue];
    if (gt[@"min_db"]) g_gate_min = [gt[@"min_db"] floatValue];
    if (gt[@"max_db"]) g_gate_max = [gt[@"max_db"] floatValue];
    if (gt[@"hold_ms"]) { g_hold_loose = [gt[@"hold_ms"][0] floatValue]; g_hold_tight = [gt[@"hold_ms"][1] floatValue]; }
    if (gt[@"release_ms"]) { g_release_loose = [gt[@"release_ms"][0] floatValue]; g_release_tight = [gt[@"release_ms"][1] floatValue]; }
    say("gate on: %.0f dB above the guitar's noise", g_gate_margin);
  }
  AudioDeviceIOProcID in_id = NULL;
  BOOL duplex = in_dev == out_dev;
  if (duplex) { g_proc = duplex_proc; g_target_fill = 0; }
  else {
    if (AudioDeviceCreateIOProcID(in_dev, input_proc, NULL, &in_id)) { say("no input"); return 1; }
    AudioDeviceStart(in_dev, in_id);
  }
  g_frames = frames;
  start_output(out_dev);
  if (!c[@"output"][@"device"]) follow_output();
  UInt32 total = device_frames(in_dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput) + device_frames(in_dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput) +
                 device_frames(out_dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput) + device_frames(out_dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput) +
                 2 * frames + g_target_fill;
  say("%s, about %.1f ms in to out as the devices report it", duplex ? "one device" : "two devices", total / RATE * 1000);
  say("running");

  dispatch_source_t meter = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
  dispatch_source_set_timer(meter, dispatch_time(DISPATCH_TIME_NOW, 0), 5 * NSEC_PER_SEC, NSEC_PER_SEC);
  dispatch_source_set_event_handler(meter, ^{
    uint32_t a = atomic_exchange(&g_peak_in, 0), b = atomic_exchange(&g_peak_out, 0);
    float i, o;
    memcpy(&i, &a, 4);
    memcpy(&o, &b, 4);
    double tick = (double)g_clock.numer / g_clock.denom / 1e6;  // ms
    say("peak in %.1f dB, out %.1f dB, floor %.1f dB, gate %.1f dB, late %u, empty %u, a buffer took %.2f ms at most (shifter %.2f), %u long, %u overloads",
        20 * log10f(i + 1e-9f), 20 * log10f(o + 1e-9f), g_floor.db, atomic_load(&g_open_db), atomic_load(&g_late_blocks), atomic_load(&g_empty_blocks),
        atomic_exchange(&g_busy_most, 0) * tick, atomic_exchange(&g_shift_most, 0) * tick, atomic_load(&g_busy_long), atomic_load(&g_overloads));
  });
  dispatch_resume(meter);
  dispatch_source_t volume = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
  dispatch_source_set_timer(volume, dispatch_time(DISPATCH_TIME_NOW, 0), NSEC_PER_SEC / 10, NSEC_PER_SEC / 50);
  // A device has one buffer size and the last to set it wins: the game sets 128 when it opens its input, and with its
  // queue sized by then it takes ours, so ours is set again
  BOOL follow_volume = ![c[@"ignore_system_volume"] boolValue], quit_with_game = ![c[@"stay_after_game"] boolValue];
  __block int ticks = 0;
  dispatch_source_set_event_handler(volume, ^{
    if (follow_volume) atomic_store(&g_system_gain, system_gain());
    if (++ticks % 10) return;
    // The mod writes once a second: a silent mod means the game has gone, crashed or not
    uint64_t heard = atomic_load(&g_last_heard);
    if (quit_with_game && heard && (uint64_t)time(NULL) > heard + 5) {
      say("the game has gone: quitting");
      AudioDeviceStop(g_out_dev, g_out_id);
      keep_guitar();
      _exit(0);
    }
    if (device_frames(g_out_dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal) != frames) {
      say("the buffer changed: setting it back");
      set_buffer(g_out_dev, frames);
    }
  });
  dispatch_resume(volume);
  if (edit) return edit_sound(@(argv[7]));
  static char line[16384];
  size_t have = 0;
  while (!atomic_load(&g_quit)) {
    ssize_t got = read(fd, line + have, sizeof line - 1 - have);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) { say("command FIFO: %s", got ? strerror(errno) : "closed"); break; }
    have += (size_t)got;
    atomic_store(&g_last_heard, (uint64_t)time(NULL));
    char *start = line, *end;
    while ((end = memchr(start, '\n', line + have - start))) {
      *end = 0;
      @autoreleasepool { command(start); }
      start = end + 1;
    }
    have -= (size_t)(start - line);
    memmove(line, start, have);
    if (have == sizeof line - 1) have = 0;  // no line is this long
  }
  AudioDeviceStop(g_out_dev, g_out_id);
  if (in_id) AudioDeviceStop(in_dev, in_id);
  keep_guitar();
  say("late blocks %u, empty blocks %u", atomic_load(&g_late_blocks), atomic_load(&g_empty_blocks));
  return 0;
}}
