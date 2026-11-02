#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tp
{
/** Windows taskbar mini player: hovering the taskbar button shows a custom preview
    (cover, title, progress) with previous / play-pause / next buttons underneath.
    On other platforms this is a no-op. */
class TaskbarPlayer
{
public:
    explicit TaskbarPlayer (juce::Component& topLevelWindow);
    ~TaskbarPlayer();

    static bool isSupported();

    void setEnabled (bool);

    std::function<void()> onPrev, onPlayPause, onNext;
    std::function<juce::Image (int width, int height)> renderPreview;   // the hover thumbnail
    std::function<bool()> isPlaying;
    std::function<juce::String()> stateKey;   // changes when the preview should be redrawn

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace tp
