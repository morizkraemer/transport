#include "DefaultPlayer.h"
#include "media/Media.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shlobj.h>
 #include <shellapi.h>
#elif JUCE_MAC
 #include <CoreServices/CoreServices.h>
#endif

namespace tp::defaultplayer
{
namespace
{
const char* kExtensions[] = { ".mp3", ".wav", ".flac", ".aif", ".aiff", ".ogg", ".m4a", ".aac", ".wma" };
const juce::String kProgId = "Transport.AudioFile";
}

#if JUCE_WINDOWS

bool isSupported() { return true; }

bool isDefault()
{
    const auto choice = juce::WindowsRegistry::getValue (
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.mp3\\UserChoice\\ProgId");
    return choice == kProgId;
}

juce::String makeDefault()
{
    using R = juce::WindowsRegistry;
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    const juce::String classes = "HKEY_CURRENT_USER\\Software\\Classes\\";

    // a ProgID that opens files with this exe
    R::setValue (classes + kProgId + "\\", "Audio file (Transport)");
    R::setValue (classes + kProgId + "\\DefaultIcon\\", "\"" + exe + "\",0");
    R::setValue (classes + kProgId + "\\shell\\open\\command\\", "\"" + exe + "\" \"%1\"");

    // capabilities, so Windows lists Transport under Default Apps
    const juce::String caps = "HKEY_CURRENT_USER\\Software\\Transport\\Capabilities\\";
    R::setValue (caps + "ApplicationName", "Transport");
    R::setValue (caps + "ApplicationDescription", "A music player that shows you what you hear.");
    for (auto* ext : kExtensions)
    {
        R::setValue (caps + "FileAssociations\\" + ext, kProgId);
        R::setValue (classes + ext + "\\OpenWithProgids\\" + kProgId, juce::String());
    }

    // videos: listed under "Open with" and selectable in Default Apps, but never taken over
    const juce::String videoProgId = "Transport.VideoFile";
    R::setValue (classes + videoProgId + "\\", "Video (Transport)");
    R::setValue (classes + videoProgId + "\\DefaultIcon\\", "\"" + exe + "\",0");
    R::setValue (classes + videoProgId + "\\shell\\open\\command\\", "\"" + exe + "\" \"%1\"");
    for (auto& ext : media::videoExtensions())
    {
        R::setValue (caps + "FileAssociations\\." + ext, videoProgId);
        R::setValue (classes + "." + ext + "\\OpenWithProgids\\" + videoProgId, juce::String());
    }
    R::setValue ("HKEY_CURRENT_USER\\Software\\RegisteredApplications\\Transport", "Software\\Transport\\Capabilities");

    SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    // Windows only lets the user confirm the default, so open the settings page for this app
    ShellExecuteW (nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=Transport", nullptr, nullptr, SW_SHOWNORMAL);
    return "In Settings, choose Transport and click \"Set default\"";
}

#elif JUCE_MAC

bool isSupported() { return true; }

namespace
{
juce::String bundleId()
{
    if (auto id = CFBundleGetIdentifier (CFBundleGetMainBundle()))
        return juce::String::fromCFString (id);
    return "app.transport.player";
}
const char* kTypes[] = { "public.mp3", "com.microsoft.waveform-audio", "org.xiph.flac", "public.aiff-audio",
                         "public.aifc-audio", "public.mpeg-4-audio", "com.apple.m4a-audio", "org.xiph.ogg-audio" };
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

bool isDefault()
{
    CFStringRef handler = LSCopyDefaultRoleHandlerForContentType (CFSTR ("public.mp3"), kLSRolesAll);
    if (handler == nullptr) return false;
    const bool result = juce::String::fromCFString (handler).equalsIgnoreCase (bundleId());
    CFRelease (handler);
    return result;
}

juce::String makeDefault()
{
    const auto id = bundleId().toCFString();
    int ok = 0;
    for (auto* type : kTypes)
    {
        CFStringRef t = CFStringCreateWithCString (nullptr, type, kCFStringEncodingUTF8);
        if (LSSetDefaultRoleHandlerForContentType (t, kLSRolesViewer | kLSRolesEditor, id) == noErr) ++ok;
        CFRelease (t);
    }
    CFRelease (id);
    return ok > 0 ? "Transport now opens your audio files" : "macOS didn't allow the change";
}

#pragma clang diagnostic pop

#else

bool isSupported() { return false; }
bool isDefault() { return false; }
juce::String makeDefault() { return {}; }

#endif
} // namespace tp::defaultplayer
