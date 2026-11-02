#include "MainComponent.h"
#include "media/Media.h"
#include "VideoPlayer.h"
#include "TaskbarPlayer.h"

namespace tp
{
//==============================================================================
class TransportLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    TransportLookAndFeel() : juce::LookAndFeel_V4 (getDarkColourScheme())
    {
        using juce::Colour;
        setColour (juce::PopupMenu::backgroundColourId, Colour (0xff2a2a2a));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colour (0xff4a4a4a));
        setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::headerTextColourId, theme::accent);
        setColour (juce::TooltipWindow::backgroundColourId, Colour (0xff1f1f1f));
        setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::outlineColourId, Colour (0xff5c5c5c));
        setColour (juce::ResizableWindow::backgroundColourId, theme::dialog);
        setColour (juce::AlertWindow::backgroundColourId, theme::dialog);
        setColour (juce::TextButton::buttonColourId, theme::button);
        setColour (juce::ComboBox::backgroundColourId, theme::field);
        setColour (juce::ComboBox::outlineColourId, Colour (0xff5c5c5c));

       #if JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Segoe UI");
       #endif
    }

    void drawResizableFrame (juce::Graphics&, int, int, const juce::BorderSize<int>&) override {}
    juce::Font getPopupMenuFont() override { return theme::font (13.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (juce::Colour (0xff4a4a4a));
        g.drawRect (0, 0, w, h);
    }
};

//==============================================================================
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow ("Transport", theme::background, DocumentWindow::allButtons, true)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (0);

        content = new MainComponent (false);
        content->onClose = [this] { closeButtonPressed(); };
        content->onMinimise = [this] { setMinimised (true); };
        content->onMaximise = [this] { setFullScreen (! isFullScreen()); };
        setContentOwned (content, true);

        setResizable (true, false);
        setResizeLimits (640, 240, 16000, 16000);

        edges = std::make_unique<juce::ResizableBorderComponent> (this, getConstrainer());
        edges->setBorderThickness (juce::BorderSize<int> (5));
        addAndMakeVisible (*edges);

        if (! restoreWindowStateFromString (lastWindowState()))
            centreWithSize (1280, 520);

        setVisible (true);
        content->grabKeyboardFocus();

        taskbar = std::make_unique<TaskbarPlayer> (*this);
        taskbar->onPrev = [this] { content->previousTrack(); };
        taskbar->onNext = [this] { content->nextTrack(); };
        taskbar->onPlayPause = [this] { content->togglePlayback(); };
        taskbar->isPlaying = [this] { return content->isPlayingNow(); };
        taskbar->renderPreview = [this] (int w, int h) { return content->renderMiniPlayer (w, h); };
        taskbar->stateKey = [this] { return content->miniPlayerKey(); };
        taskbar->setEnabled (content->isTaskbarPlayerEnabled());
        content->onTaskbarPlayerChanged = [this] (bool on) { taskbar->setEnabled (on); };
    }

    ~MainWindow() override
    {
        taskbar.reset();
        storeWindowState (getWindowStateAsString());
    }

    juce::BorderSize<int> getBorderThickness() const override       { return {}; }
    juce::BorderSize<int> getContentComponentBorder() const override { return {}; }

    void resized() override
    {
        DocumentWindow::resized();
        if (edges != nullptr)
        {
            edges->setBounds (getLocalBounds());
            edges->setVisible (! isFullScreen());
            edges->toFront (false);
        }
    }

    WindowControlKind findControlAtPoint (juce::Point<float> p) const override
    {
        if (! isFullScreen())
        {
            const float e = 5.0f, w = (float) getWidth(), h = (float) getHeight();
            const bool left = p.x < e, right = p.x >= w - e, top = p.y < e, bottom = p.y >= h - e;
            if (top && left)     return WindowControlKind::sizeTopLeft;
            if (top && right)    return WindowControlKind::sizeTopRight;
            if (bottom && left)  return WindowControlKind::sizeBottomLeft;
            if (bottom && right) return WindowControlKind::sizeBottomRight;
            if (top)    return WindowControlKind::sizeTop;
            if (left)   return WindowControlKind::sizeLeft;
            if (right)  return WindowControlKind::sizeRight;
            if (bottom) return WindowControlKind::sizeBottom;
        }
        return content != nullptr ? content->controlAt (p) : WindowControlKind::client;
    }

    void windowControlClickedClose() override    { closeButtonPressed(); }
    void windowControlClickedMinimise() override { setMinimised (true); }
    void windowControlClickedMaximise() override { setFullScreen (! isFullScreen()); }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    MainComponent* getMain() { return content; }

