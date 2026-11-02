// Platforms without a video backend: only JUCE's own audio formats.
#include "Media.h"

namespace tp::media::backend
{
bool isSupported() { return false; }
juce::StringArray videoExtensions() { return {}; }
juce::StringArray audioExtensions() { return {}; }
bool probe (const juce::File&, StreamInfo&) { return false; }
std::unique_ptr<AudioDecoder> openAudio (const juce::File&) { return nullptr; }
std::unique_ptr<VideoDecoder> openVideo (const juce::File&) { return nullptr; }
}
