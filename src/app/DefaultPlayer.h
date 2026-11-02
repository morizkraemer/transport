#pragma once

#include <juce_core/juce_core.h>

namespace tp::defaultplayer
{
/** True on Windows and macOS. */
bool isSupported();

/** Whether Transport is already the default app for MP3 files. */
bool isDefault();

/** Windows: registers Transport as an audio player (per user) and opens the Default Apps settings
    page for it, where the user confirms. macOS: becomes the default viewer for common audio types.
    Returns a short message for the user. */
juce::String makeDefault();

/** Windows: if Transport registered its file types before, re-registers them so they point at this
    .exe and show its per-type icons (after an update or a move). Elsewhere a no-op. */
void refresh();
}
