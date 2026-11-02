#include "VideoPlayer.h"

namespace tp
{
VideoPlayer::VideoPlayer() : juce::Thread ("Transport video") {}

VideoPlayer::~VideoPlayer() { close(); }

bool VideoPlayer::open (const juce::File& file)
{
    close();
    media::StreamInfo info;
    if (! media::probe (file, info) || ! info.hasVideo)
        return false;
    decoder = media::openVideo (file);
    if (decoder == nullptr)
        return false;

    streamInfo = info;
    {
        const juce::ScopedLock sl (lock);
        seekPending = true;
        awaitingFirst = true;
        freshSeek = true;
        seekTarget = 0.0;
        atEnd = false;
        lastQueuedPts = -1.0;
    }
    shownPts = -1.0;
    image = {};
    startThread (juce::Thread::Priority::high);
    return true;
}

void VideoPlayer::close()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (2000);
    decoder.reset();
    const juce::ScopedLock sl (lock);
    queue.clear();
    pool.clear();
    image = {};
    shownPts = -1.0;
    streamInfo = {};
}

void VideoPlayer::requestSeek (double time)
{
    // caller holds the lock
    seekPending = true;
    awaitingFirst = true;
    freshSeek = true;
    seekTarget = juce::jmax (0.0, time);
    shownPts = -1.0;   // the old picture stays on screen, but no longer counts for jump detection
    wake.signal();
}

void VideoPlayer::recycle (std::unique_ptr<Frame> f)
{
    if (f != nullptr) pool.push_back (std::move (f));
}

bool VideoPlayer::sync (double t)
{
    if (! isOpen()) return false;

    std::unique_ptr<Frame> pick;
    {
        const juce::ScopedLock sl (lock);
        if (seekPending || awaitingFirst)
        {
            // a seek is on its way: only start another one if the user moved far away from it
            if (std::abs (t - seekTarget) > 0.75)
                requestSeek (t);
        }
        else
        {
            const double newest = queue.empty() ? lastQueuedPts : queue.back()->pts;
            const bool backwards = shownPts >= 0.0 && t < shownPts - 0.25;
            const bool farAhead = ! atEnd && t > newest + 1.5;
            if (backwards || farAhead)
                requestSeek (t);
        }

        if (! seekPending)
        {
            // the newest frame that is due; the first one after a seek is shown regardless
            while (! queue.empty() && (queue.front()->pts <= t + 0.004 || (freshSeek && pick == nullptr)))
            {
                recycle (std::move (pick));
                pick = std::move (queue.front());
                queue.pop_front();
                freshSeek = false;
            }
        }
    }

    if (pick == nullptr)
        return false;

    copyInto (image, pick->buffer);
    shownPts = pick->pts;
    {
        const juce::ScopedLock sl (lock);
        recycle (std::move (pick));
    }
    wake.signal();
    return true;
}

bool VideoPlayer::syncBlocking (double time, int timeoutMs)
{
    if (! isOpen()) return false;
    {
        const juce::ScopedLock sl (lock);
        requestSeek (time);
    }
    shownPts = -1.0;
    const auto start = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - start < (juce::uint32) timeoutMs)
    {
        sync (time);
        {
            const juce::ScopedLock sl (lock);
            const bool settled = ! seekPending && ! awaitingFirst
                                 && (atEnd || (! queue.empty() && queue.front()->pts > time + 0.004));
            if (settled && shownPts >= 0.0) return true;
        }
        juce::Thread::sleep (5);
    }
    return shownPts >= 0.0;
}

