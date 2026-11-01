#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace tp
{
/** Single-producer / single-consumer stereo sample FIFO.
    The audio callback writes, the UI thread reads. No locks, no allocation after construction. */
class StereoFifo
{
public:
    explicit StereoFifo (int capacity = 1 << 16) : fifo (capacity), buffer (2, capacity) {}

    void push (const float* left, const float* right, int numSamples) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (numSamples, s1, n1, s2, n2);

        if (n1 > 0)
        {
            juce::FloatVectorOperations::copy (buffer.getWritePointer (0, s1), left, n1);
            juce::FloatVectorOperations::copy (buffer.getWritePointer (1, s1), right, n1);
        }
        if (n2 > 0)
        {
            juce::FloatVectorOperations::copy (buffer.getWritePointer (0, s2), left + n1, n2);
            juce::FloatVectorOperations::copy (buffer.getWritePointer (1, s2), right + n1, n2);
        }
        fifo.finishedWrite (n1 + n2);
    }

    int pop (float* left, float* right, int maxSamples) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxSamples, s1, n1, s2, n2);

        if (n1 > 0)
        {
            juce::FloatVectorOperations::copy (left, buffer.getReadPointer (0, s1), n1);
            juce::FloatVectorOperations::copy (right, buffer.getReadPointer (1, s1), n1);
        }
        if (n2 > 0)
        {
            juce::FloatVectorOperations::copy (left + n1, buffer.getReadPointer (0, s2), n2);
            juce::FloatVectorOperations::copy (right + n1, buffer.getReadPointer (1, s2), n2);
        }
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    int available() const noexcept { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> buffer;
};
} // namespace tp
