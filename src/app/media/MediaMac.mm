// macOS: decoding through AVFoundation (part of macOS, nothing to ship). Compiled with ARC.
#include "Media.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   // the synchronous asset API is fine on a decoder thread

namespace tp::media::backend
{
namespace
{
AVURLAsset* openAsset (const juce::File& file)
{
    NSURL* url = [NSURL fileURLWithPath: [NSString stringWithUTF8String: file.getFullPathName().toRawUTF8()]];
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL: url options: @{ AVURLAssetPreferPreciseDurationAndTimingKey: @YES }];
    return asset != nil && asset.readable ? asset : nil;
}

AVAssetTrack* firstTrack (AVAsset* asset, AVMediaType type)
{
    NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType: type];
    return tracks.count > 0 ? tracks.firstObject : nil;
}

FourCharCode subType (AVAssetTrack* track)
{
    if (track.formatDescriptions.count == 0) return 0;
    auto desc = (__bridge CMFormatDescriptionRef) track.formatDescriptions.firstObject;
    return CMFormatDescriptionGetMediaSubType (desc);
}

juce::String videoCodecName (FourCharCode c)
{
    switch (c)
    {
        case 'avc1': case 'avc3': return "H.264";
        case 'hvc1': case 'hev1': return "HEVC";
        case 'av01': return "AV1";
        case 'vp09': return "VP9";
        case 'mp4v': return "MPEG-4";
        case 'apcn': case 'apch': case 'apcs': case 'apco': case 'ap4h': case 'ap4x': return "ProRes";
        case 'jpeg': return "MJPEG";
        default: break;
    }
    char s[5] = { (char) (c >> 24), (char) (c >> 16), (char) (c >> 8), (char) c, 0 };
    return juce::String (s).trim();
}

juce::String audioCodecName (FourCharCode c)
{
    switch (c)
    {
        case kAudioFormatMPEG4AAC: case kAudioFormatMPEG4AAC_HE: case kAudioFormatMPEG4AAC_HE_V2: return "AAC";
        case kAudioFormatMPEGLayer3: return "MP3";
        case kAudioFormatAppleLossless: return "ALAC";
        case kAudioFormatAC3: return "AC-3";
        case kAudioFormatEnhancedAC3: return "E-AC-3";
        case kAudioFormatLinearPCM: return "PCM";
        case kAudioFormatOpus: return "Opus";
        case kAudioFormatFLAC: return "FLAC";
        default: return {};
    }
}

double seconds (CMTime t) { return CMTIME_IS_NUMERIC (t) ? CMTimeGetSeconds (t) : 0.0; }

/** One AVAssetReader on one track, restarted from a new time to seek. */
struct TrackReader
{
    AVURLAsset* asset = nil;
    AVAssetTrack* track = nil;
    NSDictionary* settings = nil;
    AVAssetReader* reader = nil;
    AVAssetReaderTrackOutput* output = nil;

    bool start (double from)
    {
        @autoreleasepool
        {
            if (reader != nil) [reader cancelReading];
            NSError* error = nil;
            reader = [AVAssetReader assetReaderWithAsset: asset error: &error];
            if (reader == nil) return false;
            reader.timeRange = CMTimeRangeMake (CMTimeMakeWithSeconds (juce::jmax (0.0, from), 90000), kCMTimePositiveInfinity);
            output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack: track outputSettings: settings];
            output.alwaysCopiesSampleData = NO;
            if (! [reader canAddOutput: output]) return false;
            [reader addOutput: output];
            return [reader startReading];
        }
    }

    ~TrackReader()
    {
        if (reader != nil) [reader cancelReading];
    }

    /** +1 sample buffer or nullptr at the end. */
    CMSampleBufferRef next()
    {
        if (reader == nil || reader.status != AVAssetReaderStatusReading) return nullptr;
        return [output copyNextSampleBuffer];
    }
};

