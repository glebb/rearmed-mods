// RocksmithAudioBridge's drop pedal shifter (DLL/DropPedal/DelayLinePitchShifter.cpp, MIT, Cheesewizard) behind three C
// functions, for the bench to compare against. Its source is not in this repository: run.sh compiles it from a checkout
// given as RAB_DIR, and without one the bench runs without this baseline.
#include "DelayLinePitchShifter.hpp"

extern "C" void *rab_new(int semitones) {
  auto *shifter = new Audio::DelayLinePitchShifter(semitones);
  Audio::CaptureFormat format;
  format.sampleFormat = Audio::SampleFormat::Float32;
  format.sampleRate = 48000;
  format.channelCount = 1;
  shifter->Prepare(format);
  return shifter;
}

extern "C" void rab_process(void *state, float *samples, unsigned frames) {
  static_cast<Audio::DelayLinePitchShifter *>(state)->Process(samples, frames);
}

extern "C" double rab_delay(void *state) { return static_cast<Audio::DelayLinePitchShifter *>(state)->GetLiveDelayFrames(); }

extern "C" void rab_free(void *state) { delete static_cast<Audio::DelayLinePitchShifter *>(state); }
