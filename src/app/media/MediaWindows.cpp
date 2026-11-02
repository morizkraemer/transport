// Windows: decoding through Media Foundation (part of Windows, nothing to ship).
#include "Media.h"
#include <juce_events/juce_events.h>

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace tp::media::backend
{
namespace
{
/** COM + Media Foundation for whichever thread is decoding (JUCE's read-ahead, thumbnail and video threads). */
void ensureInitialised()
{
    // the message thread must stay single-threaded (OLE drag and drop); decoder threads join the MTA
    thread_local const bool com = []
    {
        const bool ui = juce::MessageManager::existsAndIsCurrentThread();
        const HRESULT hr = CoInitializeEx (nullptr, ui ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
        return SUCCEEDED (hr) || hr == RPC_E_CHANGED_MODE;
    }();
    static const bool mf = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE));
    juce::ignoreUnused (com, mf);
}

constexpr double kHns = 10000000.0;   // Media Foundation time unit: 100 ns

juce::String fourcc (DWORD v)
{
    juce::String s;
    for (int i = 0; i < 4; ++i)
    {
        const char c = (char) ((v >> (8 * i)) & 0xff);
        if (c >= 32 && c < 127) s += c;
    }
    return s.trim();
}

juce::String videoCodecName (const GUID& sub)
{
    switch (sub.Data1)
    {
        case FCC ('H264'): case FCC ('h264'): case FCC ('avc1'): return "H.264";
        case FCC ('HEVC'): case FCC ('H265'): case FCC ('hvc1'): case FCC ('hev1'): return "HEVC";
        case FCC ('VP90'): return "VP9";
        case FCC ('VP80'): return "VP8";
        case FCC ('AV01'): return "AV1";
        case FCC ('MP4V'): case FCC ('mp4v'): case FCC ('M4S2'): return "MPEG-4";
        case FCC ('MP43'): return "MPEG-4 v3";
        case FCC ('WMV3'): return "WMV 9";
        case FCC ('WVC1'): return "VC-1";
        case FCC ('MJPG'): return "MJPEG";
        default: break;
    }
    if (sub == MFVideoFormat_MPEG2) return "MPEG-2";
    return fourcc (sub.Data1);
}

juce::String audioCodecName (const GUID& sub)
{
    switch (sub.Data1)
    {
        case 0x1610: case 0x1600: case 0xA106: return "AAC";
        case 0x0055: return "MP3";
        case 0x0050: return "MPEG audio";
        case 0x0001: case 0x0003: return "PCM";
        case 0x0160: case 0x0161: case 0x0162: case 0x0163: return "WMA";
        case 0xF1AC: return "FLAC";
        case 0x704F: return "Opus";
        case 0x6C61: return "ALAC";
        case 0x2000: return "AC-3";
        default: break;
    }
    if (sub == MFAudioFormat_Dolby_AC3)       return "AC-3";
    if (sub == MFAudioFormat_Dolby_DDPlus)    return "E-AC-3";
    return {};
}

ComPtr<IMFSourceReader> openReader (const juce::File& file, bool videoProcessing)
{
    ensureInitialised();
    ComPtr<IMFAttributes> attrs;
    if (FAILED (MFCreateAttributes (&attrs, 2))) return {};
    if (videoProcessing)
        attrs->SetUINT32 (MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // YUV -> RGB32 in the reader

    ComPtr<IMFSourceReader> reader;
    if (FAILED (MFCreateSourceReaderFromURL (file.getFullPathName().toWideCharPointer(), attrs.Get(), &reader)))
        return {};
    return reader;
}

double durationOf (IMFSourceReader* reader)
{
    PROPVARIANT var;
    PropVariantInit (&var);
    double d = 0.0;
    if (SUCCEEDED (reader->GetPresentationAttribute ((DWORD) MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) && var.vt == VT_UI8)
        d = (double) var.uhVal.QuadPart / kHns;
    PropVariantClear (&var);
    return d;
}

bool seekReader (IMFSourceReader* reader, double seconds)
{
    PROPVARIANT var;
    PropVariantInit (&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = (LONGLONG) (juce::jmax (0.0, seconds) * kHns);
    const HRESULT hr = reader->SetCurrentPosition (GUID_NULL, var);
    PropVariantClear (&var);
    return SUCCEEDED (hr);
}

/** MF_MT_VIDEO_ROTATION is counter-clockwise; we want clockwise degrees. */
int rotationOf (IMFMediaType* type)
{
    UINT32 ccw = 0;
    if (FAILED (type->GetUINT32 (MF_MT_VIDEO_ROTATION, &ccw))) return 0;
    return (int) ((360 - (ccw % 360)) % 360);
}

//==============================================================================
class MFAudio final : public AudioDecoder
{
public:
    bool open (const juce::File& file)
    {
        reader = openReader (file, false);
        if (reader == nullptr) return false;
        const DWORD stream = (DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM;
        reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
        if (FAILED (reader->SetStreamSelection (stream, TRUE))) return false;

        ComPtr<IMFMediaType> type;
        if (FAILED (MFCreateMediaType (&type))) return false;
        type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (FAILED (reader->SetCurrentMediaType (stream, nullptr, type.Get()))) return false;

        ComPtr<IMFMediaType> actual;
        if (FAILED (reader->GetCurrentMediaType (stream, &actual))) return false;
        UINT32 ch = 0, sr = 0;
        actual->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &ch);
        actual->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
        numChannels = (int) ch;
        rate = (double) sr;
        len = durationOf (reader.Get());
        return numChannels > 0 && rate > 0.0;
    }

    double sampleRate() const override { return rate; }
    int channels() const override      { return numChannels; }
    double duration() const override   { return len; }
    bool seek (double s) override      { ensureInitialised(); return seekReader (reader.Get(), s); }

    bool read (std::vector<float>& out, double& pts) override
    {
        ensureInitialised();
        for (int guard = 0; guard < 1000; ++guard)
        {
            DWORD flags = 0;
            LONGLONG ts = 0;
            ComPtr<IMFSample> sample;
            if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &ts, &sample)))
                return false;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
            if (sample == nullptr) continue;   // gap / stream tick

            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED (sample->ConvertToContiguousBuffer (&buffer))) return false;
            BYTE* data = nullptr;
            DWORD bytes = 0;
            if (FAILED (buffer->Lock (&data, nullptr, &bytes))) return false;
            out.assign (reinterpret_cast<const float*> (data), reinterpret_cast<const float*> (data) + bytes / sizeof (float));
            buffer->Unlock();
            pts = (double) ts / kHns;
            return true;
        }
        return false;
    }

private:
    ComPtr<IMFSourceReader> reader;
    int numChannels = 0;
    double rate = 0.0, len = 0.0;
};

