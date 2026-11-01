#pragma once

#include <juce_dsp/juce_dsp.h>
#include "AnalysisFrame.h"
#include "LoudnessMeter.h"

namespace tp
{
/** Turns the raw stereo stream into an AnalysisFrame. Runs on the UI thread, fed from a
    lock-free FIFO, so nothing here ever touches the audio callback. */
class Analyzer
{
public:
    Analyzer();

    void prepare (double sampleRate);
    void resetTrack();   // clears loudness and peak memory (new track)

    void push (const float* left, const float* right, int numSamples);
    void computeFrame (AnalysisFrame& frame, float dtSeconds);

private:
    double sampleRate = 44100.0;

    std::vector<float> historyL, historyR;   // circular, kFftSize samples
    int historyPos = 0;

    juce::dsp::FFT fft { kFftOrder };
    std::vector<float> fftData, window;
    std::array<float, kNumBins> spectrumSmooth;

    double meanSquare[2] {};
    float  vuCoeff = 0.0f;
    float  tickPeak[2] {};
    float  maxPeak = 0.0f;
    int    samplesSinceFrame = 0;
    float  lastPeakDb[2] { kSilenceDb, kSilenceDb };

    std::array<float, kPolarBins> polarTick {}, polarHist {}, polarPeak {};

    LoudnessMeter loudness;
    std::uint64_t frameCounter = 0;
};
} // namespace tp