//==============================================================================
class AVAudio final : public AudioDecoder
{
public:
    bool open (const juce::File& file)
    {
        @autoreleasepool
        {
            r.asset = openAsset (file);
            if (r.asset == nil) return false;
            r.track = firstTrack (r.asset, AVMediaTypeAudio);
            if (r.track == nil || r.track.formatDescriptions.count == 0) return false;

            auto desc = (__bridge CMAudioFormatDescriptionRef) r.track.formatDescriptions.firstObject;
            const auto* asbd = CMAudioFormatDescriptionGetStreamBasicDescription (desc);
            if (asbd == nullptr || asbd->mSampleRate <= 0.0) return false;
            rate = asbd->mSampleRate;
            numChannels = juce::jlimit (1, 2, (int) asbd->mChannelsPerFrame);   // AVFoundation downmixes surround

            NSMutableDictionary* s = [@{ AVFormatIDKey: @(kAudioFormatLinearPCM),
                                         AVLinearPCMBitDepthKey: @32,
                                         AVLinearPCMIsFloatKey: @YES,
                                         AVLinearPCMIsBigEndianKey: @NO,
                                         AVLinearPCMIsNonInterleaved: @NO,
                                         AVSampleRateKey: @(rate),
                                         AVNumberOfChannelsKey: @(numChannels) } mutableCopy];
            AudioChannelLayout layout {};
            layout.mChannelLayoutTag = numChannels == 1 ? kAudioChannelLayoutTag_Mono : kAudioChannelLayoutTag_Stereo;
            s[AVChannelLayoutKey] = [NSData dataWithBytes: &layout length: sizeof (layout)];
            r.settings = s;

            len = seconds (r.asset.duration);
            return r.start (0.0);
        }
    }

    double sampleRate() const override { return rate; }
    int channels() const override      { return numChannels; }
    double duration() const override   { return len; }
    bool seek (double s) override      { return r.start (s); }

    bool read (std::vector<float>& out, double& pts) override
    {
        @autoreleasepool
        {
            for (int guard = 0; guard < 100; ++guard)
            {
                CMSampleBufferRef sb = r.next();
                if (sb == nullptr) return false;
                CMBlockBufferRef block = CMSampleBufferGetDataBuffer (sb);
                const size_t bytes = block != nullptr ? CMBlockBufferGetDataLength (block) : 0;
                if (bytes == 0) { CFRelease (sb); continue; }
                out.resize (bytes / sizeof (float));
                CMBlockBufferCopyDataBytes (block, 0, out.size() * sizeof (float), out.data());
                pts = seconds (CMSampleBufferGetPresentationTimeStamp (sb));
                CFRelease (sb);
                return true;
            }
            return false;
        }
    }

private:
    TrackReader r;
    double rate = 0.0, len = 0.0;
    int numChannels = 2;
};

//==============================================================================
class AVVideo final : public VideoDecoder
{
public:
    ~AVVideo() override { drop(); }

    bool open (const juce::File& file)
    {
        @autoreleasepool
        {
            r.asset = openAsset (file);
            if (r.asset == nil) return false;
            r.track = firstTrack (r.asset, AVMediaTypeVideo);
            if (r.track == nil) return false;
            if (r.track.formatDescriptions.count > 0)
            {
                const auto dims = CMVideoFormatDescriptionGetDimensions ((__bridge CMVideoFormatDescriptionRef) r.track.formatDescriptions.firstObject);
                codedW = dims.width;
                codedH = dims.height;
            }
            updateSettings();
            const float fps = r.track.nominalFrameRate;
            frameDur = fps > 0.0f ? 1.0 / fps : 1.0 / 30.0;
            return r.start (0.0);
        }
    }

    bool setOutputLimit (int w, int h) override
    {
        const auto before = limitedSize (codedW, codedH, limitW, limitH);
        limitW = w;
        limitH = h;
        if (limitedSize (codedW, codedH, limitW, limitH) == before) return false;
        updateSettings();
        return true;   // the reader has to restart with the new size
    }

    bool seek (double s) override
    {
        @autoreleasepool
        {
            drop();
            if (! r.start (juce::jmax (0.0, s - frameDur))) return false;
            for (int guard = 0; guard < 2000; ++guard)
            {
                CMSampleBufferRef sb = nextFrame();
                if (sb == nullptr) return true;
                held = sb;
                if (seconds (CMSampleBufferGetPresentationTimeStamp (sb)) + frameDur * 0.999 > s) return true;
                drop();
            }
            return true;
        }
    }

