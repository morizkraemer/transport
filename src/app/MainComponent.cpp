#include "MainComponent.h"
#include "TrackInfo.h"
#include "DefaultPlayer.h"
#include "media/Media.h"

namespace tp
{
namespace
{
constexpr int kTitleH = TitleBar::kHeight;
constexpr int kMinWave = 32, kMaxWave = 180, kMinPlugins = 90;

const juce::StringArray defaultSlots { "vu", "polar" };
}

//==============================================================================
/** Horizontal drag handle between two areas; only visible (and grabbable) in customize mode. */
class MainComponent::Splitter final : public juce::Component
{
public:
    std::function<void (int deltaY)> onDrag;
    std::function<void()> onDragEnd;
    Smoothed shown, hover;

    Splitter() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }

    void animate (float dt)
    {
        hover.target = isMouseOverOrDragging() ? 1.0f : 0.0f;
        const bool a = shown.tick (dt, 10.0f), b = hover.tick (dt, 16.0f);
        setInterceptsMouseClicks (shown.target > 0.5f, false);
        if (a || b) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (shown.current < 0.01f) return;
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::accent.withAlpha ((0.25f + 0.45f * hover.current) * shown.current));
        g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 2.0f));
        const auto pill = juce::Rectangle<float> (46.0f, 10.0f).withCentre (r.getCentre());
        g.setColour (theme::accent.withAlpha (shown.current));
        g.fillRoundedRectangle (pill, 5.0f);
        g.setColour (theme::titleBlock.withAlpha (shown.current));
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (pill.getCentre().translated ((float) i * 8.0f, 0.0f)));
    }

    void mouseDown (const juce::MouseEvent&) override { lastY = 0; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int dy = e.getDistanceFromDragStartY();
        if (onDrag) onDrag (dy - lastY);
        lastY = dy;
    }
    void mouseUp (const juce::MouseEvent&) override { if (onDragEnd) onDragEnd(); }

private:
    int lastY = 0;
};

//==============================================================================
/** Slim banner asking to become the default audio player. */
class MainComponent::DefaultPrompt final : public juce::Component
{
public:
    std::function<void (int choice)> onChoice;   // 0 make default, 1 not now, 2 never
    Smoothed shown;
    HoverState buttons[3];

    void animate (float dt)
    {
        bool dirty = shown.tick (dt, 9.0f);
        for (auto& b : buttons) dirty |= b.tick (dt);
        setVisible (shown.current > 0.01f);
        if (dirty) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const float a = shown.current;
        auto r = getLocalBounds().toFloat().reduced (1.0f).translated (0.0f, 10.0f * (1.0f - easeOutCubic (a)));
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f * a), 18, { 0, 6 }).drawForRectangle (g, r.toNearestInt());
        g.setColour (theme::titleBlock.withAlpha (0.97f * a));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::accent.withAlpha (a));
        g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre ({ r.getX() + 18.0f, r.getCentreY() }));
        g.setColour (juce::Colours::white.withAlpha (a));
        g.setFont (theme::font (12.0f, true));
        g.drawText ("Make Transport your default audio player?", r.withTrimmedLeft (32).withTrimmedRight (310), juce::Justification::centredLeft, true);

        const char* labels[] = { "Make default", "Not now", "Don't ask again" };
        for (int i = 0; i < 3; ++i)
        {
            const auto b = buttonRect (i).translated (0.0f, 10.0f * (1.0f - easeOutCubic (a)));
            const float h = buttons[i].hover.current;
            g.setColour ((i == 0 ? theme::accent.interpolatedWith (juce::Colours::white, 0.2f * h) : juce::Colour (0xff3a3a3a).interpolatedWith (juce::Colour (0xff4a4a4a), h)).withAlpha (a));
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour ((i == 0 ? theme::titleBlock : juce::Colours::white).withAlpha (a));
            g.setFont (theme::font (11.0f, i == 0));
            g.drawText (labels[i], b, juce::Justification::centred, false);
        }
    }

    juce::Rectangle<float> buttonRect (int i) const
    {
        const float widths[] = { 104.0f, 74.0f, 112.0f };
        float x = (float) getWidth() - 10.0f;
        for (int k = 2; k >= i; --k) x -= widths[k] + (k < 2 ? 6.0f : 0.0f);
        return { x, ((float) getHeight() - 26.0f) * 0.5f, widths[i], 26.0f };
    }

    int buttonAt (juce::Point<float> p) const { for (int i = 0; i < 3; ++i) if (buttonRect (i).contains (p)) return i; return -1; }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int b = buttonAt (e.position);
        for (int i = 0; i < 3; ++i) buttons[i].hover.target = i == b ? 1.0f : 0.0f;
        setMouseCursor (b >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { for (auto& b : buttons) b.hover.target = 0.0f; }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int b = buttonAt (e.position);
        if (b >= 0 && onChoice) onChoice (b);
    }
};

