#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tp
{
struct TrackInfo
{
    juce::String title, artist, album;
    juce::Image cover;

    /** "Artist - Title" when tags exist, otherwise the file name. */
    juce::String displayName (const juce::File& file) const;
};

/** Reads ID3v2 (MP3) or FLAC tags and embedded cover art; falls back to cover/folder images. */
TrackInfo readTrackInfo (const juce::File& file);

/** Anything Transport can play: audio, and video where the platform can decode it. */
bool isAudioFile (const juce::File& file);
/** "*.mp3;*.wav;..." for file choosers. */
juce::String playableWildcard();
juce::Array<juce::File> siblingAudioFiles (const juce::File& file);
} // namespace tp