    bool read (FrameBuffer& out, double& pts) override
    {
        @autoreleasepool
        {
            CMSampleBufferRef sb = held != nullptr ? held : nextFrame();
            held = nullptr;
            if (sb == nullptr) return false;
            pts = seconds (CMSampleBufferGetPresentationTimeStamp (sb));
            const bool ok = copyOut (sb, out);
            CFRelease (sb);
            return ok;
        }
    }

private:
    void updateSettings()
    {
        NSMutableDictionary* s = [@{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) } mutableCopy];
        const auto size = limitedSize (codedW, codedH, limitW, limitH);
        if (codedW > 0 && codedH > 0 && (size.x != codedW || size.y != codedH))
        {
            s[(id) kCVPixelBufferWidthKey] = @(size.x);
            s[(id) kCVPixelBufferHeightKey] = @(size.y);
        }
        r.settings = s;
    }

    CMSampleBufferRef nextFrame()
    {
        for (int guard = 0; guard < 100; ++guard)
        {
            CMSampleBufferRef sb = r.next();
            if (sb == nullptr) return nullptr;
            if (CMSampleBufferGetImageBuffer (sb) != nullptr) return sb;
            CFRelease (sb);   // marker buffer without a picture
        }
        return nullptr;
    }

    static bool copyOut (CMSampleBufferRef sb, FrameBuffer& out)
    {
        CVImageBufferRef image = CMSampleBufferGetImageBuffer (sb);
        if (image == nullptr) return false;
        CVPixelBufferLockBaseAddress (image, kCVPixelBufferLock_ReadOnly);
        const int w = (int) CVPixelBufferGetWidth (image), h = (int) CVPixelBufferGetHeight (image);
        const size_t bpr = CVPixelBufferGetBytesPerRow (image);
        const auto* base = static_cast<const juce::uint8*> (CVPixelBufferGetBaseAddress (image));
        if (base != nullptr && w > 0 && h > 0)
        {
            if (out.width != w || out.height != h) out.allocate (w, h);
            for (int y = 0; y < h; ++y)
                std::memcpy (out.pixels.data() + (size_t) y * (size_t) out.stride, base + (size_t) y * bpr, (size_t) w * 4);
        }
        CVPixelBufferUnlockBaseAddress (image, kCVPixelBufferLock_ReadOnly);
        return base != nullptr && w > 0 && h > 0;
    }

    void drop()
    {
        if (held != nullptr) { CFRelease (held); held = nullptr; }
    }

    TrackReader r;
    CMSampleBufferRef held = nullptr;
    double frameDur = 1.0 / 30.0;
    int codedW = 0, codedH = 0, limitW = 0, limitH = 0;
};
} // namespace

bool isSupported() { return true; }
juce::StringArray videoExtensions() { return { "mp4", "m4v", "mov", "3gp" }; }
juce::StringArray audioExtensions() { return {}; }   // CoreAudio already reads m4a / aac

bool probe (const juce::File& file, StreamInfo& info)
{
    @autoreleasepool
    {
        AVURLAsset* asset = openAsset (file);
        if (asset == nil) return false;
        info = {};
        info.duration = seconds (asset.duration);

        if (AVAssetTrack* v = firstTrack (asset, AVMediaTypeVideo))
        {
            CGSize size = v.naturalSize;
            if (v.formatDescriptions.count > 0)
            {
                auto desc = (__bridge CMVideoFormatDescriptionRef) v.formatDescriptions.firstObject;
                size = CMVideoFormatDescriptionGetPresentationDimensions (desc, true, true);
            }
            if (size.width > 0 && size.height > 0)
            {
                info.hasVideo = true;
                info.width = (int) std::lround (size.width);
                info.height = (int) std::lround (size.height);
                const CGAffineTransform t = v.preferredTransform;
                const int degrees = (int) std::lround (std::atan2 (t.b, t.a) * 180.0 / M_PI);
                info.rotation = (((degrees % 360) + 360) % 360 + 45) / 90 * 90 % 360;
                info.fps = v.nominalFrameRate;
                info.videoCodec = videoCodecName (subType (v));
            }
        }

        if (AVAssetTrack* a = firstTrack (asset, AVMediaTypeAudio))
        {
            info.hasAudio = true;
            if (a.formatDescriptions.count > 0)
            {
                auto desc = (__bridge CMAudioFormatDescriptionRef) a.formatDescriptions.firstObject;
                if (const auto* asbd = CMAudioFormatDescriptionGetStreamBasicDescription (desc))
                {
                    info.sampleRate = asbd->mSampleRate;
                    info.channels = (int) asbd->mChannelsPerFrame;
                }
            }
            info.audioCodec = audioCodecName (subType (a));
        }
        return info.hasVideo || info.hasAudio;
    }
}

std::unique_ptr<AudioDecoder> openAudio (const juce::File& file)
{
    auto d = std::make_unique<AVAudio>();
    return d->open (file) ? std::move (d) : nullptr;
}

std::unique_ptr<VideoDecoder> openVideo (const juce::File& file)
{
    auto d = std::make_unique<AVVideo>();
    return d->open (file) ? std::move (d) : nullptr;
}
} // namespace tp::media::backend

#pragma clang diagnostic pop