//==============================================================================
MainComponent::MainComponent (bool isHeadless)
    : headless (isHeadless), waveform (engine.thumbnail(), engine.formats())
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Transport";
    opts.filenameSuffix = ".settings";
    opts.folderName = configFolder();
    opts.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters (opts);
    auto* settings = properties.getUserSettings();

    popL.resize (8192);
    popR.resize (8192);

    addAndMakeVisible (titleBar);
    addAndMakeVisible (header);
    addAndMakeVisible (waveform);
    addAndMakeVisible (slotGrid);
    addChildComponent (videoView);
    splitHeader = std::make_unique<Splitter>();
    splitWave = std::make_unique<Splitter>();
    addAndMakeVisible (*splitHeader);
    addAndMakeVisible (*splitWave);
    splitHeader->onDrag = [this] (int dy) { headerHeight = juce::jlimit (HeaderView::kMinHeight, HeaderView::kMaxHeight, headerHeight + dy); resized(); };
    splitWave->onDrag = [this] (int dy) { waveHeight = juce::jlimit (kMinWave, kMaxWave, waveHeight + dy); resized(); };
    splitHeader->onDragEnd = splitWave->onDragEnd = [this] { saveSettings(); };
    addChildComponent (browser);
    defaultPrompt = std::make_unique<DefaultPrompt>();
    addChildComponent (*defaultPrompt);
    defaultPrompt->onChoice = [this] (int choice)
    {
        defaultPrompt->shown.target = 0.0f;
        auto* st = properties.getUserSettings();
        if (choice == 0) toast (defaultplayer::makeDefault());
        if (choice == 2) st->setValue ("defaultPrompt", "never");
        st->saveIfNeeded();
    };

    // ---- title bar
    titleBar.onMenu = [this] { showMainMenu(); };
    titleBar.onMinimise = [this] { if (onMinimise) onMinimise(); };
    titleBar.onMaximise = [this] { if (onMaximise) onMaximise(); };
    titleBar.onClose = [this] { if (onClose) onClose(); };
    titleBar.onDone = [this] { setCustomizing (false); };
    header.onLayoutChanged = [this] { titleBar.setMenuWidth (header.coverRight()); };

    // ---- header
    header.onPlayPause = [this]
    {
        if (engine.currentFile() == juce::File()) { showOpenDialog(); return; }
        engine.togglePlay();
    };
    header.onPrev = [this] { skip (-1); };
    header.onNext = [this] { skip (1); };
    header.onOpen = [this] { showOpenDialog(); };
    header.onVolumeChanged = [this] (float v) { engine.setVolume (v); saveSettings(); };
    header.onQuickAction = [this] (int i, QuickAction a) { runQuickAction (i, a); };
    header.onQuickRightClick = [this] (int i) { showQuickMenu (i); };

    // ---- waveform
    waveform.onSeek = [this] (double s) { engine.setPosition (s); };
    waveform.onEmptyClick = [this] { showOpenDialog(); };
    waveform.onStyleChanged = [this] (WaveformView::Style) { saveSettings(); };
    {
        const int styleIndex = WaveformView::styleIds().indexOf (settings->getValue ("waveStyle", "bars"));
        if (! headless && styleIndex >= 0) waveform.setStyle ((WaveformView::Style) styleIndex, false);
    }

    // ---- video
    videoView.onTogglePlay = [this] { header.onPlayPause(); };
    videoView.onToggleFullscreen = [this] { setFullscreenVideo (! videoFullscreen); };
    videoView.onShowPlugins = [this] { setShowVideo (false); };
    videoView.onSeek = [this] (double s) { engine.setPosition (s); };
    showVideo = headless || settings->getBoolValue ("showVideo", true);

    // ---- slots
    slotGrid.onBrowseRequested = [this] (int index) { browser.open (index, slotGrid.getIds()); };
    slotGrid.onChanged = [this] { engine.setEffectChain (slotGrid.effectChain()); saveSettings(); };
    slotGrid.onStateChanged = [this] { settingsDirtyFor = 0.0f; };
    slotGrid.onCustomizeRequested = [this] { setCustomizing (! customizing); };
    browser.onPick = [this] (int index, const juce::String& id)
    {
        if (index < 0) slotGrid.addVisualizer (id);
        else slotGrid.replaceVisualizer (index, id);
    };

    // ---- restore settings
    auto slotIds = juce::StringArray::fromTokens (headless ? defaultSlots.joinIntoString (",") : settings->getValue ("slots", defaultSlots.joinIntoString (",")), ",", "");
    slotIds.removeEmptyStrings();
    juce::Array<juce::var> slotStates;
    if (! headless)
    {
        const auto parsed = juce::JSON::parse (settings->getValue ("slotStates", "[]"));   // keep the var alive while reading it
        if (auto* arr = parsed.getArray())
            slotStates = *arr;
    }
    juce::Array<int> slotWidths;
    if (! headless)
        for (auto& w : juce::StringArray::fromTokens (settings->getValue ("slotWidths"), ",", ""))
            slotWidths.add (w.getIntValue());
    slotGrid.setIds (slotIds, slotStates, slotWidths);

    if (! headless)
    {
        headerHeight = juce::jlimit (HeaderView::kMinHeight, HeaderView::kMaxHeight, settings->getIntValue ("headerHeight", HeaderView::kDefaultHeight));
        waveHeight = juce::jlimit (kMinWave, kMaxWave, settings->getIntValue ("waveHeight", 56));
        setUiScale ((float) settings->getDoubleValue ("uiScale", 1.0));
        taskbarPlayer = settings->getBoolValue ("taskbarPlayer", true);
    }
    engine.setEffectChain (slotGrid.effectChain());

    quickColumns = juce::jlimit (2, 4, settings->getIntValue ("quickColumns", 2));
    const auto quickIds = juce::StringArray::fromTokens (settings->getValue ("quick", "monitor,playlist,customize,dim"), ",", "");
    const QuickAction defaults[] = { QuickAction::monitor, QuickAction::playlist, QuickAction::customize, QuickAction::dim,
                                     QuickAction::abLoop, QuickAction::mono, QuickAction::open, QuickAction::compact };
    for (int i = 0; i < quickColumns * 2; ++i)
        quick.push_back (actionFromId (quickIds[i], defaults[i]));
    applyQuickActions();

    const float vol = (float) settings->getDoubleValue ("volume", 0.8);
    engine.setVolume (vol);
    header.setVolume (vol);

    // ---- audio
    engine.onSampleRateChanged = [this] (double sr) { analyzer.prepare (sr); };
    if (headless)
    {
        engine.prepareOffline (44100.0, 512);
        analyzer.prepare (44100.0);
    }
    else
    {
        if (auto error = engine.openDevice(); error.isNotEmpty())
            juce::MessageManager::callAsync ([this, error] { toast ("Audio device: " + error); });

        const juce::File last (settings->getValue ("lastFile"));
        if (last.existsAsFile())
            openFile (last, false);

        vblank = juce::VBlankAttachment (this, [this] (double ts) { onVBlank (ts); });

        if (defaultplayer::isSupported() && settings->getValue ("defaultPrompt") != "never")
        {
            juce::Component::SafePointer<MainComponent> safe (this);
            juce::Timer::callAfterDelay (1500, [safe] { if (safe != nullptr) safe->askToBecomeDefault(); });
        }
    }

    setWantsKeyboardFocus (true);
    setSize (1280, 520);
}

