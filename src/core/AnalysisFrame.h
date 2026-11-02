#pragma once

#include <array>
#include <cstdint>

namespace tp
{
constexpr int kFftOrder   = 12;
constexpr int kFftSize    = 1 << kFftOrder;
constexpr int kNumBins    = kFftSize / 2;
constexpr int kScopeSize  = 2048;
constexpr int kPolarBins  = 73;
constexpr float kSilenceDb = -120.0f;

/** One snapshot of everything the visualizers may draw. Produced once per display frame
    by the Analyzer on the UI thread; plugins only ever read from it. */
struct AnalysisFrame
{
    double sampleRate = 44100.0;
    bool   receivedAudio = false;          // true if new samples arrived since the last frame

    std::array<float, kScopeSize> left {}, right {};   // most recent samples, oldest first
    std::array<float, kNumBins> spectrumDb {};          // mono (L+R)/2, dBFS per bin, smoothed

    float peakDb[2] { kSilenceDb, kSilenceDb };          // sample peak since last frame
    float rmsDb[2]  { kSilenceDb, kSilenceDb };          // 300 ms RMS
    float vu[2]     { -40.0f, -40.0f };                  // VU, 0 VU = -10 dBFS sine (mastered-music calibration)
    float maxPeakDb = kSilenceDb;                        // highest sample peak this track

    float momentaryLufs  = kSilenceDb;
    float shortTermLufs  = kSilenceDb;
    float integratedLufs = kSilenceDb;

    float correlation = 0.0f;    // -1 .. +1
    float balanceDb   = 0.0f;    // L minus R

    std::array<float, kPolarBins> polar {};      // 0..1 radius per angle bin (-90..+90 deg)
    std::array<float, kPolarBins> polarPeak {};  // slower-decaying outline

    // musical analysis
    std::array<float, 12> chroma {};   // C..B, 0..1
    int   key = -1;                     // 0-11 major, 12-23 minor, -1 unknown
    float keyConfidence = 0.0f;
    float bpm = 0.0f, bpmConfidence = 0.0f;
    float beatPhase = 0.0f;             // 0 at a beat
    float onset = 0.0f;                 // 0..1 onset strength right now

    // true peak & clipping (since track start)
    float truePeakDb = kSilenceDb;      // this frame
    float maxTruePeakDb = kSilenceDb;
    int   clippedSamples = 0, intersampleOvers = 0;

    // stereo width per band: sub, low, mid, high, air. 0 = mono, 0.5 = wide, 1 = out of phase
    static constexpr int kWidthBands = 5;
    std::array<float, kWidthBands> bandWidth {};

    // transport (filled in by the player, not the analyzer)
    double positionSeconds = 0.0, lengthSeconds = 0.0;
    bool playing = false;

    std::uint64_t frameIndex = 0;

    float binToHz (int bin) const noexcept { return (float) (bin * sampleRate / kFftSize); }
};
} // namespace tp