void VideoPlayer::run()
{
    while (! threadShouldExit())
    {
        bool doSeek = false;
        double target = 0.0;
        std::unique_ptr<Frame> f;
        {
            const juce::ScopedLock sl (lock);
            if (seekPending)
            {
                doSeek = true;
                target = seekTarget;
                seekPending = false;
                atEnd = false;
                lastQueuedPts = -1.0;
                for (auto& q : queue) recycle (std::move (q));
                queue.clear();
            }
            if (doSeek || (! atEnd && queue.size() < kQueueLength))
            {
                if (! pool.empty()) { f = std::move (pool.back()); pool.pop_back(); }
                else f = std::make_unique<Frame>();
            }
        }

        if (f == nullptr)
        {
            wake.wait (20);
            continue;
        }

        if (doSeek)
            decoder->seek (target);

        double pts = 0.0;
        const bool ok = decoder->read (f->buffer, pts);

        const juce::ScopedLock sl (lock);
        if (seekPending)        { recycle (std::move (f)); continue; }   // stale: a newer seek came in
        if (! ok)               { atEnd = true; awaitingFirst = false; recycle (std::move (f)); continue; }
        f->pts = pts;
        lastQueuedPts = pts;
        awaitingFirst = false;
        queue.push_back (std::move (f));
    }
}

void VideoPlayer::copyInto (juce::Image& img, const media::FrameBuffer& b)
{
    if (b.width <= 0 || b.height <= 0) return;
    if (! img.isValid() || img.getWidth() != b.width || img.getHeight() != b.height)
        img = juce::Image (juce::Image::ARGB, b.width, b.height, false, juce::SoftwareImageType());
    const juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < b.height; ++y)
        std::memcpy (data.getLinePointer (y), b.pixels.data() + (size_t) y * (size_t) b.stride, (size_t) b.width * 4);
}

juce::Image VideoPlayer::toImage (const media::FrameBuffer& b)
{
    juce::Image img;
    copyInto (img, b);
    return img;
}

juce::Image VideoPlayer::grabFrame (const juce::File& file, double seconds, int maxSize)
{
    media::StreamInfo info;
    if (! media::probe (file, info) || ! info.hasVideo) return {};
    auto dec = media::openVideo (file);
    if (dec == nullptr) return {};

    media::FrameBuffer buffer;
    double pts = 0.0;
    dec->seek (juce::jlimit (0.0, juce::jmax (0.0, info.duration - 0.5), seconds));
    if (! dec->read (buffer, pts)) { dec->seek (0.0); if (! dec->read (buffer, pts)) return {}; }
    const auto picture = toImage (buffer);
    if (! picture.isValid()) return {};

    const bool sideways = info.rotation == 90 || info.rotation == 270;
    float w = (float) (info.width > 0 ? info.width : picture.getWidth());
    float h = (float) (info.height > 0 ? info.height : picture.getHeight());
    if (sideways) std::swap (w, h);
    const float scale = juce::jmin (1.0f, (float) maxSize / juce::jmax (w, h));
    juce::Image out (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (w * scale)), juce::jmax (1, juce::roundToInt (h * scale)), true);
    juce::Graphics g (out);
    drawUpright (g, picture, out.getBounds().toFloat(), info);
    return out;
}

juce::Rectangle<float> VideoPlayer::fitRect (juce::Rectangle<float> area, const media::StreamInfo& info, const juce::Image& picture)
{
    float w = (float) (info.width > 0 ? info.width : picture.getWidth());
    float h = (float) (info.height > 0 ? info.height : picture.getHeight());
    if (info.rotation == 90 || info.rotation == 270) std::swap (w, h);
    if (w <= 0.0f || h <= 0.0f) return area;
    const float scale = juce::jmin (area.getWidth() / w, area.getHeight() / h);
    return juce::Rectangle<float> (w * scale, h * scale).withCentre (area.getCentre());
}

void VideoPlayer::drawUpright (juce::Graphics& g, const juce::Image& picture, juce::Rectangle<float> area, const media::StreamInfo& info)
{
    if (! picture.isValid()) return;
    const auto r = fitRect (area, info, picture);
    const bool sideways = info.rotation == 90 || info.rotation == 270;
    const float W = sideways ? r.getHeight() : r.getWidth();
    const float H = sideways ? r.getWidth() : r.getHeight();
    const auto t = juce::AffineTransform::translation (-0.5f * (float) picture.getWidth(), -0.5f * (float) picture.getHeight())
                       .scaled (W / (float) picture.getWidth(), H / (float) picture.getHeight())
                       .rotated (juce::degreesToRadians ((float) info.rotation))
                       .translated (r.getCentre());
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (picture, t);
}
} // namespace tp