MainComponent::~MainComponent()
{
    ++coverToken;
    if (videoFullscreen && ! headless) juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
    video.close();
    saveSettings();
    vblank = {};
}

//==============================================================================
bool MainComponent::openFile (const juce::File& file, bool autoplay)
{
    if (! engine.load (file))
    {
        toast ("Can't open " + file.getFileName());
        return false;
    }

    const bool isVideo = media::isVideoFile (file);
    if (videoFullscreen && ! isVideo) setFullscreenVideo (false);
    videoLoaded = isVideo && video.open (file);
    if (! videoLoaded) video.close();
    ++coverToken;   // cancels a frame grab still running for the previous file

    const auto info = readTrackInfo (file);
    const auto& d = engine.details();

    juce::StringArray meta;
    const auto dot = utf8 ("  \xc2\xb7  ");
    auto kHz = [] (double sr) { return juce::String (sr / 1000.0, std::fmod (sr, 1000.0) != 0.0 ? 1 : 0) + " kHz"; };
    juce::String line;
    if (videoLoaded)
    {
        const auto& vi = video.info();
        const bool sideways = vi.rotation == 90 || vi.rotation == 270;
        line = d.formatName;
        if (vi.videoCodec.isNotEmpty()) line << dot << vi.videoCodec;
        line << dot << (sideways ? vi.height : vi.width) << utf8 ("\xc3\x97") << (sideways ? vi.width : vi.height);
        if (vi.fps > 0.0) line << dot << juce::String (vi.fps, std::abs (vi.fps - std::round (vi.fps)) > 0.01 ? 2 : 0) << " fps";
        if (vi.hasAudio) line << dot << (vi.audioCodec.isNotEmpty() ? vi.audioCodec + " " : juce::String()) << kHz (d.sampleRate);
        else line << dot << "no sound";
    }
    else
    {
        line = d.formatName + dot + kHz (d.sampleRate);
        if (file.hasFileExtension ("wav;aif;aiff;flac") && d.bitsPerSample > 0) line << dot << d.bitsPerSample << " bit";
        else if (d.kbps > 0) line << dot << d.kbps << " kbps";
    }
    line << dot << juce::String ((double) d.fileSize / (1024.0 * 1024.0), 1) << " MB";
    meta.add (line);

    // title and artist from tags, else from an "Artist - Title" file name
    juce::String title = info.title, artist = info.artist;
    if (title.isEmpty())
    {
        const auto name = file.getFileNameWithoutExtension();
        if (name.contains (" - ")) { artist = name.upToFirstOccurrenceOf (" - ", false, false).trim(); title = name.fromFirstOccurrenceOf (" - ", false, false).trim(); }
        else title = name;
    }
    header.setTrack (info.cover, title, artist, meta);
    currentTitle = title;
    currentArtist = artist;
    currentCover = info.cover;
    slotGrid.setCover (info.cover);
    videoView.setTrack (title, artist);
    if (videoLoaded && ! info.cover.isValid())
        loadVideoCover (file, video.info().duration);
    waveform.trackChanged (file);
    analyzer.resetTrack();
    loopA = loopB = -1.0;
    waveform.setLoop (loopA, loopB);

    playlist = siblingAudioFiles (file);
    playIndex = playlist.indexOf (file);

    if (auto* top = getTopLevelComponent())
        top->setName (info.displayName (file) + utf8 (" \xe2\x80\x94 Transport"));

    if (! headless) properties.getUserSettings()->setValue ("lastFile", file.getFullPathName());
    if (autoplay) engine.play();
    refreshQuickState();
    updateVideoLayout();
    return true;
}

void MainComponent::loadVideoCover (const juce::File& file, double duration)
{
    // a frame from a tenth into the video (at most 30 s in) as the cover; decoded off the UI thread
    const int token = coverToken;
    const double at = juce::jmin (30.0, duration * 0.1);
    juce::Component::SafePointer<MainComponent> safe (this);
    auto apply = [safe, token] (const juce::Image& img)
    {
        if (safe == nullptr || safe->coverToken != token || ! img.isValid()) return;
        safe->currentCover = img;
        safe->header.setCover (img);
        safe->slotGrid.setCover (img);
    };
    if (headless) { apply (VideoPlayer::grabFrame (file, at, 480)); return; }
    juce::Thread::launch ([apply, file, at]
    {
        auto img = VideoPlayer::grabFrame (file, at, 480);
        juce::MessageManager::callAsync ([apply, img] { apply (img); });
    });
}

void MainComponent::setShowVideo (bool s)
{
    showVideo = s;
    if (videoLoaded) toast (s ? juce::String ("Video") : utf8 ("Plugins \xc2\xb7 press V for the video"));
    refreshQuickState();
    saveSettings();
}

void MainComponent::setFullscreenVideo (bool on)
{
    if (on && ! videoLoaded) return;
    if (on == videoFullscreen) return;
    videoFullscreen = on;
    if (on) { showVideo = true; if (customizing) setCustomizing (false); }

    if (! headless)
        if (auto* top = getTopLevelComponent())
            juce::Desktop::getInstance().setKioskModeComponent (on ? top : nullptr, false);

    for (auto* c : std::initializer_list<juce::Component*> { &titleBar, &header, &waveform })
        c->setVisible (! on);
    videoView.setFullscreen (on);
    resized();
    refreshQuickState();
    grabKeyboardFocus();
}

