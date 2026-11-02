#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Theme.h"

namespace tp
{
/** The waveform strip: several looks, glowing playhead, hover time readout, drag-to-seek, A-B region.
    A background job splits the track into low/mid/high bands for the colour-coded styles. */
class WaveformView final : public juce::Component, public Animated, private juce::ChangeListener
{
public:
    enum class Style { bars, mirror, reflection, line, spectral, threeBand };
    static const juce::StringArray& styleNames();
    static const juce::StringArray& styleIds();

    struct Bin { float peak = 0, low = 0, mid = 0, high = 0; };
    using BandData = std::vector<Bin>;

    WaveformView (juce::AudioThumbnail& thumbnail, juce::AudioFormatManager& formats);
    ~WaveformView() override;

    void trackChanged (const juce::File& file);   // restarts the reveal animation and the band analysis
    void setPosition (double seconds, double length);
    void setLoop (double a, double b);             // negative = unset
    void refreshBars();

    void setStyle (Style s, bool animate = true);
    Style getStyle() const { return style; }

    /** Splits a file into peak + low/mid/high energy per bin. Blocking; used by the background job and tests. */
    static std::shared_ptr<const BandData> analyse (const juce::File&, juce::AudioFormatManager&, const std::function<bool()>& shouldStop);
    void setBandData (std::shared_ptr<const BandData> data);
    bool hasBandData() const { return bandData != nullptr; }

    std::function<void (double seconds)> onSeek;
    std::function<void()> onEmptyClick;
    std::function<void (Style)> onStyleChanged;

    void animate (float dt) override;
    void paint (juce::Graphics&) override;
    void resized() override { refreshBars(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class AnalysisJob;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshBars(); }
    juce::Rectangle<float> barArea() const { return getLocalBounds().toFloat().reduced (14, 6); }
    double xToTime (float x) const;
    float timeToX (double t) const;
    void paintStyle (juce::Graphics&, Style, float alpha, float playX);
    void showStyleMenu();

    juce::AudioThumbnail& thumb;
    juce::AudioFormatManager& formats;
    juce::ThreadPool pool { 1 };
    int generation = 0;
    std::shared_ptr<const BandData> bandData;

    std::vector<Bin> bars;
    double length = 0.0, position = 0.0, loopA = -1.0, loopB = -1.0;
    Smoothed shownPos;
    float reveal = 1.0f;
    Smoothed hover, loopAlpha, dragging, styleFade, bandFade;
    Style style = Style::bars, previousStyle = Style::bars;
    float hoverX = 0.0f;
    bool scrubbing = false;
};
} // namespace tp
