#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

/** Video and container support through the platform's own decoders:
    Media Foundation on Windows, AVFoundation on macOS, FFmpeg (system libraries) on Linux.
    The sound of a video plays through the normal audio engine, so waveform, meters and
    effects work exactly as for music files. */
namespace tp::media
{
struct StreamInfo
{
    double duration = 0.0;
    bool hasVideo = false, hasAudio = false;
    int width = 0, height = 0;      // display size of the decoded picture (pixel aspect applied, before rotation)
    int rotation = 0;               // clockwise degrees to show the picture upright (0, 90, 180, 270)
    double fps = 0.0;
    juce::String videoCodec, audioCodec;
    double sampleRate = 0.0;
    int channels = 0;
};

/** A decoded picture: BGRA bytes, top row first, alpha 255. */
struct FrameBuffer
{
    std::vector<juce::uint8> pixels;
    int width = 0, height = 0, stride = 0;

    void allocate (int w, int h)
    {
        width = w; height = h; stride = w * 4;
        pixels.resize ((size_t) stride * (size_t) h);
    }
};

/** Sequential decoders, one per platform. An instance is used by one thread at a time. */
class AudioDecoder
{
public:
    virtual ~AudioDecoder() = default;
    virtual double sampleRate() const = 0;
    virtual int channels() const = 0;                  // decoded channel count (any; downmixed to stereo later)
    virtual double duration() const = 0;
    /** Positions so the next chunk starts at or slightly before `seconds`. */
    virtual bool seek (double seconds) = 0;
    /** Next chunk of interleaved float samples; pts = time of its first frame. False at the end. */
    virtual bool read (std::vector<float>& interleaved, double& pts) = 0;
};

class VideoDecoder
{
public:
    virtual ~VideoDecoder() = default;
    /** Accurate seek: the next read() returns the picture that is on screen at `seconds`. */
    virtual bool seek (double seconds) = 0;
    /** Decodes the next picture into `into` (reusing its memory). False at the end. */
    virtual bool read (FrameBuffer& into, double& pts) = 0;
};

/** True where a platform decoder is available. */
bool isSupported();
/** Lower-case extensions without dots. */
juce::StringArray videoExtensions();
/** Audio-only formats the platform decoder adds to JUCE's own (e.g. m4a on Windows). */
juce::StringArray extraAudioExtensions();
bool isVideoFile (const juce::File&);

bool probe (const juce::File&, StreamInfo&);
std::unique_ptr<AudioDecoder> openAudio (const juce::File&);
std::unique_ptr<VideoDecoder> openVideo (const juce::File&);

/** Lets juce::AudioFormatManager read the sound of media files (and silence for videos without sound). */
class MediaAudioFormat final : public juce::AudioFormat
{
public:
    MediaAudioFormat();
    juce::Array<int> getPossibleSampleRates() override { return {}; }
    juce::Array<int> getPossibleBitDepths() override   { return {}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override   { return true; }
    bool isCompressed() override { return true; }
    juce::AudioFormatReader* createReaderFor (juce::InputStream*, bool deleteStreamIfOpeningFails) override;
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor (std::unique_ptr<juce::OutputStream>&,
                                                              const juce::AudioFormatWriterOptions&) override { return nullptr; }
};

// ---- implemented by each platform backend
namespace backend
{
bool isSupported();
juce::StringArray videoExtensions();
juce::StringArray audioExtensions();
bool probe (const juce::File&, StreamInfo&);
std::unique_ptr<AudioDecoder> openAudio (const juce::File&);
std::unique_ptr<VideoDecoder> openVideo (const juce::File&);
}
} // namespace tp::media