bool MainComponent::syncVideoNow (int timeoutMs)
{
    if (! videoLoaded) return false;
    const bool ok = video.syncBlocking (engine.getSmoothPosition(), timeoutMs);
    videoView.pictureChanged();
    videoMix.snap (videoWanted() ? 1.0f : 0.0f);
    updateVideoLayout();
    return ok;
}

void MainComponent::updateVideoLayout()
{
    if (videoFullscreen)
    {
        videoView.setTransform ({});
        videoView.setBounds (getLocalBounds());
        videoView.setAlpha (1.0f);
        videoView.setVisible (true);
        videoView.toFront (false);
        slotGrid.setVisible (false);
        return;
    }

    const auto area = slotGrid.getBounds();
    const float m = videoMix.current;
    videoView.setBounds (area);
    videoView.setAlpha (m);
    const float scale = 0.985f + 0.015f * m;
    videoView.setTransform (juce::AffineTransform::scale (scale, scale, (float) area.getCentreX(), (float) area.getCentreY()));
    videoView.setVisible (m > 0.001f && area.getHeight() > 30);
    slotGrid.setAlpha (1.0f - m);
    slotGrid.setVisible (m < 0.999f && area.getHeight() > 30);
}

void MainComponent::onVBlank (double ts)
{
    const float dt = lastTimestamp > 0.0 ? (float) juce::jlimit (0.0, 0.1, ts - lastTimestamp) : 1.0f / 60.0f;
    lastTimestamp = ts;
    tickFrame (dt);
}

void MainComponent::tickFrame (float dt)
{
    // 1. drain audio for analysis
    int received = 0;
    for (int n; (n = engine.analysisFifo().pop (popL.data(), popR.data(), (int) popL.size())) > 0;)
    {
        analyzer.push (popL.data(), popR.data(), n);
        received += n;
    }
    if (received > 0) silenceDebt = 0.0f;
    else if ((silenceDebt += dt) > 0.05f)
    {
        // no audio flowing (no device / stopped): feed silence so meters fall smoothly
        const int n = juce::jmin ((int) popL.size(), (int) (silenceDebt * engine.getSampleRate()));
        std::fill (popL.begin(), popL.begin() + n, 0.0f);
        std::fill (popR.begin(), popR.begin() + n, 0.0f);
        analyzer.push (popL.data(), popR.data(), n);
        silenceDebt = 0.0f;
    }

    // 2. analysis + plugins
    analyzer.computeFrame (frame, dt);
    frame.positionSeconds = engine.getSmoothPosition();
    frame.lengthSeconds = engine.getLength();
    frame.playing = engine.isPlaying();
    slotGrid.updateFrame (frame, dt);
    engine.collectGarbage();

    if (settingsDirtyFor >= 0.0f && (settingsDirtyFor += dt) > 0.8f)
    {
        settingsDirtyFor = -1.0f;
        saveSettings();
    }

    // 3. transport bookkeeping
    const double pos = engine.getSmoothPosition();
    if (loopA >= 0.0 && loopB > loopA && engine.isPlaying() && pos >= loopB)
        engine.setPosition (loopA);

    if (engine.hasFinished() && engine.getLength() > 0.0 && ! headless)
    {
        if (playIndex >= 0 && playIndex + 1 < playlist.size()) skip (1);
        else { engine.pause(); engine.setPosition (0.0); }
    }

    if (videoLoaded)
    {
        // decode at the size the picture is shown (after the size has settled for a moment)
        const float scale = (float) juce::Component::getApproximateScaleFactorForComponent (&videoView);
        const auto px = juce::Point<int> (juce::roundToInt ((float) videoView.getWidth() * scale), juce::roundToInt ((float) videoView.getHeight() * scale));
        if (px != videoPixels) { videoPixels = px; videoPixelsAge = 0.0f; }
        else if (videoPixelsAge >= 0.0f && (videoPixelsAge += dt) > 0.25f)
        {
            videoPixelsAge = -1.0f;
            video.setDisplaySize (px.x, px.y);
        }

        if (video.sync (engine.getAudiblePosition()))
            videoView.pictureChanged();
        videoView.setTransport (engine.getSmoothPosition(), engine.getLength(), engine.isPlaying());
    }
    videoMix.target = videoWanted() ? 1.0f : 0.0f;
    if (videoMix.tick (dt, 9.0f)) updateVideoLayout();

    waveform.setPosition (engine.getSmoothPosition(), engine.getLength());
    header.setPlaying (engine.isPlaying());
    header.setTimeLine (engine.getLength() > 0.0 ? formatClock (engine.getSmoothPosition()) + " / " + formatClock (engine.getLength()) : juce::String());

    // 4. animations
    for (auto* a : std::initializer_list<Animated*> { &titleBar, &header, &waveform, &slotGrid, &browser, &videoView })
        a->animate (dt);
    splitHeader->animate (dt);
    splitWave->animate (dt);
    defaultPrompt->animate (dt);

    bool overlayDirty = dropGlow.tick (dt, 14.0f);
    if (toastTime > 0.0f && (toastTime -= dt) <= 0.0f) toastAlpha.target = 0.0f;
    overlayDirty |= toastAlpha.tick (dt, 12.0f);
    if (overlayDirty) repaint();

    if (animatingHeight)
    {
        windowHeight.tick (dt, 12.0f);
        if (auto* top = getTopLevelComponent())
            top->setSize (top->getWidth(), juce::roundToInt (windowHeight.current) + (top->getHeight() - getHeight()));
        if (! windowHeight.moving()) animatingHeight = false;
    }
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (dropGlow.current > 0.01f)
    {
        g.setColour (theme::accent.withAlpha (0.08f * dropGlow.current));
        g.fillRect (getLocalBounds());
        g.setColour (theme::accent.withAlpha (dropGlow.current));
        g.drawRect (getLocalBounds().toFloat().reduced (1.5f), 3.0f);

        auto pill = juce::Rectangle<float> (220, 44).withCentre (getLocalBounds().toFloat().getCentre());
        g.setColour (theme::titleBlock.withAlpha (0.92f * dropGlow.current));
        g.fillRoundedRectangle (pill, 22.0f);
        g.setColour (juce::Colours::white.withAlpha (dropGlow.current));
        g.setFont (theme::font (14.0f, true));
        g.drawText ("Drop to play", pill, juce::Justification::centred, false);
    }

    if (toastAlpha.current > 0.01f && toastText.isNotEmpty())
    {
        g.setFont (theme::font (12.5f, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), toastText) + 36.0f;
        auto r = juce::Rectangle<float> (w, 32).withCentre ({ (float) getWidth() * 0.5f, (float) waveform.getBottom() + 10.0f - 8.0f * (1.0f - toastAlpha.current) });
        g.setColour (theme::titleBlock.withAlpha (0.94f * toastAlpha.current));
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (theme::accent.withAlpha (toastAlpha.current));
        g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ r.getX() + 16.0f, r.getCentreY() }));
        g.setColour (juce::Colours::white.withAlpha (toastAlpha.current));
        g.drawText (toastText, r.withTrimmedLeft (24), juce::Justification::centredLeft, false);
    }
}