private:
    static juce::PropertiesFile& windowProps()
    {
        static juce::PropertiesFile props ([]
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Transport";
            o.filenameSuffix = ".window";
            o.folderName = configFolder();
            o.osxLibrarySubFolder = "Application Support";
            return o;
        }());
        return props;
    }
    static juce::String lastWindowState()            { return windowProps().getValue ("bounds"); }
    static void storeWindowState (const juce::String& s) { windowProps().setValue ("bounds", s); windowProps().saveIfNeeded(); }

    MainComponent* content = nullptr;
    std::unique_ptr<juce::ResizableBorderComponent> edges;
    std::unique_ptr<TaskbarPlayer> taskbar;
};

//==============================================================================
/** A 20 second loop with kick, hats, bass and a wide pad, used for snapshots and as a test tone. */
juce::File writeDemoTrack()
{
    const double sr = 44100.0, seconds = 20.0, bpm = 124.0, beat = 60.0 / bpm;
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> buf (2, n);
    juce::Random rng (42);

    const double bass[] = { 55.0, 43.65, 65.41, 49.0 };
    const double pads[][3] = { { 220.0, 261.63, 329.63 }, { 174.61, 220.0, 261.63 }, { 261.63, 329.63, 392.0 }, { 196.0, 246.94, 293.66 } };
    double kickPhase = 0.0;
    const double twoPi = juce::MathConstants<double>::twoPi;

    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double inBeat = std::fmod (t, beat);
        const int bar = (int) (t / (beat * 4.0)) % 4;

        const double kf = 45.0 + 95.0 * std::exp (-inBeat * 28.0);
        kickPhase += twoPi * kf / sr;
        const double kick = 0.85 * std::sin (kickPhase) * std::exp (-inBeat * 7.0);

        const double off = std::fmod (t + beat * 0.5, beat);
        const double hat = (rng.nextDouble() * 2.0 - 1.0) * 0.16 * std::exp (-off * 45.0);

        double b = 0.0;
        for (int h = 1; h <= 6; ++h)
            b += std::sin (twoPi * bass[bar] * h * t) / h;
        b *= 0.22 * (0.6 + 0.4 * std::exp (-inBeat * 4.0));

        double pl = 0.0, pr = 0.0;
        const double trem = 0.75 + 0.25 * std::sin (twoPi * 0.25 * t);
        for (double f : pads[bar])
        {
            pl += std::sin (twoPi * f * 0.997 * t);
            pr += std::sin (twoPi * f * 1.003 * t + 0.7);
        }
        pl *= 0.055 * trem;
        pr *= 0.055 * trem;

        buf.setSample (0, i, (float) (kick + b + pl + hat * 0.7));
        buf.setSample (1, i, (float) (kick + b + pr + hat * 1.0));
    }

    const float peak = buf.getMagnitude (0, n);
    buf.applyGain (0.89f / juce::jmax (0.001f, peak));
    buf.applyGainRamp (0, 4410, 0.0f, 1.0f);

    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Transport Demo").getChildFile ("Demo - Test Signal.wav");
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
    juce::WavAudioFormat wav;
    if (auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (16)))
        writer->writeFromAudioSampleBuffer (buf, 0, n);
    return file;
}

