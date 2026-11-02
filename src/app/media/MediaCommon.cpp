#include "Media.h"

namespace tp::media
{
bool isSupported()                       { return backend::isSupported(); }
juce::StringArray videoExtensions()      { return backend::videoExtensions(); }
juce::StringArray extraAudioExtensions() { return backend::audioExtensions(); }

bool isVideoFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().trimCharactersAtStart (".").toLowerCase();
    return ext.isNotEmpty() && backend::isSupported() && backend::videoExtensions().contains (ext);
}

bool probe (const juce::File& f, StreamInfo& info)            { return backend::isSupported() && backend::probe (f, info); }
std::unique_ptr<AudioDecoder> openAudio (const juce::File& f) { return backend::isSupported() ? backend::openAudio (f) : nullptr; }
std::unique_ptr<VideoDecoder> openVideo (const juce::File& f) { return backend::isSupported() ? backend::openVideo (f) : nullptr; }

namespace
{
/** Random-access reader on top of a sequential decoder: keeps the current chunk and seeks only
    when the request isn't just after it (the transport reads ahead sequentially). */
class MediaAudioReader final : public juce::AudioFormatReader
{
public:
    MediaAudioReader (juce::InputStream* in, std::unique_ptr<AudioDecoder> d)
        : AudioFormatReader (in, "Media"), dec (std::move (d))
    {
        sourceChannels = juce::jmax (1, dec->channels());
        sampleRate = dec->sampleRate();
        numChannels = sourceChannels == 1 ? 1u : 2u;
        bitsPerSample = 32;
        usesFloatingPointData = true;
        lengthInSamples = (juce::int64) std::llround (dec->duration() * sampleRate);
    }

    bool readSamples (int* const* dest, int numDest, int destOffset, juce::int64 start, int num) override
    {
        int done = 0;
        while (done < num)
        {
            const juce::int64 pos = start + done;
            if (pos >= chunkStart && pos < chunkStart + chunkFrames)
            {
                const int offset = (int) (pos - chunkStart);
                const int n = juce::jmin (num - done, chunkFrames - offset);
                copyOut (dest, numDest, destOffset + done, offset, n);
                done += n;
                continue;
            }

            if (! fetch (pos))
            {
                zero (dest, numDest, destOffset + done, num - done);   // past the end
                break;
            }
            if (pos < chunkStart)
            {
                // the decoder landed after the requested sample (stream starts late): fill the gap
                const int n = (int) juce::jmin<juce::int64> (num - done, chunkStart - pos);
                zero (dest, numDest, destOffset + done, n);
                done += n;
            }
        }
        return true;
    }

private:
    bool decodeNext()
    {
        double pts = 0.0;
        if (eof || ! dec->read (buffer, pts))
        {
            eof = true;
            return false;
        }
        const int frames = (int) (buffer.size() / (size_t) sourceChannels);
        const auto ptsSample = (juce::int64) std::llround (pts * sampleRate);
        // stay contiguous unless the stream really jumps
        chunkStart = (afterSeek || std::abs (ptsSample - (chunkStart + chunkFrames)) > (juce::int64) (sampleRate * 0.25))
                         ? ptsSample : chunkStart + chunkFrames;
        chunkFrames = frames;
        afterSeek = false;
        return true;
    }

    bool fetch (juce::int64 pos)
    {
        const juce::int64 end = chunkStart + chunkFrames;
        const bool sequential = pos >= end && pos < end + (juce::int64) (sampleRate * 0.5) && chunkFrames > 0;
        if (! sequential)
        {
            seekTo (pos / sampleRate);
            if (! decodeNext()) return false;
            if (chunkStart > pos && pos > 0)
            {
                // seek landed too late: go further back once
                seekTo (juce::jmax (0.0, pos / sampleRate - 1.0));
                if (! decodeNext()) return false;
            }
        }
        else if (! decodeNext())
            return false;

        // decode forward until the chunk covers pos
        for (int guard = 0; chunkStart + chunkFrames <= pos && guard < 100000; ++guard)
            if (! decodeNext()) return false;
        return true;
    }

    void seekTo (double seconds)
    {
        dec->seek (juce::jmax (0.0, seconds));
        eof = false;
        afterSeek = true;
        chunkFrames = 0;
    }

