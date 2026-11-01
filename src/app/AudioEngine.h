#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "core/StereoFifo.h"

namespace tp
{
/** Monitoring switches applied after decoding, before the analysis tap. */
struct MonitorFlags
{
    std::atomic<bool> mono { false }, swap { false }, soloLeft { false }, soloRight { false },
                      mid { false }, side { false }, dim { false }, bypass { false };
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    /** Opens the default output device. Returns an error string, empty on success. */
    juce::String openDevice();
    /** For headless rendering (tests / snapshots): no device, call renderOffline() instead. */
    void prepareOffline (double sampleRate, int blockSize);
    void renderOffline (int numSamples);

    bool load (const juce::File& file);
    void unload();

    void play();
    void pause();
    void togglePlay()           { isPlaying() ? pause() : play(); }
    bool isPlaying() const      { return transport.isPlaying(); }
    bool hasFinished() const    { return transport.hasStreamFinished(); }

    double getLength() const    { return transport.getLengthInSeconds(); }
    void   setPosition (double seconds);
    /** Position interpolated between audio callbacks so the playhead moves at display rate. */
    double getSmoothPosition() const;

    void  setVolume (float zeroToOne);
    float getVolume() const     { return volume.load(); }

    double getSampleRate() const { return currentSampleRate.load(); }
    StereoFifo& analysisFifo()   { return fifo; }
    MonitorFlags& monitor()      { return flags; }

    juce::AudioFormatManager& formats() { return formatManager; }
    juce::AudioDeviceManager& devices() { return deviceManager; }
    juce::AudioThumbnail& thumbnail()   { return thumb; }

    struct FileDetails
    {
        juce::String formatName;
        double sampleRate = 0.0, lengthSeconds = 0.0;
        int bitsPerSample = 0, numChannels = 0;
        juce::int64 fileSize = 0;
        int kbps = 0;
    };
    const FileDetails& details() const { return fileDetails; }
    juce::File currentFile() const     { return loadedFile; }

    std::function<void (double newSampleRate)> onSampleRateChanged;

private:
    class Processor;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread readAheadThread { "Transport read-ahead" };
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<Processor> processor;
    juce::AudioSourcePlayer player;

    juce::AudioThumbnailCache thumbCache { 8 };
    juce::AudioThumbnail thumb { 256, formatManager, thumbCache };

    StereoFifo fifo;
    MonitorFlags flags;
    std::atomic<float> volume { 0.8f };
    std::atomic<double> currentSampleRate { 44100.0 };

    std::atomic<double> lastBlockPosition { 0.0 }, lastBlockTime { 0.0 };

    bool offline = false;
    juce::AudioBuffer<float> offlineBuffer;
    FileDetails fileDetails;
    juce::File loadedFile;

    friend class Processor;
};
} // namespace tp
