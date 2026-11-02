#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>
#include "media/Media.h"

namespace tp
{
/** Decodes the picture of a video on its own thread, a few frames ahead, and hands the UI the
    frame that belongs to the audible playback time. The audio engine is the clock: seeking,
    pausing and looping just follow the time passed to sync(). */
class VideoPlayer final : private juce::Thread
{
public:
    VideoPlayer();
    ~VideoPlayer() override;

    bool open (const juce::File&);
    void close();
    bool isOpen() const                    { return decoder != nullptr; }
    const media::StreamInfo& info() const  { return streamInfo; }

    /** UI thread, once per display frame. Returns true when the picture changed. */
    bool sync (double audibleTime);
    /** How many pixels the picture covers on screen; frames are decoded at (at most) that size. */
    void setDisplaySize (int pixelWidth, int pixelHeight);

    /** Waits until the picture for `time` has been decoded (snapshots, tests). */
    bool syncBlocking (double time, int timeoutMs);
    const juce::Image& picture() const     { return image; }
    double pictureTime() const             { return shownPts; }

    /** Decodes one picture (any thread); scaled to fit maxSize. */
    static juce::Image grabFrame (const juce::File&, double seconds, int maxSize);
    static juce::Image toImage (const media::FrameBuffer&);
    static void copyInto (juce::Image&, const media::FrameBuffer&);

    /** Where the upright picture sits inside `area` (letterboxed). */
    static juce::Rectangle<float> fitRect (juce::Rectangle<float> area, const media::StreamInfo&, const juce::Image&);
    /** Draws the picture upright (rotation, pixel aspect) and letterboxed into `area`. */
    static void drawUpright (juce::Graphics&, const juce::Image&, juce::Rectangle<float> area, const media::StreamInfo&);

private:
    struct Frame { media::FrameBuffer buffer; double pts = 0.0; };

    void run() override;
    void requestSeek (double time);
    void recycle (std::unique_ptr<Frame>);

    std::unique_ptr<media::VideoDecoder> decoder;
    media::StreamInfo streamInfo;

    juce::CriticalSection lock;
    std::deque<std::unique_ptr<Frame>> queue;
    std::vector<std::unique_ptr<Frame>> pool;
    bool seekPending = false, atEnd = false, awaitingFirst = true, freshSeek = true;
    double seekTarget = 0.0, lastQueuedPts = -1.0;
    juce::WaitableEvent wake;

    // UI side
    juce::Image image;
    double shownPts = -1.0;

    std::atomic<int> wantW { 0 }, wantH { 0 };
    int appliedW = -1, appliedH = -1;   // decoder thread

    static constexpr size_t kQueueLength = 6;
};
} // namespace tp