MainComponent::Heights MainComponent::effectiveHeights() const
{
    // the plugin area keeps at least ~45 % of the window: header and waveform give way first
    const int available = getHeight() - kTitleH;
    int h = headerHeight, w = waveHeight;
    const int reserve = compact ? 0 : juce::jmax (kMinPlugins, (int) (available * 0.45f));
    int excess = h + w + reserve - available;
    if (excess > 0) { const int cut = juce::jmin (excess, h - HeaderView::kMinHeight); h -= cut; excess -= cut; }
    if (excess > 0) { const int cut = juce::jmin (excess, w - kMinWave); w -= cut; }
    return { h, w };
}

void MainComponent::resized()
{
    const auto heights = effectiveHeights();
    auto r = getLocalBounds();
    titleBar.setBounds (r.removeFromTop (kTitleH));
    header.setBounds (r.removeFromTop (heights.header));
    waveform.setBounds (r.removeFromTop (heights.wave));
    slotGrid.setBounds (r.reduced (6, 0).withTrimmedTop (6).withTrimmedBottom (6));
    updateVideoLayout();
    splitHeader->setBounds (0, header.getBottom() - 5, getWidth(), 10);
    splitWave->setBounds (0, waveform.getBottom() - 2, getWidth(), 10);
    splitHeader->toFront (false);
    splitWave->toFront (false);
    browser.setBounds (getLocalBounds().withTrimmedTop (kTitleH));
    browser.toFront (false);
    const int promptW = juce::jmin (getWidth() - 24, 660);
    defaultPrompt->setBounds ((getWidth() - promptW) / 2, getHeight() - 58, promptW, 46);
    titleBar.setMenuWidth (header.coverRight());
    if (auto* top = getTopLevelComponent())
        titleBar.setMaximised (top->isOnDesktop() && dynamic_cast<juce::ResizableWindow*> (top) != nullptr
                               && static_cast<juce::ResizableWindow*> (top)->isFullScreen());
}

juce::Component::WindowControlKind MainComponent::controlAt (juce::Point<float> p) const
{
    if (! videoFullscreen && p.y < (float) kTitleH)
    {
        switch (titleBar.hitAt (p))
        {
            case TitleBar::Hit::caption:  return WindowControlKind::caption;
            case TitleBar::Hit::minimise: return WindowControlKind::minimise;
            case TitleBar::Hit::maximise: return WindowControlKind::maximise;
            case TitleBar::Hit::close:    return WindowControlKind::close;
            case TitleBar::Hit::menu:
            case TitleBar::Hit::done:
            case TitleBar::Hit::none:     break;
        }
    }
    return WindowControlKind::client;
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (k == juce::KeyPress::spaceKey)       { header.onPlayPause(); return true; }
    if (k == juce::KeyPress::leftKey)        { engine.setPosition (engine.getSmoothPosition() - (mods.isShiftDown() ? 30.0 : 5.0)); return true; }
    if (k == juce::KeyPress::rightKey)       { engine.setPosition (engine.getSmoothPosition() + (mods.isShiftDown() ? 30.0 : 5.0)); return true; }
    if (k == juce::KeyPress::upKey)          { engine.setVolume (engine.getVolume() + 0.05f); header.setVolume (engine.getVolume()); return true; }
    if (k == juce::KeyPress::downKey)        { engine.setVolume (engine.getVolume() - 0.05f); header.setVolume (engine.getVolume()); return true; }
    if (k == juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0)) { showOpenDialog(); return true; }
    if (k.getTextCharacter() == 'n')         { skip (1); return true; }
    if (k.getTextCharacter() == 'p')         { skip (-1); return true; }
    if (k.getTextCharacter() == 'm')         { runQuickAction (-1, QuickAction::mono); return true; }
    if (k.getTextCharacter() == 'l')         { runQuickAction (-1, QuickAction::abLoop); return true; }
    if (k.getTextCharacter() == 'v' && videoLoaded) { setShowVideo (! showVideo); return true; }
    if (k.getTextCharacter() == 'f' && videoLoaded) { setFullscreenVideo (! videoFullscreen); return true; }
    if (k == juce::KeyPress::escapeKey && videoFullscreen)  { setFullscreenVideo (false); return true; }
    if (k == juce::KeyPress::escapeKey && browser.isOpen()) { browser.close(); return true; }
    if (k == juce::KeyPress::escapeKey && customizing)      { setCustomizing (false); return true; }
    if (k == juce::KeyPress::escapeKey && slotGrid.restoreMaximised()) return true;
    return false;
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (juce::File (f))) return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dropGlow.target = 0.0f;
    for (auto& f : files)
        if (isAudioFile (juce::File (f)))
        {
            openFile (juce::File (f), true);
            return;
        }
}

//==============================================================================
void MainComponent::showOpenDialog()
{
    chooser = std::make_unique<juce::FileChooser> (media::isSupported() ? "Open a track or video" : "Open a track",
                                                   engine.currentFile().getParentDirectory(), playableWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  openFile (fc.getResult(), true);
                          });
}