//==============================================================================
class MFVideo final : public VideoDecoder
{
public:
    bool open (const juce::File& file, double fps)
    {
        reader = openReader (file, true);
        if (reader == nullptr) return false;
        const DWORD stream = (DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM;
        reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
        if (FAILED (reader->SetStreamSelection (stream, TRUE))) return false;

        ComPtr<IMFMediaType> type;
        if (FAILED (MFCreateMediaType (&type))) return false;
        type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED (reader->SetCurrentMediaType (stream, nullptr, type.Get()))) return false;
        frameDur = fps > 0.0 ? 1.0 / fps : 1.0 / 30.0;
        return updateFormat();
    }

    bool seek (double s) override
    {
        ensureInitialised();
        held = false;
        if (! seekReader (reader.Get(), s)) return false;
        // decode up to the picture that is on screen at s
        for (int guard = 0; guard < 2000; ++guard)
        {
            if (! readSample()) return true;
            if (heldPts + frameDur * 0.999 > s) { held = true; return true; }
        }
        return true;
    }

    bool read (FrameBuffer& out, double& pts) override
    {
        ensureInitialised();
        if (held) held = false;
        else if (! readSample()) return false;
        pts = heldPts;
        return copyOut (out);
    }

private:
    bool updateFormat()
    {
        ComPtr<IMFMediaType> current;
        if (FAILED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current))) return false;
        UINT32 w = 0, h = 0;
        if (FAILED (MFGetAttributeSize (current.Get(), MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0) return false;
        width = (int) w;
        height = (int) h;

        // the decoder may pad the picture (1080 -> 1088): only show the visible area
        crop = { 0, 0, width, height };
        MFVideoArea area {};
        if (SUCCEEDED (current->GetBlob (MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*> (&area), sizeof (area), nullptr)))
            crop = juce::Rectangle<int> (area.OffsetX.value, area.OffsetY.value, (int) area.Area.cx, (int) area.Area.cy).getIntersection ({ 0, 0, width, height });
        if (crop.isEmpty()) crop = { 0, 0, width, height };

        UINT32 strideValue = 0;
        defaultStride = SUCCEEDED (current->GetUINT32 (MF_MT_DEFAULT_STRIDE, &strideValue)) ? (LONG) (INT32) strideValue : (LONG) width * 4;
        return true;
    }

    /** Reads the next sample into `heldSample`. */
    bool readSample()
    {
        for (int guard = 0; guard < 1000; ++guard)
        {
            DWORD flags = 0;
            LONGLONG ts = 0;
            ComPtr<IMFSample> sample;
            if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &sample)))
                return false;
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) updateFormat();
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
            if (sample == nullptr) continue;
            heldSample = sample;
            heldPts = (double) ts / kHns;
            return true;
        }
        return false;
    }

    bool copyOut (FrameBuffer& out)
    {
        if (heldSample == nullptr) return false;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED (heldSample->ConvertToContiguousBuffer (&buffer))) return false;

        if (out.width != crop.getWidth() || out.height != crop.getHeight())
            out.allocate (crop.getWidth(), crop.getHeight());

        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        ComPtr<IMF2DBuffer> buffer2d;
        bool locked2d = false;
        BYTE* raw = nullptr;
        if (SUCCEEDED (buffer.As (&buffer2d)) && SUCCEEDED (buffer2d->Lock2D (&scan0, &pitch)))
        {
            locked2d = true;
        }
        else
        {
            DWORD len = 0;
            if (FAILED (buffer->Lock (&raw, nullptr, &len))) return false;
            pitch = defaultStride;
            scan0 = pitch >= 0 ? raw : raw + (size_t) (height - 1) * (size_t) (-pitch);   // bottom-up
        }

        for (int y = 0; y < crop.getHeight(); ++y)
        {
            const BYTE* src = scan0 + (ptrdiff_t) (y + crop.getY()) * pitch + crop.getX() * 4;
            auto* dst = reinterpret_cast<juce::uint32*> (out.pixels.data() + (size_t) y * (size_t) out.stride);
            const auto* s = reinterpret_cast<const juce::uint32*> (src);
            for (int x = 0; x < crop.getWidth(); ++x)
                dst[x] = s[x] | 0xff000000u;   // RGB32's fourth byte is undefined
        }

        if (locked2d) buffer2d->Unlock2D();
        else buffer->Unlock();
        return true;
    }

    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFSample> heldSample;
    double heldPts = 0.0, frameDur = 1.0 / 30.0;
    bool held = false;
    int width = 0, height = 0;
    juce::Rectangle<int> crop;
    LONG defaultStride = 0;
};
} // namespace

