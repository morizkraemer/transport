// Linux (and any platform built with TRANSPORT_FFMPEG): decoding through the system's FFmpeg libraries.
#include "Media.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace tp::media::backend
{
namespace
{
juce::String prettyCodec (AVCodecID id)
{
    switch (id)
    {
        case AV_CODEC_ID_H264:       return "H.264";
        case AV_CODEC_ID_HEVC:       return "HEVC";
        case AV_CODEC_ID_VP8:        return "VP8";
        case AV_CODEC_ID_VP9:        return "VP9";
        case AV_CODEC_ID_AV1:        return "AV1";
        case AV_CODEC_ID_MPEG4:      return "MPEG-4";
        case AV_CODEC_ID_MPEG2VIDEO: return "MPEG-2";
        case AV_CODEC_ID_PRORES:     return "ProRes";
        case AV_CODEC_ID_AAC:        return "AAC";
        case AV_CODEC_ID_MP3:        return "MP3";
        case AV_CODEC_ID_OPUS:       return "Opus";
        case AV_CODEC_ID_VORBIS:     return "Vorbis";
        case AV_CODEC_ID_AC3:        return "AC-3";
        case AV_CODEC_ID_EAC3:       return "E-AC-3";
        case AV_CODEC_ID_FLAC:       return "FLAC";
        case AV_CODEC_ID_ALAC:       return "ALAC";
        default: break;
    }
    if (auto* d = avcodec_descriptor_get (id))
    {
        const juce::String name (d->name);
        return name.startsWith ("pcm_") ? juce::String ("PCM") : name.toUpperCase();
    }
    return {};
}

void quiet()
{
    static const bool once = [] { av_log_set_level (AV_LOG_ERROR); return true; }();
    juce::ignoreUnused (once);
}

double toSeconds (int64_t ts, AVRational tb) { return ts == AV_NOPTS_VALUE ? 0.0 : (double) ts * av_q2d (tb); }

int rotationOf (AVStream* st)
{
    const int32_t* matrix = nullptr;
   #if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(60, 15, 100)
    if (auto* sd = av_packet_side_data_get (st->codecpar->coded_side_data, st->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX))
        matrix = reinterpret_cast<const int32_t*> (sd->data);
   #else
    matrix = reinterpret_cast<const int32_t*> (av_stream_get_side_data (st, AV_PKT_DATA_DISPLAYMATRIX, nullptr));
   #endif
    if (matrix == nullptr) return 0;
    const double ccw = av_display_rotation_get (matrix);   // counter-clockwise degrees
    if (std::isnan (ccw)) return 0;
    int cw = (int) std::lround (-ccw);
    cw = ((cw % 360) + 360) % 360;
    return (cw + 45) / 90 * 90 % 360;
}

/** One open file + one stream decoder. */
struct Demux
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    int stream = -1;
    bool draining = false;

    ~Demux()
    {
        av_frame_free (&frame);
        av_packet_free (&packet);
        avcodec_free_context (&codec);
        if (fmt != nullptr) avformat_close_input (&fmt);
    }

    bool open (const juce::File& file, AVMediaType type)
    {
        quiet();
        if (avformat_open_input (&fmt, file.getFullPathName().toRawUTF8(), nullptr, nullptr) < 0) return false;
        if (avformat_find_stream_info (fmt, nullptr) < 0) return false;

        const AVCodec* dec = nullptr;
        stream = av_find_best_stream (fmt, type, -1, -1, &dec, 0);
        if (stream < 0 || dec == nullptr) return false;
        if (type == AVMEDIA_TYPE_VIDEO && (fmt->streams[stream]->disposition & AV_DISPOSITION_ATTACHED_PIC)) return false;

        for (unsigned i = 0; i < fmt->nb_streams; ++i)
            if ((int) i != stream) fmt->streams[i]->discard = AVDISCARD_ALL;

        codec = avcodec_alloc_context3 (dec);
        if (codec == nullptr || avcodec_parameters_to_context (codec, fmt->streams[stream]->codecpar) < 0) return false;
        codec->thread_count = 0;   // automatic
        codec->pkt_timebase = fmt->streams[stream]->time_base;
        if (avcodec_open2 (codec, dec, nullptr) < 0) return false;

        packet = av_packet_alloc();
        frame = av_frame_alloc();
        return packet != nullptr && frame != nullptr;
    }

    AVRational timeBase() const { return fmt->streams[stream]->time_base; }

    /** Next decoded frame into `frame`. */
    bool next()
    {
        for (;;)
        {
            const int r = avcodec_receive_frame (codec, frame);
            if (r == 0) return true;
            if (r == AVERROR_EOF) return false;
            if (r != AVERROR (EAGAIN)) return false;
            if (draining) return false;

            const int rd = av_read_frame (fmt, packet);
            if (rd < 0)
            {
                draining = true;
                avcodec_send_packet (codec, nullptr);
                continue;
            }
            if (packet->stream_index == stream)
                avcodec_send_packet (codec, packet);
            av_packet_unref (packet);
        }
    }

    double framePts() const
    {
        int64_t ts = frame->best_effort_timestamp;
        if (ts == AV_NOPTS_VALUE) ts = frame->pts;
        return toSeconds (ts, timeBase());
    }

    bool seek (double seconds)
    {
        const auto ts = (int64_t) (seconds / av_q2d (timeBase()));
        const int r = av_seek_frame (fmt, stream, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers (codec);
        draining = false;
        return r >= 0;
    }
};

class FFAudio final : public AudioDecoder
{
public:
    bool open (const juce::File& file)
    {
        if (! d.open (file, AVMEDIA_TYPE_AUDIO)) return false;
        const int inChannels = d.codec->ch_layout.nb_channels;
        outChannels = inChannels >= 2 ? 2 : 1;
        rate = d.codec->sample_rate;
        if (rate <= 0) return false;

        AVChannelLayout outLayout;
        av_channel_layout_default (&outLayout, outChannels);
        if (swr_alloc_set_opts2 (&swr, &outLayout, AV_SAMPLE_FMT_FLT, rate,
                                 &d.codec->ch_layout, d.codec->sample_fmt, rate, 0, nullptr) < 0
            || swr_init (swr) < 0)
            return false;

        len = d.fmt->duration != AV_NOPTS_VALUE ? (double) d.fmt->duration / AV_TIME_BASE
                                                : toSeconds (d.fmt->streams[d.stream]->duration, d.timeBase());
        return true;
    }

    ~FFAudio() override { swr_free (&swr); }

    double sampleRate() const override { return rate; }
    int channels() const override      { return outChannels; }
    double duration() const override   { return len; }
    bool seek (double s) override      { swr_init (swr); return d.seek (s); }

    bool read (std::vector<float>& out, double& pts) override
    {
        if (! d.next()) return false;
        pts = d.framePts();
        const int maxOut = swr_get_out_samples (swr, d.frame->nb_samples);
        out.resize ((size_t) juce::jmax (0, maxOut) * (size_t) outChannels);
        auto* dst = reinterpret_cast<uint8_t*> (out.data());
        const int got = swr_convert (swr, &dst, maxOut, (const uint8_t**) d.frame->extended_data, d.frame->nb_samples);
        out.resize ((size_t) juce::jmax (0, got) * (size_t) outChannels);
        return true;
    }

private:
    Demux d;
    SwrContext* swr = nullptr;
    int outChannels = 2;
    double rate = 0.0, len = 0.0;
};

class FFVideo final : public VideoDecoder
{
public:
    bool open (const juce::File& file)
    {
        if (! d.open (file, AVMEDIA_TYPE_VIDEO)) return false;
        const auto fr = d.fmt->streams[d.stream]->avg_frame_rate;
        frameDur = fr.num > 0 && fr.den > 0 ? 1.0 / av_q2d (fr) : 1.0 / 30.0;
        return true;
    }

    ~FFVideo() override { sws_freeContext (sws); }

    bool seek (double s) override
    {
        if (! d.seek (juce::jmax (0.0, s))) return false;
        // decode up to the frame that is on screen at s, without converting the ones before it
        for (int guard = 0; guard < 2000; ++guard)
        {
            if (! d.next()) { held = false; return true; }
            if (d.framePts() + frameDur * 0.999 > s) { held = true; return true; }
        }
        held = false;
        return true;
    }

    bool read (FrameBuffer& out, double& pts) override
    {
        if (held) held = false;
        else if (! d.next()) return false;

        auto* f = d.frame;
        pts = d.framePts();
        sws = sws_getCachedContext (sws, f->width, f->height, (AVPixelFormat) f->format,
                                    f->width, f->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws == nullptr) return false;
        if (out.width != f->width || out.height != f->height) out.allocate (f->width, f->height);
        uint8_t* dst[] = { out.pixels.data() };
        const int dstStride[] = { out.stride };
        sws_scale (sws, f->data, f->linesize, 0, f->height, dst, dstStride);
        return true;
    }

private:
    Demux d;
    SwsContext* sws = nullptr;
    double frameDur = 1.0 / 30.0;
    bool held = false;
};
} // namespace

bool isSupported() { return true; }

juce::StringArray videoExtensions()
{
    return { "mp4", "m4v", "mov", "mkv", "webm", "avi", "wmv", "flv", "mpg", "mpeg", "ts", "m2ts", "3gp", "ogv" };
}

juce::StringArray audioExtensions() { return { "m4a", "aac", "opus", "wma" }; }

bool probe (const juce::File& file, StreamInfo& info)
{
    quiet();
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input (&fmt, file.getFullPathName().toRawUTF8(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info (fmt, nullptr) < 0) { avformat_close_input (&fmt); return false; }

    info = {};
    info.duration = fmt->duration != AV_NOPTS_VALUE ? (double) fmt->duration / AV_TIME_BASE : 0.0;

    const int v = av_find_best_stream (fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (v >= 0 && ! (fmt->streams[v]->disposition & AV_DISPOSITION_ATTACHED_PIC))
    {
        auto* st = fmt->streams[v];
        info.hasVideo = true;
        const auto sar = av_guess_sample_aspect_ratio (fmt, st, nullptr);
        const double par = sar.num > 0 && sar.den > 0 ? av_q2d (sar) : 1.0;
        info.width = (int) std::lround (st->codecpar->width * par);
        info.height = st->codecpar->height;
        info.rotation = rotationOf (st);
        const auto fr = st->avg_frame_rate.num > 0 ? st->avg_frame_rate : st->r_frame_rate;
        info.fps = fr.num > 0 && fr.den > 0 ? av_q2d (fr) : 0.0;
        info.videoCodec = prettyCodec (st->codecpar->codec_id);
    }

    const int a = av_find_best_stream (fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (a >= 0)
    {
        auto* st = fmt->streams[a];
        info.hasAudio = true;
        info.sampleRate = st->codecpar->sample_rate;
        info.channels = st->codecpar->ch_layout.nb_channels;
        info.audioCodec = prettyCodec (st->codecpar->codec_id);
    }

    avformat_close_input (&fmt);
    return info.hasVideo || info.hasAudio;
}

std::unique_ptr<AudioDecoder> openAudio (const juce::File& file)
{
    auto d = std::make_unique<FFAudio>();
    return d->open (file) ? std::move (d) : nullptr;
}

std::unique_ptr<VideoDecoder> openVideo (const juce::File& file)
{
    auto d = std::make_unique<FFVideo>();
    return d->open (file) ? std::move (d) : nullptr;
}
} // namespace tp::media::backend
