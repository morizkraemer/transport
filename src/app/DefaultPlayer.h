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
}