bool isSupported() { return true; }

juce::StringArray videoExtensions() { return { "mp4", "m4v", "mov", "mkv", "avi", "wmv", "asf", "3gp" }; }
juce::StringArray audioExtensions() { return { "m4a", "aac" }; }

bool probe (const juce::File& file, StreamInfo& info)
{
    auto reader = openReader (file, false);
    if (reader == nullptr) return false;
    info = {};
    info.duration = durationOf (reader.Get());

    ComPtr<IMFMediaType> v;
    if (SUCCEEDED (reader->GetNativeMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &v)))
    {
        UINT32 w = 0, h = 0, num = 0, den = 0, parN = 1, parD = 1;
        MFGetAttributeSize (v.Get(), MF_MT_FRAME_SIZE, &w, &h);
        if (w > 0 && h > 0)
        {
            info.hasVideo = true;
            if (FAILED (MFGetAttributeRatio (v.Get(), MF_MT_PIXEL_ASPECT_RATIO, &parN, &parD)) || parN == 0 || parD == 0) { parN = parD = 1; }
            MFVideoArea area {};
            if (SUCCEEDED (v->GetBlob (MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*> (&area), sizeof (area), nullptr))
                && area.Area.cx > 0 && area.Area.cy > 0)
            {
                w = (UINT32) area.Area.cx;
                h = (UINT32) area.Area.cy;
            }
            info.width = (int) std::lround ((double) w * parN / parD);
            info.height = (int) h;
            if (SUCCEEDED (MFGetAttributeRatio (v.Get(), MF_MT_FRAME_RATE, &num, &den)) && den > 0)
                info.fps = (double) num / den;
            info.rotation = rotationOf (v.Get());
            GUID sub {};
            if (SUCCEEDED (v->GetGUID (MF_MT_SUBTYPE, &sub))) info.videoCodec = videoCodecName (sub);
        }
    }

    ComPtr<IMFMediaType> a;
    if (SUCCEEDED (reader->GetNativeMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &a)))
    {
        info.hasAudio = true;
        UINT32 ch = 0, sr = 0;
        a->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &ch);
        a->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
        info.channels = (int) ch;
        info.sampleRate = (double) sr;
        GUID sub {};
        if (SUCCEEDED (a->GetGUID (MF_MT_SUBTYPE, &sub))) info.audioCodec = audioCodecName (sub);
    }
    return info.hasVideo || info.hasAudio;
}

std::unique_ptr<AudioDecoder> openAudio (const juce::File& file)
{
    auto d = std::make_unique<MFAudio>();
    return d->open (file) ? std::move (d) : nullptr;
}

std::unique_ptr<VideoDecoder> openVideo (const juce::File& file)
{
    StreamInfo info;
    probe (file, info);
    auto d = std::make_unique<MFVideo>();
    return d->open (file, info.fps) ? std::move (d) : nullptr;
}
} // namespace tp::media::backend