    void copyOut (int* const* dest, int numDest, int destStart, int srcFrame, int n)
    {
        const float* src = buffer.data() + (size_t) srcFrame * (size_t) sourceChannels;
        float* l = numDest > 0 && dest[0] != nullptr ? reinterpret_cast<float*> (dest[0]) + destStart : nullptr;
        float* r = numDest > 1 && dest[1] != nullptr ? reinterpret_cast<float*> (dest[1]) + destStart : nullptr;
        const int c = sourceChannels;

        for (int i = 0; i < n; ++i, src += c)
        {
            float a, b;
            if (c == 1)      { a = b = src[0]; }
            else if (c == 2) { a = src[0]; b = src[1]; }
            else
            {
                // WAVE order: FL FR FC LFE BL BR SL SR
                const float centre = c > 2 ? src[2] : 0.0f;
                const float sl = (c > 4 ? src[4] : 0.0f) + (c > 6 ? src[6] : 0.0f);
                const float sr = (c > 5 ? src[5] : 0.0f) + (c > 7 ? src[7] : 0.0f);
                a = 0.6f * (src[0] + 0.707f * centre + 0.707f * sl);
                b = 0.6f * (src[1] + 0.707f * centre + 0.707f * sr);
            }
            if (l != nullptr) l[i] = a;
            if (r != nullptr) r[i] = b;
        }
        for (int ch = 2; ch < numDest; ++ch)
            if (dest[ch] != nullptr) juce::FloatVectorOperations::clear (reinterpret_cast<float*> (dest[ch]) + destStart, n);
    }

    static void zero (int* const* dest, int numDest, int destStart, int n)
    {
        for (int ch = 0; ch < numDest; ++ch)
            if (dest[ch] != nullptr) juce::FloatVectorOperations::clear (reinterpret_cast<float*> (dest[ch]) + destStart, n);
    }

    std::unique_ptr<AudioDecoder> dec;
    std::vector<float> buffer;
    int sourceChannels = 2;
    juce::int64 chunkStart = 0;
    int chunkFrames = 0;
    bool eof = false, afterSeek = true;
};

/** For videos without sound: the engine still needs a clock to play against. */
class SilentReader final : public juce::AudioFormatReader
{
public:
    SilentReader (juce::InputStream* in, double seconds) : AudioFormatReader (in, "Media")
    {
        sampleRate = 48000.0;
        numChannels = 2;
        bitsPerSample = 32;
        usesFloatingPointData = true;
        lengthInSamples = (juce::int64) std::llround (seconds * sampleRate);
    }

    bool readSamples (int* const* dest, int numDest, int destOffset, juce::int64, int num) override
    {
        for (int ch = 0; ch < numDest; ++ch)
            if (dest[ch] != nullptr) juce::FloatVectorOperations::clear (reinterpret_cast<float*> (dest[ch]) + destOffset, num);
        return true;
    }
};

juce::StringArray dotted (const juce::StringArray& exts)
{
    juce::StringArray r;
    for (auto& e : exts) r.add ("." + e);
    return r;
}
} // namespace

MediaAudioFormat::MediaAudioFormat()
    : AudioFormat ("Media", [] { auto e = dotted (backend::videoExtensions()); e.addArray (dotted (backend::audioExtensions())); return e; }())
{
}

juce::AudioFormatReader* MediaAudioFormat::createReaderFor (juce::InputStream* in, bool deleteStreamIfOpeningFails)
{
    auto fail = [&]() -> juce::AudioFormatReader*
    {
        if (deleteStreamIfOpeningFails) delete in;
        return nullptr;
    };

    auto* fileStream = dynamic_cast<juce::FileInputStream*> (in);
    if (fileStream == nullptr || ! backend::isSupported())
        return fail();

    const auto file = fileStream->getFile();
    StreamInfo info;
    if (! backend::probe (file, info) || (! info.hasAudio && ! info.hasVideo))
        return fail();

    if (info.hasAudio)
        if (auto dec = backend::openAudio (file); dec != nullptr && dec->sampleRate() > 0.0)
            return new MediaAudioReader (in, std::move (dec));

    if (info.hasVideo && info.duration > 0.0)
        return new SilentReader (in, info.duration);

    return fail();
}
} // namespace tp::media