/** --snapshot out.png [--size 1400x560] [--slots vu,polar] [--widths 1,1] [--states '[{...}]'] [--header 112 --wave-height 56] [--customize] [--file x.mp3] [--seek 0.3] [--seconds 4] [--browser] [--wave bars|mirror|reflection|line|spectral|3band] */
int runSnapshot (const juce::StringArray& args)
{
    auto value = [&] (const char* key, const juce::String& fallback)
    {
        const int i = args.indexOf (key);
        return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : fallback;
    };

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (value ("--snapshot", "snapshot.png"));
    const auto size = juce::StringArray::fromTokens (value ("--size", "1400x560"), "x", "");
    const int w = size[0].getIntValue(), h = size[1].getIntValue();

    auto main = std::make_unique<MainComponent> (true);
    main->setSize (w, h);

    if (args.contains ("--slots"))
    {
        juce::Array<juce::var> states;
        const auto parsed = juce::JSON::parse (value ("--states", "[]"));   // keep the var alive while reading it
        if (auto* arr = parsed.getArray()) states = *arr;
        juce::Array<int> widths;
        for (auto& w : juce::StringArray::fromTokens (value ("--widths", ""), ",", "")) widths.add (w.getIntValue());
        main->getSlots().setIds (juce::StringArray::fromTokens (value ("--slots", ""), ",", ""), states, widths);
        main->getEngine().setEffectChain (main->getSlots().effectChain());
    }

    juce::File track (value ("--file", {}));
    if (! track.existsAsFile())
        track = writeDemoTrack();

    if (! main->openFile (track, true))
        return 2;

    auto& thumb = main->getEngine().thumbnail();
    for (int i = 0; i < 500 && ! thumb.isFullyLoaded(); ++i)
        juce::Thread::sleep (10);
    main->getWaveform().setBandData (WaveformView::analyse (track, main->getEngine().formats(), [] { return false; }));
    const int styleIndex = WaveformView::styleIds().indexOf (value ("--wave", "bars"));
    if (styleIndex >= 0) main->getWaveform().setStyle ((WaveformView::Style) styleIndex, false);
    main->getWaveform().refreshBars();

    const double length = main->getEngine().getLength();
    main->getEngine().setPosition (length * value ("--seek", "0.3").getDoubleValue());

    if (args.contains ("--header"))
        main->setAreaHeights (value ("--header", "112").getIntValue(), value ("--wave-height", "56").getIntValue());
    if (args.contains ("--customize"))
        main->setCustomizing (true);
    if (args.contains ("--browser"))
        main->openPluginBrowser();
    if (args.contains ("--fullscreen"))
        main->setFullscreenVideo (true);

    const int frames = (int) (60.0 * value ("--seconds", "4").getDoubleValue());
    for (int i = 0; i < frames; ++i)
    {
        main->getEngine().renderOffline (735);
        main->tickFrame (1.0f / 60.0f);
    }

    if (main->hasVideo() && ! main->syncVideoNow (8000))
        std::printf ("video frame not ready\n");

    auto img = main->createComponentSnapshot (main->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (img, stream);
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}

/** --probe-media in.mp4 --out result.json: what the platform decoder sees (CI checks this on every OS). */
int runProbe (const juce::StringArray& args)
{
    auto value = [&] (const char* key, const juce::String& fallback)
    {
        const int i = args.indexOf (key);
        return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : fallback;
    };
    const juce::File in = juce::File::getCurrentWorkingDirectory().getChildFile (value ("--probe-media", {}));
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (value ("--out", "probe.json"));

    auto* obj = new juce::DynamicObject();
    juce::var result (obj);
    media::StreamInfo info;
    obj->setProperty ("backend", media::isSupported());
    obj->setProperty ("isVideoFile", media::isVideoFile (in));
    obj->setProperty ("probed", media::probe (in, info));
    obj->setProperty ("duration", info.duration);
    obj->setProperty ("hasVideo", info.hasVideo);
    obj->setProperty ("hasAudio", info.hasAudio);
    obj->setProperty ("width", info.width);
    obj->setProperty ("height", info.height);
    obj->setProperty ("rotation", info.rotation);
    obj->setProperty ("fps", info.fps);
    obj->setProperty ("videoCodec", info.videoCodec);
    obj->setProperty ("audioCodec", info.audioCodec);

    // sound through the same reader the player uses, with a jump back to test seeking
    juce::AudioFormatManager fm;
    fm.registerFormat (new media::MediaAudioFormat(), false);
    fm.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in)); reader != nullptr)
    {
        obj->setProperty ("sampleRate", reader->sampleRate);
        obj->setProperty ("audioLength", (double) reader->lengthInSamples / reader->sampleRate);
        juce::Array<juce::var> levels;
        for (double t : { 2.0, 0.5, 1.0 })
        {
            const int n = (int) (reader->sampleRate * 0.25);
            juce::AudioBuffer<float> buf (2, n);
            reader->read (&buf, 0, n, (juce::int64) (t * reader->sampleRate), true, true);
            const float rms = buf.getRMSLevel (0, 0, n);
            levels.add (juce::Decibels::gainToDecibels (rms, -120.0f));
        }
        obj->setProperty ("rmsDb", levels);
    }

    juce::Array<juce::var> frames;
    for (double t : { 0.5, 1.5, 2.5, 1.0 })
    {
        auto dec = media::openVideo (in);
        if (dec == nullptr) break;
        media::FrameBuffer fb;
        double pts = -1.0;
        dec->seek (t);
        auto* f = new juce::DynamicObject();
        f->setProperty ("t", t);
        if (dec->read (fb, pts))
        {
            double r = 0, g = 0, b = 0;
            int count = 0;
            for (int y = fb.height / 4; y < fb.height * 3 / 4; y += 2)
                for (int x = fb.width / 4; x < fb.width * 3 / 4; x += 2)
                {
                    const auto* px = fb.pixels.data() + (size_t) y * (size_t) fb.stride + (size_t) x * 4;
                    b += px[0]; g += px[1]; r += px[2]; ++count;
                }
            f->setProperty ("pts", pts);
            f->setProperty ("w", fb.width);
            f->setProperty ("h", fb.height);
            f->setProperty ("rgb", juce::Array<juce::var> { r / juce::jmax (1, count), g / juce::jmax (1, count), b / juce::jmax (1, count) });
        }
        frames.add (juce::var (f));
    }
    obj->setProperty ("frames", frames);

    // decoding straight to the on-screen size
    if (auto dec = media::openVideo (in))
    {
        const bool restart = dec->setOutputLimit (160, 90);
        dec->seek (0.5);
        media::FrameBuffer fb;
        double pts = 0.0;
        if (dec->read (fb, pts))
            obj->setProperty ("limited", juce::Array<juce::var> { fb.width, fb.height, restart });
    }
    obj->setProperty ("platform", juce::SystemStats::getOperatingSystemName());

    // real-time playback against a simulated clock, with jumps back and forward
    if (info.hasVideo)
    {
        VideoPlayer player;
        if (player.open (in))
        {
            const double frameDur = info.fps > 0.0 ? 1.0 / info.fps : 1.0 / 30.0;
            double worstLag = 0.0, clock = 0.0;
            int late = 0, ticks = 0;
            juce::Array<juce::var> seeks;
            auto run = [&] (double from, double seconds)
            {
                clock = from;
                const auto start = juce::Time::getMillisecondCounterHiRes();
                double firstOk = -1.0;
                while ((juce::Time::getMillisecondCounterHiRes() - start) * 0.001 < seconds)
                {
                    clock = from + (juce::Time::getMillisecondCounterHiRes() - start) * 0.001;
                    if (clock > info.duration - 0.05) break;
                    player.sync (clock);
                    const double lag = clock - player.pictureTime();
                    const bool ok = lag >= -0.005 && lag < frameDur + 0.02;
                    if (ok && firstOk < 0.0) firstOk = clock - from;
                    if (firstOk >= 0.0) { worstLag = juce::jmax (worstLag, lag); ++ticks; if (! ok) ++late; }
                    juce::Thread::sleep (8);
                }
                return firstOk;
            };
            for (auto [from, len] : { std::pair<double, double> { 0.0, 1.2 }, { 2.0, 0.6 }, { 0.3, 0.6 }, { 1.0, 0.6 } })
                seeks.add (run (from, len));
            obj->setProperty ("syncWorstLag", worstLag);
            obj->setProperty ("syncLateTicks", late);
            obj->setProperty ("syncTicks", ticks);
            obj->setProperty ("syncSeekSettle", seeks);
        }
    }

    out.replaceWithText (juce::JSON::toString (result));
    std::printf ("%s\n", juce::JSON::toString (result).toRawUTF8());
    return info.hasVideo || info.hasAudio ? 0 : 1;
}

//==============================================================================
class TransportApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        const auto args = juce::StringArray::fromTokens (commandLine, true);
        if (args.contains ("--probe-media"))
        {
            setApplicationReturnValue (runProbe (args));
            quit();
            return;
        }
        if (args.contains ("--snapshot"))
        {
            setApplicationReturnValue (runSnapshot (args));
            quit();
            return;
        }

        window = std::make_unique<MainWindow>();
        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        openFromCommandLine (commandLine);
        if (window != nullptr) window->toFront (true);
    }

private:
    void openFromCommandLine (const juce::String& commandLine)
    {
        for (auto& token : juce::StringArray::fromTokens (commandLine, true))
        {
            const juce::File f (token.unquoted());
            if (window != nullptr && juce::File::isAbsolutePath (token.unquoted()) && f.existsAsFile())
            {
                window->getMain()->openFile (f, true);
                return;
            }
        }
    }

    TransportLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> window;
};
} // namespace tp

START_JUCE_APPLICATION (tp::TransportApplication)
