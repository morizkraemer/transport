#include "MainComponent.h"
#include "TrackInfo.h"

namespace tp
{
namespace
{
constexpr int kTitleH = 34, kHeaderH = 150, kWaveH = 66;
constexpr int kCompactHeight = kTitleH + kHeaderH + kWaveH;

const juce::StringArray defaultSlots { "vu", "polar" };
}

MainComponent::MainComponent (bool isHeadless)
    : headless (isHeadless), waveform (engine.thumbnail())
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
    addChildComponent (browser);

    // ---- title bar
    titleBar.onMenu = [this] { showMainMenu(); };
    titleBar.onMinimise = [this] { if (onMinimise) onMinimise(); };
    titleBar.onMaximise = [this] { if (onMaximise) onMaximise(); };
    titleBar.onClose = [this] { if (onClose) onClose(); };

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

    // ---- slots
    slotGrid.onBrowseRequested = [this] (int index) { browser.open (index, slotGrid.getIds()); };
    slotGrid.onChanged = [this] { saveSettings(); };
    slotGrid.onRowsChanged = [this] (int rows) { growForRows (rows); };
    browser.onPick = [this] (int index, const juce::String& id)
    {
        if (index < 0) slotGrid.addVisualizer (id);
        else slotGrid.replaceVisualizer (index, id);
    };

    // ---- restore settings
    auto slotIds = juce::StringArray::fromTokens (headless ? defaultSlots.joinIntoString (",") : settings->getValue ("slots", defaultSlots.joinIntoString (",")), ",", "");
    slotIds.removeEmptyStrings();
    slotGrid.setColumns (headless ? 3 : settings->getIntValue ("columns", 3));
    slotGrid.setIds (slotIds);

    quickColumns = juce::jlimit (2, 4, settings->getIntValue ("quickColumns", 2));
    const auto quickIds = juce::StringArray::fromTokens (settings->getValue ("quick", "monitor,playlist,addvis,dim"), ",", "");
    const QuickAction defaults[] = { QuickAction::monitor, QuickAction::playlist, QuickAction::addVisualizer, QuickAction::dim,
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

       #if JUCE_MAC
        openGL.setComponentPaintingEnabled (true);
        openGL.setContinuousRepainting (false);
        openGL.attachTo (*this);
       #endif
        vblank = juce::VBlankAttachment (this, [this] (double ts) { onVBlank (ts); });
    }

    setWantsKeyboardFocus (true);
    setSize (1400, 560);
}

MainComponent::~MainComponent()
{
    saveSettings();
    vblank = {};
    openGL.detach();
}

