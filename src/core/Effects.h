#pragma once

#include <array>
#include <atomic>
#include <vector>

namespace tp
{
/** An audio effect in the slot chain. process() runs on the audio thread: no locks, no allocation.
    Parameters are atomics written by the UI and picked up (smoothed) at the next block. */
class EffectProcessor
{
public:
    virtual ~EffectProcessor() = default;
    virtual void prepare (double sampleRate) = 0;
    virtual void process (float* left, float* right, int numSamples) noexcept = 0;

    std::atomic<bool> bypass { false };
};

/** RBJ cookbook biquad, stereo. */
struct Biquad
{
    enum class Type { lowShelf, peak, highShelf, lowCut, highCut };

    void set (Type type, double sampleRate, double freq, double q, double gainDb) noexcept;
    float process (int channel, float x) noexcept
    {
        const double y = b0 * x + z1[channel];
        z1[channel] = b1 * x - a1 * y + z2[channel];
        z2[channel] = b2 * x - a2 * y;
        return (float) y;
    }
    void clear() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }
    double magnitudeDb (double freq, double sampleRate) const noexcept;

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};
};

//==============================================================================
class Equalizer final : public EffectProcessor
{
public:
    static constexpr int kBands = 5;

    struct Band
    {
        Biquad::Type type = Biquad::Type::peak;
        std::atomic<float> freq { 1000.0f }, gainDb { 0.0f }, q { 0.9f };
        std::atomic<bool> enabled { true };
    };

    Equalizer();
    void prepare (double sampleRate) override;
    void process (float* l, float* r, int n) noexcept override;

    /** Total response from the current parameter targets (for drawing the curve). */
    float responseDb (float hz) const;
    void resetBands();

    std::array<Band, kBands> bands;

private:
    double sampleRate = 44100.0;
    Biquad filters[kBands];
    float current[kBands][3] {};   // smoothed freq, gain, q
};

//==============================================================================
class Compressor final : public EffectProcessor
{
public:
    void prepare (double sampleRate) override;
    void process (float* l, float* r, int n) noexcept override;

    /** Static gain computer with a soft knee: output level for an input level (dB). */
    static float curve (float inDb, float thresholdDb, float ratio, float kneeDb);

    std::atomic<float> thresholdDb { -18.0f }, ratio { 3.0f }, attackMs { 10.0f }, releaseMs { 150.0f },
                       makeupDb { 0.0f }, kneeDb { 6.0f };
    std::atomic<float> meterGainReductionDb { 0.0f }, meterInputDb { -120.0f };

private:
    double sampleRate = 44100.0;
    float envelopeDb = 0.0f;   // current gain reduction (positive dB)
    float makeup = 1.0f;
};

//==============================================================================
/** Look-ahead brickwall limiter with input drive: the "maximizer". */
class Limiter final : public EffectProcessor
{
public:
    void prepare (double sampleRate) override;
    void process (float* l, float* r, int n) noexcept override;

    std::atomic<float> driveDb { 0.0f }, ceilingDb { -1.0f }, releaseMs { 80.0f };
    std::atomic<float> meterGainReductionDb { 0.0f }, meterOutputDb { -120.0f };

private:
    static constexpr int kMaxLookahead = 512;
    double sampleRate = 44100.0;
    int lookahead = 64, writePos = 0;
    float delayL[kMaxLookahead] {}, delayR[kMaxLookahead] {}, needed[kMaxLookahead] {};
    float gain = 1.0f, drive = 1.0f;
};
} // namespace tp