void MainComponent::skip (int direction)
{
    if (playlist.isEmpty()) return;
    if (direction < 0 && engine.getSmoothPosition() > 3.0) { engine.setPosition (0.0); return; }

    const int next = playIndex + direction;
    if (juce::isPositiveAndBelow (next, playlist.size()))
    {
        const bool wasPlaying = engine.isPlaying() || engine.hasFinished();
        openFile (playlist[next], wasPlaying);
    }
}

void MainComponent::showMainMenu()
{
    juce::PopupMenu m, quickGrid, waveStyles, sizes;
    m.addItem (1, utf8 ("Open file\xe2\x80\xa6"), true, false);
    m.addSeparator();
    m.addItem (2, customizing ? "Done customizing" : utf8 ("Customize layout\xe2\x80\xa6"), true, customizing);
    m.addItem (8, utf8 ("Add plugin\xe2\x80\xa6"));
    if (videoLoaded)
    {
        m.addItem (11, "Show video", true, showVideo);
        m.addItem (12, "Full screen video", true, videoFullscreen);
    }
    for (int i = 0; i < WaveformView::styleNames().size(); ++i)
        waveStyles.addItem (300 + i, WaveformView::styleNames()[i], true, (int) waveform.getStyle() == i);
    m.addSubMenu ("Waveform style", waveStyles);
    for (int c = 2; c <= 4; ++c) quickGrid.addItem (200 + c, juce::String (c) + utf8 (" \xc3\x97 2"), true, c == quickColumns);
    m.addSubMenu ("Quick-access grid", quickGrid);
    const float scales[] = { 0.9f, 1.0f, 1.15f, 1.3f };
    const char* scaleNames[] = { "Small", "Medium", "Large", "Extra large" };
    for (int i = 0; i < 4; ++i) sizes.addItem (400 + i, scaleNames[i], true, std::abs (uiScale - scales[i]) < 0.01f);
    m.addSubMenu ("Display size", sizes);
    m.addItem (3, "Reset layout");
    m.addSeparator();
    m.addItem (4, utf8 ("Audio output\xe2\x80\xa6"));
    if (defaultplayer::isSupported())
        m.addItem (10, utf8 ("Set as default audio player\xe2\x80\xa6"), true, defaultplayer::isDefault());
    m.addItem (5, "Compact mode", true, compact);
   #if JUCE_WINDOWS
    m.addItem (9, "Taskbar mini player", true, taskbarPlayer);
   #endif
    m.addSeparator();
    m.addItem (6, "About Transport");
    m.addItem (7, "Quit");

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&titleBar).withTargetScreenArea (
                         titleBar.getScreenBounds().withWidth (header.coverRight())),
                     [safe, scales] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& self = *safe;
        if (r == 1) self.showOpenDialog();
        else if (r == 2) self.setCustomizing (! self.customizing);
        else if (r == 8) self.browser.open (-1, self.slotGrid.getIds());
        else if (r == 3)
        {
            self.headerHeight = HeaderView::kDefaultHeight;
            self.waveHeight = 56;
            self.slotGrid.setIds (defaultSlots);
            self.engine.setEffectChain (self.slotGrid.effectChain());
            self.resized();
            self.saveSettings();
        }
        else if (r == 4) self.showAudioSettings();
        else if (r == 5) self.setCompact (! self.compact);
        else if (r == 6) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Transport " JUCE_APPLICATION_VERSION_STRING,
                                                                 "A music player that shows you what you hear.\nBuilt with JUCE.");
        else if (r == 7) { if (self.onClose) self.onClose(); }
        else if (r == 10) self.toast (defaultplayer::makeDefault());
        else if (r == 11) self.setShowVideo (! self.showVideo);
        else if (r == 12) self.setFullscreenVideo (! self.videoFullscreen);
        else if (r == 9)
        {
            self.taskbarPlayer = ! self.taskbarPlayer;
            if (self.onTaskbarPlayerChanged) self.onTaskbarPlayerChanged (self.taskbarPlayer);
            self.toast (self.taskbarPlayer ? "Hover the taskbar icon for the mini player" : "Taskbar mini player off");
            self.saveSettings();
        }
        else if (r >= 400) { self.setUiScale (scales[r - 400]); self.saveSettings(); }
        else if (r >= 300) { self.waveform.setStyle ((WaveformView::Style) (r - 300)); self.saveSettings(); }
        else if (r > 200) { self.quickColumns = r - 200; self.quick.resize ((size_t) self.quickColumns * 2, QuickAction::open); self.applyQuickActions(); self.saveSettings(); }
    });
}

void MainComponent::setCustomizing (bool c)
{
    customizing = c;
    slotGrid.setCustomizing (c);
    titleBar.setCustomizing (c);
    splitHeader->shown.target = c ? 1.0f : 0.0f;
    splitWave->shown.target = c ? 1.0f : 0.0f;
    if (c) toast (utf8 ("Drag slots to reorder \xc2\xb7 drag the orange handles to resize areas"));
    refreshQuickState();
}

void MainComponent::setUiScale (float s)
{
    uiScale = juce::jlimit (0.75f, 1.5f, s);
    if (! headless)
        juce::Desktop::getInstance().setGlobalScaleFactor (uiScale);
}