//==============================================================================
bool MainComponent::openFile (const juce::File& file, bool autoplay)
{
    if (! engine.load (file))
    {
        toast ("Can't open " + file.getFileName());
        return false;
    }

    const auto info = readTrackInfo (file);
    const auto& d = engine.details();

    juce::StringArray meta;
    juce::String line = d.formatName + " - " + juce::String (d.sampleRate / 1000.0, std::fmod (d.sampleRate, 1000.0) != 0.0 ? 1 : 0) + " kHz";
    if (file.hasFileExtension ("wav;aif;aiff;flac") && d.bitsPerSample > 0) line << " - " << d.bitsPerSample << " bit";
    else if (d.kbps > 0) line << " - " << d.kbps << " kbps";
    meta.add (line);
    meta.add (juce::String ((double) d.fileSize / (1024.0 * 1024.0), 2) + " MB");

    header.setTrack (info.cover, info.displayName (file), meta);
    waveform.trackChanged();
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
    return true;
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
    slotGrid.updateFrame (frame, dt);

    // 3. transport bookkeeping
    const double pos = engine.getSmoothPosition();
    if (loopA >= 0.0 && loopB > loopA && engine.isPlaying() && pos >= loopB)
        engine.setPosition (loopA);

    if (engine.hasFinished() && engine.getLength() > 0.0 && ! headless)
    {
        if (playIndex >= 0 && playIndex + 1 < playlist.size()) skip (1);
        else { engine.pause(); engine.setPosition (0.0); }
    }

    waveform.setPosition (engine.getSmoothPosition(), engine.getLength());
    header.setPlaying (engine.isPlaying());
    header.setTimeLine (engine.getLength() > 0.0 ? formatClock (engine.getSmoothPosition()) + " / " + formatClock (engine.getLength()) : juce::String());

    // 4. animations
    for (auto* a : std::initializer_list<Animated*> { &titleBar, &header, &waveform, &slotGrid, &browser })
        a->animate (dt);

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

void MainComponent::resized()
{
    auto r = getLocalBounds();
    titleBar.setBounds (r.removeFromTop (kTitleH));
    header.setBounds (r.removeFromTop (kHeaderH));
    waveform.setBounds (r.removeFromTop (kWaveH));
    slotGrid.setBounds (r.reduced (6, 0).withTrimmedTop (8).withTrimmedBottom (6));
    slotGrid.setVisible (slotGrid.getHeight() > 40);
    browser.setBounds (getLocalBounds().withTrimmedTop (kTitleH));
}

juce::Component::WindowControlKind MainComponent::controlAt (juce::Point<float> p) const
{
    if (p.y < (float) kTitleH)
    {
        switch (titleBar.hitAt (p))
        {
            case TitleBar::Hit::caption:  return WindowControlKind::caption;
            case TitleBar::Hit::minimise: return WindowControlKind::minimise;
            case TitleBar::Hit::maximise: return WindowControlKind::maximise;
            case TitleBar::Hit::close:    return WindowControlKind::close;
            case TitleBar::Hit::menu:
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
    if (k == juce::KeyPress::escapeKey && browser.isOpen()) { browser.close(); return true; }
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
    chooser = std::make_unique<juce::FileChooser> ("Open a track", engine.currentFile().getParentDirectory(),
                                                   "*.mp3;*.wav;*.flac;*.aif;*.aiff;*.ogg;*.m4a;*.aac;*.wma");
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
    juce::PopupMenu m, cols, quickGrid;
    m.addItem (1, utf8 ("Open file\xe2\x80\xa6"), true, false);
    m.addSeparator();
    m.addItem (2, utf8 ("Add plugin\xe2\x80\xa6"));
    for (int c = 1; c <= 6; ++c) cols.addItem (100 + c, juce::String (c), true, c == slotGrid.getColumns());
    m.addSubMenu ("Plugin columns", cols);
    for (int c = 2; c <= 4; ++c) quickGrid.addItem (200 + c, juce::String (c) + utf8 (" \xc3\x97 2"), true, c == quickColumns);
    m.addSubMenu ("Quick-access grid", quickGrid);
    m.addItem (3, "Reset layout");
    m.addSeparator();
    m.addItem (4, utf8 ("Audio output\xe2\x80\xa6"));
    m.addItem (5, "Compact mode", true, compact);
    m.addSeparator();
    m.addItem (6, "About Transport");
    m.addItem (7, "Quit");

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&titleBar).withTargetScreenArea (
                         titleBar.getScreenBounds().withWidth (150)),
                     [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& self = *safe;
        if (r == 1) self.showOpenDialog();
        else if (r == 2) self.browser.open (-1, self.slotGrid.getIds());
        else if (r == 3) { self.slotGrid.setColumns (3); self.slotGrid.setIds (defaultSlots); self.saveSettings(); }
        else if (r == 4) self.showAudioSettings();
        else if (r == 5) self.setCompact (! self.compact);
        else if (r == 6) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Transport " JUCE_APPLICATION_VERSION_STRING,
                                                                 "A music player that shows you what you hear.\nBuilt with JUCE.");
        else if (r == 7) { if (self.onClose) self.onClose(); }
        else if (r > 200) { self.quickColumns = r - 200; self.quick.resize ((size_t) self.quickColumns * 2, QuickAction::open); self.applyQuickActions(); self.saveSettings(); }
        else if (r > 100) self.slotGrid.setColumns (r - 100);
    });
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
        case QuickAction::snapshot: saveSnapshot(); break;
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
    windowHeight.target = (float) (compact ? kCompactHeight : juce::jmax (420, expandedHeight));
    animatingHeight = true;
    refreshQuickState();
}

void MainComponent::growForRows (int rows)
{
    auto* top = getTopLevelComponent();
    if (top == nullptr || compact || headless) return;

    const int wanted = kCompactHeight + 14 + rows * 200 + (rows - 1) * 8;
    if (getHeight() >= wanted) return;

    int available = wanted;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (top->getScreenBounds()))
        available = display->userArea.getBottom() - top->getScreenY() - (top->getHeight() - getHeight());

    const int target = juce::jmin (wanted, available);
    if (target <= getHeight()) return;
    windowHeight.snap ((float) getHeight());
    windowHeight.target = (float) target;
    animatingHeight = true;
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
    s->setValue ("columns", slotGrid.getColumns());
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
