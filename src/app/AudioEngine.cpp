#include "AudioEngine.h"

namespace tp
{
namespace
{
double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
}

//==============================================================================
class AudioEngine::Processor final : public juce::AudioSource
{
public:
    explicit Processor (AudioEngine& e) : engine (e) {}

    void prepareToPlay (int samplesPerBlock, double sampleRate) override
    {
        engine.transport.prepareToPlay (samplesPerBlock, sampleRate);
        gain.reset (sampleRate, 0.05);
        gain.setCurrentAndTargetValue (targetGain());
        engine.currentSampleRate = sampleRate;

        juce::MessageManager::callAsync ([weak = juce::WeakReference<Processor> (this), sampleRate]
        {
            if (auto* p = weak.get())
                if (p->engine.onSampleRateChanged)
                    p->engine.onSampleRateChanged (sampleRate);
        });
    }

    void releaseResources() override { engine.transport.releaseResources(); }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        engine.transport.getNextAudioBlock (info);

        auto* buffer = info.buffer;
        const int n = info.numSamples;
        if (buffer->getNumChannels() == 0)
            return;

        float* l = buffer->getWritePointer (0, info.startSample);
        float* r = buffer->getNumChannels() > 1 ? buffer->getWritePointer (1, info.startSample) : l;

        auto& f = engine.flags;
        if (! f.bypass.load())
        {
            const bool mono = f.mono, swap = f.swap, soloL = f.soloLeft, soloR = f.soloRight, mid = f.mid, side = f.side;
            if (mono || swap || soloL || soloR || mid || side)
            {
                for (int i = 0; i < n; ++i)
                {
                    float a = l[i], b = r[i];
                    if (swap)  std::swap (a, b);
                    if (soloL) b = a;
                    if (soloR) a = b;
                    if (mid)  { const float m = 0.5f * (a + b); a = b = m; }
                    if (side) { const float s = 0.5f * (a - b); a = s; b = -s; }
                    if (mono) { const float m = 0.5f * (a + b); a = b = m; }
                    l[i] = a;
                    if (r != l) r[i] = b;
                }
            }
        }

        // Analysis tap: pre-volume so meters show the music, not the listening level
        engine.fifo.push (l, r, n);

        gain.setTargetValue (targetGain());
        if (gain.isSmoothing() || gain.getTargetValue() != 1.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float g = gain.getNextValue();
                l[i] *= g;
                if (r != l) r[i] *= g;
            }
        }

        engine.lastBlockPosition = engine.transport.getCurrentPosition();
        engine.lastBlockTime = nowSeconds();
    }

private:
    float targetGain() const
    {
        const float v = engine.volume.load();
        float g = v * v;   // perceptual-ish curve
        if (engine.flags.dim.load() && ! engine.flags.bypass.load())
            g *= 0.1f;
        return g;
    }

    AudioEngine& engine;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gain { 1.0f };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Processor)
};

//==============================================================================
AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats();
    processor = std::make_unique<Processor> (*this);
    readAheadThread.startThread (juce::Thread::Priority::high);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
    transport.setSource (nullptr);
    readAheadThread.stopThread (1000);
}

juce::String AudioEngine::openDevice()
{
    offline = false;
    auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
    player.setSource (processor.get());
    deviceManager.addAudioCallback (&player);
    return error;
}

void AudioEngine::prepareOffline (double sampleRate, int blockSize)
{
    offline = true;
    offlineBuffer.setSize (2, blockSize);
    processor->prepareToPlay (blockSize, sampleRate);
}

void AudioEngine::renderOffline (int numSamples)
{
    while (numSamples > 0)
    {
        const int n = juce::jmin (numSamples, offlineBuffer.getNumSamples());
        offlineBuffer.clear();
        processor->getNextAudioBlock (juce::AudioSourceChannelInfo (&offlineBuffer, 0, n));
        numSamples -= n;
    }
}

bool AudioEngine::load (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const bool wasPlaying = isPlaying();
    transport.stop();
    transport.setSource (nullptr);

    fileDetails = {};
    fileDetails.formatName    = file.getFileExtension().trimCharactersAtStart (".").toUpperCase();
    fileDetails.sampleRate    = reader->sampleRate;
    fileDetails.bitsPerSample = (int) reader->bitsPerSample;
    fileDetails.numChannels   = (int) reader->numChannels;
    fileDetails.lengthSeconds = reader->sampleRate > 0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
    fileDetails.fileSize      = file.getSize();
    if (fileDetails.lengthSeconds > 0.0)
        fileDetails.kbps = (int) std::lround ((double) fileDetails.fileSize * 8.0 / fileDetails.lengthSeconds / 1000.0);

    const double sr = reader->sampleRate;
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    transport.setSource (readerSource.get(),
                         offline ? 0 : 1 << 16,
                         offline ? nullptr : &readAheadThread,
                         sr, 2);

    thumb.setSource (new juce::FileInputSource (file));
    loadedFile = file;

    lastBlockPosition = 0.0;
    lastBlockTime = nowSeconds();

    if (wasPlaying)
        play();
    return true;
}

void AudioEngine::unload()
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    thumb.clear();
    loadedFile = juce::File();
    fileDetails = {};
}

void AudioEngine::play()
{
    if (readerSource == nullptr)
        return;
    if (transport.hasStreamFinished())
        setPosition (0.0);
    lastBlockTime = nowSeconds();
    transport.start();
}

void AudioEngine::pause()
{
    transport.stop();
    lastBlockPosition = transport.getCurrentPosition();
}

void AudioEngine::setPosition (double seconds)
{
    seconds = juce::jlimit (0.0, juce::jmax (0.0, getLength()), seconds);
    transport.setPosition (seconds);
    lastBlockPosition = seconds;
    lastBlockTime = nowSeconds();
}

double AudioEngine::getSmoothPosition() const
{
    if (! transport.isPlaying())
        return transport.getCurrentPosition();

    const double elapsed = juce::jlimit (0.0, 0.1, nowSeconds() - lastBlockTime.load());
    return juce::jmin (getLength(), lastBlockPosition.load() + elapsed);
}

void AudioEngine::setVolume (float v)
{
    volume = juce::jlimit (0.0f, 1.0f, v);
}
} // namespace tp