void MainComponent::applyQuickActions()
{
    // never show the same action twice
    for (size_t i = 0; i < quick.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (quick[i] == quick[j])
                for (auto& info : quickActions())
                    if (std::find (quick.begin(), quick.end(), info.action) == quick.end()) { quick[i] = info.action; break; }

    header.setQuickActions (quick, quickColumns);
    refreshQuickState();
}

void MainComponent::refreshQuickState()
{
    auto& f = engine.monitor();
    header.setQuickActive (QuickAction::monitor, f.mono || f.swap || f.soloLeft || f.soloRight || f.mid || f.side || f.bypass);
    header.setQuickActive (QuickAction::mono, f.mono);
    header.setQuickActive (QuickAction::swap, f.swap);
    header.setQuickActive (QuickAction::soloLeft, f.soloLeft);
    header.setQuickActive (QuickAction::soloRight, f.soloRight);
    header.setQuickActive (QuickAction::mid, f.mid);
    header.setQuickActive (QuickAction::side, f.side);
    header.setQuickActive (QuickAction::bypass, f.bypass);
    header.setQuickActive (QuickAction::dim, f.dim);
    header.setQuickActive (QuickAction::abLoop, loopA >= 0.0);
    header.setQuickActive (QuickAction::compact, compact);
    header.setQuickActive (QuickAction::customize, customizing);
    header.setQuickActive (QuickAction::video, videoLoaded && showVideo);
    header.setQuickActive (QuickAction::fullscreen, videoFullscreen);
    if (auto* top = getTopLevelComponent())
        header.setQuickActive (QuickAction::alwaysOnTop, top->isAlwaysOnTop());
}

void MainComponent::runQuickAction (int, QuickAction a)
{
    auto& f = engine.monitor();
    auto flip = [this] (std::atomic<bool>& flag, const char* on, const char* off) { flag = ! flag.load(); toast (flag ? on : off); };

    switch (a)
    {
        case QuickAction::monitor:       showMonitorMenu(); break;
        case QuickAction::playlist:      showPlaylistMenu(); break;
        case QuickAction::addVisualizer: browser.open (-1, slotGrid.getIds()); break;
        case QuickAction::open:          showOpenDialog(); break;
        case QuickAction::dim:           flip (f.dim, "Dim -20 dB", "Dim off"); break;
        case QuickAction::mono:          flip (f.mono, "Mono", "Stereo"); break;
        case QuickAction::swap:          flip (f.swap, "Left and right swapped", "Swap off"); break;
        case QuickAction::soloLeft:      flip (f.soloLeft, "Left channel only", "Solo left off"); break;
        case QuickAction::soloRight:     flip (f.soloRight, "Right channel only", "Solo right off"); break;
        case QuickAction::mid:           flip (f.mid, "Mid only", "Mid solo off"); break;
        case QuickAction::side:          flip (f.side, "Side only", "Side solo off"); break;
        case QuickAction::bypass:        flip (f.bypass, "Bypass: untouched signal", "Bypass off"); break;
        case QuickAction::abLoop:
        {
            const double pos = engine.getSmoothPosition();
            if (loopA < 0.0)                        { loopA = pos; toast (utf8 ("Loop start set \xc2\xb7 click again for the end")); }
            else if (loopB < 0.0 && pos > loopA + 0.2) { loopB = pos; toast ("Looping " + formatClock (loopA) + utf8 (" \xe2\x80\x93 ") + formatClock (loopB)); engine.setPosition (loopA); }
            else                                    { loopA = loopB = -1.0; toast ("Loop cleared"); }
            waveform.setLoop (loopA, loopB);
            break;
        }
        case QuickAction::alwaysOnTop:
            if (auto* top = getTopLevelComponent())
            {
                top->setAlwaysOnTop (! top->isAlwaysOnTop());
                toast (top->isAlwaysOnTop() ? "Always on top" : "Normal window");
            }
            break;
        case QuickAction::compact:  setCompact (! compact); break;
        case QuickAction::customize: setCustomizing (! customizing); break;
        case QuickAction::snapshot: saveSnapshot(); break;
        case QuickAction::video:
            if (videoLoaded) setShowVideo (! showVideo);
            else toast ("Open a video to show it here");
            break;
        case QuickAction::fullscreen:
            if (videoLoaded) setFullscreenVideo (! videoFullscreen);
            else toast ("Open a video first");
            break;
        case QuickAction::count:    break;
    }
    refreshQuickState();
}

void MainComponent::showMonitorMenu()
{
    auto& f = engine.monitor();
    juce::PopupMenu m;
    m.addSectionHeader ("Monitoring");
    m.addItem (1, "Mono", true, f.mono);
    m.addItem (2, "Swap L / R", true, f.swap);
    m.addItem (3, "Solo left", true, f.soloLeft);
    m.addItem (4, "Solo right", true, f.soloRight);
    m.addItem (5, "Mid only", true, f.mid);
    m.addItem (6, "Side only", true, f.side);
    m.addSeparator();
    m.addItem (7, "Bypass everything", true, f.bypass);
    m.addItem (8, "Reset");

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& fl = safe->engine.monitor();
        const QuickAction map[] = { QuickAction::mono, QuickAction::swap, QuickAction::soloLeft, QuickAction::soloRight, QuickAction::mid, QuickAction::side, QuickAction::bypass };
        if (r == 8)
        {
            for (auto* b : { &fl.mono, &fl.swap, &fl.soloLeft, &fl.soloRight, &fl.mid, &fl.side, &fl.bypass }) *b = false;
            safe->toast ("Monitoring reset");
            safe->refreshQuickState();
        }
        else safe->runQuickAction (-1, map[r - 1]);
    });
}

void MainComponent::showPlaylistMenu()
{
    juce::PopupMenu m;
    m.addItem (1, utf8 ("Open file\xe2\x80\xa6"));
    if (! playlist.isEmpty())
    {
        m.addSectionHeader (engine.currentFile().getParentDirectory().getFileName());
        for (int i = 0; i < playlist.size() && i < 400; ++i)
            m.addItem (100 + i, playlist[i].getFileNameWithoutExtension(), true, i == playIndex);
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) safe->showOpenDialog();
        else if (r >= 100) safe->openFile (safe->playlist[r - 100], true);
    });
}

