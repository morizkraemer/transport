#include "MainComponent.h"

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
        setResizeLimits (900, 250, 16000, 16000);

        edges = std::make_unique<juce::ResizableBorderComponent> (this, getConstrainer());
        edges->setBorderThickness (juce::BorderSize<int> (5));
        addAndMakeVisible (*edges);

        if (! restoreWindowStateFromString (lastWindowState()))
            centreWithSize (1400, 560);

        setVisible (true);
        content->grabKeyboardFocus();
    }

    ~MainWindow() override
    {
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

/** --snapshot out.png [--size 1400x560] [--slots vu,polar] [--states '[{...}]'] [--cols 3] [--file x.mp3] [--seek 0.3] [--seconds 4] [--browser] [--wave bars|mirror|reflection|line|spectral|3band] */
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
        main->getSlots().setColumns (value ("--cols", "3").getIntValue());
        juce::Array<juce::var> states;
        const auto parsed = juce::JSON::parse (value ("--states", "[]"));   // keep the var alive while reading it
        if (auto* arr = parsed.getArray()) states = *arr;
        main->getSlots().setIds (juce::StringArray::fromTokens (value ("--slots", ""), ",", ""), states);
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

    if (args.contains ("--browser"))
        main->openPluginBrowser();

    const int frames = (int) (60.0 * value ("--seconds", "4").getDoubleValue());
    for (int i = 0; i < frames; ++i)
    {
        main->getEngine().renderOffline (735);
        main->tickFrame (1.0f / 60.0f);
    }

    auto img = main->createComponentSnapshot (main->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (img, stream);
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
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