void MainComponent::showQuickMenu (int index)
{
    juce::PopupMenu m, grid;
    m.addSectionHeader ("Button " + juce::String (index + 1));
    for (auto& info : quickActions())
        m.addItem (1 + (int) info.action, utf8 (info.name), true, quick[(size_t) index] == info.action);
    m.addSeparator();
    for (int c = 2; c <= 4; ++c) grid.addItem (200 + c, juce::String (c) + utf8 (" \xc3\x97 2"), true, c == quickColumns);
    m.addSubMenu ("Grid size", grid);

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, index] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& self = *safe;
        if (r >= 200)
        {
            self.quickColumns = r - 200;
            self.quick.resize ((size_t) self.quickColumns * 2, QuickAction::open);
        }
        else
        {
            const auto chosen = (QuickAction) (r - 1);
            // swap with the button that already had it, so nothing disappears
            for (auto& q : self.quick) if (q == chosen) q = self.quick[(size_t) index];
            self.quick[(size_t) index] = chosen;
        }
        self.applyQuickActions();
        self.saveSettings();
    });
}

void MainComponent::setCompact (bool c)
{
    auto* top = getTopLevelComponent();
    if (top == nullptr || c == compact) return;
    compact = c;
    if (compact) expandedHeight = getHeight();
    windowHeight.snap ((float) getHeight());
    windowHeight.target = (float) (compact ? kTitleH + effectiveHeights().header + effectiveHeights().wave : juce::jmax (360, expandedHeight));
    animatingHeight = true;
    refreshQuickState();
}

juce::Image MainComponent::renderMiniPlayer (int width, int height)
{
    juce::Image img (juce::Image::ARGB, width, height, true);
    juce::Graphics g (img);
    auto r = img.getBounds().toFloat();
    g.setColour (theme::header);
    g.fillRect (r);

    auto art = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.08f);
    if (currentCover.isValid())
        g.drawImage (currentCover, art, juce::RectanglePlacement::fillDestination);
    else
    {
        g.setColour (juce::Colour (0xff1c1c1c));
        g.fillEllipse (art);
        g.setColour (theme::accent);
        g.fillEllipse (art.withSizeKeepingCentre (art.getWidth() * 0.3f, art.getHeight() * 0.3f));
    }

    auto text = r.reduced (r.getHeight() * 0.08f, r.getHeight() * 0.1f);
    const float px = juce::jlimit (9.0f, 22.0f, text.getHeight() * 0.2f);
    g.setColour (theme::text);
    g.setFont (theme::font (px, true));
    g.drawText (currentTitle.isNotEmpty() ? currentTitle : juce::String ("Transport"), text.removeFromTop (px * 1.5f), juce::Justification::centredLeft, true);
    g.setColour (juce::Colour (0xffc8c8c8));
    g.setFont (theme::font (px * 0.8f));
    g.drawText (currentArtist, text.removeFromTop (px * 1.3f), juce::Justification::centredLeft, true);

    auto bottom = text.removeFromBottom (px * 1.2f);
    const double len = engine.getLength(), pos = engine.getSmoothPosition();
    g.setFont (theme::mono (px * 0.75f, false));
    g.drawText (formatClock (pos).fromFirstOccurrenceOf (":", false, false) + " / " + formatClock (len).fromFirstOccurrenceOf (":", false, false),
                bottom, juce::Justification::centredRight, false);
    auto bar = text.removeFromBottom (px * 0.5f).withHeight (juce::jmax (2.0f, px * 0.18f));
    g.setColour (juce::Colour (0xff595959));
    g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
    g.setColour (theme::accent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) (len > 0.0 ? pos / len : 0.0)), bar.getHeight() * 0.5f);
    return img;
}

juce::String MainComponent::miniPlayerKey()
{
    return currentTitle + (engine.isPlaying() ? "|play|" : "|pause|") + juce::String ((int) engine.getSmoothPosition());
}

void MainComponent::askToBecomeDefault()
{
    if (! defaultplayer::isDefault())
    {
        defaultPrompt->toFront (false);
        defaultPrompt->shown.target = 1.0f;
    }
}

void MainComponent::saveSnapshot()
{
    auto img = createComponentSnapshot (getLocalBounds(), true, 2.0f);
    auto dir = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
    auto file = dir.getNonexistentChildFile ("Transport " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"), ".png");
    juce::PNGImageFormat png;
    if (auto out = file.createOutputStream(); out != nullptr && png.writeImageToStream (img, *out))
        toast ("Snapshot saved to Desktop");
    else
        toast ("Couldn't save snapshot");
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.devices(), 0, 0, 2, 2, false, false, true, false);
    selector->setSize (520, 360);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio output";
    o.dialogBackgroundColour = theme::dialog;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::toast (const juce::String& text)
{
    toastText = text;
    toastTime = 1.8f;
    toastAlpha.target = 1.0f;
}

void MainComponent::saveSettings()
{
    if (headless) return;   // snapshot / test runs never touch the user's settings
    auto* s = properties.getUserSettings();
    s->setValue ("slots", slotGrid.getIds().joinIntoString (","));
    juce::StringArray widths;
    for (int w : slotGrid.getWidths()) widths.add (juce::String (w));
    s->setValue ("slotWidths", widths.joinIntoString (","));
    s->setValue ("headerHeight", headerHeight);
    s->setValue ("waveHeight", waveHeight);
    s->setValue ("uiScale", (double) uiScale);
    s->setValue ("taskbarPlayer", taskbarPlayer);
    s->setValue ("showVideo", showVideo);
    s->setValue ("waveStyle", WaveformView::styleIds()[(int) waveform.getStyle()]);
    s->setValue ("slotStates", juce::JSON::toString (juce::var (slotGrid.getStates()), true));
    juce::StringArray ids;
    for (auto q : quick) ids.add (infoFor (q).id);
    s->setValue ("quick", ids.joinIntoString (","));
    s->setValue ("quickColumns", quickColumns);
    s->setValue ("volume", (double) engine.getVolume());
    s->saveIfNeeded();
}

juce::String MainComponent::formatClock (double seconds) const
{
    const int t = juce::jmax (0, (int) seconds);
    return juce::String::formatted ("%02d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
}
} // namespace tp
