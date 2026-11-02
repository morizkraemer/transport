#include "TaskbarPlayer.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <commctrl.h>
 #include <dwmapi.h>
 #include <shobjidl.h>
#endif

namespace tp
{
#if JUCE_WINDOWS
namespace
{
constexpr UINT_PTR kSubclassId = 0x7470;   // "tp"
enum ButtonId : UINT { prevId = 1, playId = 2, nextId = 3 };

/** 32-bit top-down DIB with premultiplied BGRA, as DWM and icons expect. */
HBITMAP toHBITMAP (const juce::Image& source)
{
    const auto img = source.convertedToFormat (juce::Image::ARGB);
    BITMAPINFO bmi {};
    bmi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img.getWidth();
    bmi.bmiHeader.biHeight = -img.getHeight();
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection (nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp == nullptr || bits == nullptr) return bmp;

    const juce::Image::BitmapData data (img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < img.getHeight(); ++y)
    {
        auto* dst = static_cast<juce::uint8*> (bits) + (size_t) y * (size_t) img.getWidth() * 4;
        for (int x = 0; x < img.getWidth(); ++x)
        {
            const auto c = data.getPixelColour (x, y);
            const float a = c.getFloatAlpha();
            dst[x * 4 + 0] = (juce::uint8) juce::roundToInt (c.getBlue() * a);
            dst[x * 4 + 1] = (juce::uint8) juce::roundToInt (c.getGreen() * a);
            dst[x * 4 + 2] = (juce::uint8) juce::roundToInt (c.getRed() * a);
            dst[x * 4 + 3] = c.getAlpha();
        }
    }
    return bmp;
}

HICON makeIcon (int size, int kind)   // 0 prev, 1 play, 2 pause, 3 next
{
    juce::Image img (juce::Image::ARGB, size, size, true);
    {
        juce::Graphics g (img);
        g.setColour (juce::Colours::white);
        const float s = (float) size;
        juce::Path p;
        switch (kind)
        {
            case 0: p.addRectangle (s * 0.22f, s * 0.25f, s * 0.1f, s * 0.5f); p.addTriangle (s * 0.78f, s * 0.25f, s * 0.36f, s * 0.5f, s * 0.78f, s * 0.75f); break;
            case 1: p.addTriangle (s * 0.3f, s * 0.2f, s * 0.8f, s * 0.5f, s * 0.3f, s * 0.8f); break;
            case 2: p.addRectangle (s * 0.28f, s * 0.22f, s * 0.15f, s * 0.56f); p.addRectangle (s * 0.57f, s * 0.22f, s * 0.15f, s * 0.56f); break;
            default: p.addTriangle (s * 0.22f, s * 0.25f, s * 0.64f, s * 0.5f, s * 0.22f, s * 0.75f); p.addRectangle (s * 0.68f, s * 0.25f, s * 0.1f, s * 0.5f); break;
        }
        g.fillPath (p);
    }

    HBITMAP colour = toHBITMAP (img);
    HBITMAP mask = CreateBitmap (size, size, 1, 1, nullptr);
    ICONINFO info {};
    info.fIcon = TRUE;
    info.hbmColor = colour;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect (&info);
    DeleteObject (colour);
    DeleteObject (mask);
    return icon;
}
} // namespace

struct TaskbarPlayer::Impl : private juce::Timer
{
    Impl (TaskbarPlayer& o, juce::Component& w) : owner (o), window (w)
    {
        taskbarCreatedMessage = RegisterWindowMessageW (L"TaskbarButtonCreated");
        const int iconSize = juce::jmax (16, GetSystemMetrics (SM_CXSMICON));
        for (int k = 0; k < 4; ++k) icons[k] = makeIcon (iconSize, k);
        startTimerHz (2);
    }

    ~Impl() override
    {
        stopTimer();
        detach();
        for (auto& i : icons) if (i != nullptr) DestroyIcon (i);
        if (taskbar != nullptr) taskbar->Release();
    }

    void setEnabled (bool e)
    {
        enabled = e;
        if (hwnd == nullptr) attach();
        if (hwnd == nullptr) return;
        BOOL on = e ? TRUE : FALSE;
        DwmSetWindowAttribute (hwnd, DWMWA_FORCE_ICONIC_REPRESENTATION, &on, sizeof (on));
        DwmSetWindowAttribute (hwnd, DWMWA_HAS_ICONIC_BITMAP, &on, sizeof (on));
        if (e) addOrUpdateButtons();
        else if (buttonsAdded) updateButtons (THBF_HIDDEN);
        DwmInvalidateIconicBitmaps (hwnd);
    }

private:
    void attach()
    {
        auto* peer = window.getPeer();
        if (peer == nullptr) return;
        hwnd = (HWND) peer->getNativeHandle();
        if (hwnd == nullptr) return;
        ChangeWindowMessageFilterEx (hwnd, taskbarCreatedMessage, MSGFLT_ALLOW, nullptr);
        SetWindowSubclass (hwnd, &Impl::subclassProc, kSubclassId, (DWORD_PTR) this);
    }

    void detach()
    {
        if (hwnd != nullptr)
            RemoveWindowSubclass (hwnd, &Impl::subclassProc, kSubclassId);
        hwnd = nullptr;
    }

    bool ensureTaskbar()
    {
        if (taskbar != nullptr) return true;
        if (FAILED (CoCreateInstance (CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&taskbar))))
        {
            taskbar = nullptr;
            return false;
        }
        if (FAILED (taskbar->HrInit()))
        {
            taskbar->Release();
            taskbar = nullptr;
            return false;
        }
        return true;
    }

    void fillButtons (THUMBBUTTON* b, DWORD flags)
    {
        const bool playing = owner.isPlaying && owner.isPlaying();
        const UINT ids[] = { prevId, playId, nextId };
        const wchar_t* tips[] = { L"Previous", playing ? L"Pause" : L"Play", L"Next" };
        HICON iconsFor[] = { icons[0], playing ? icons[2] : icons[1], icons[3] };
        for (int i = 0; i < 3; ++i)
        {
            b[i] = {};
            b[i].dwMask = (THUMBBUTTONMASK) (THB_ICON | THB_TOOLTIP | THB_FLAGS);
            b[i].iId = ids[i];
            b[i].hIcon = iconsFor[i];
            b[i].dwFlags = (THUMBBUTTONFLAGS) flags;
            wcsncpy_s (b[i].szTip, tips[i], _TRUNCATE);
        }
    }

    void addOrUpdateButtons()
    {
        if (hwnd == nullptr || ! ensureTaskbar()) return;
        THUMBBUTTON b[3];
        fillButtons (b, THBF_ENABLED);
        if (! buttonsAdded)
            buttonsAdded = SUCCEEDED (taskbar->ThumbBarAddButtons (hwnd, 3, b));   // may only be called once per window
        else
            taskbar->ThumbBarUpdateButtons (hwnd, 3, b);
        lastPlaying = owner.isPlaying && owner.isPlaying();
    }

    void updateButtons (DWORD flags)
    {
        if (hwnd == nullptr || ! ensureTaskbar() || ! buttonsAdded) return;
        THUMBBUTTON b[3];
        fillButtons (b, flags);
        taskbar->ThumbBarUpdateButtons (hwnd, 3, b);
    }

    void timerCallback() override
    {
        if (hwnd == nullptr)
        {
            attach();
            if (hwnd != nullptr) setEnabled (enabled);
            return;
        }
        if (! enabled) return;

        const bool playing = owner.isPlaying && owner.isPlaying();
        if (playing != lastPlaying) addOrUpdateButtons();

        const auto key = owner.stateKey ? owner.stateKey() : juce::String();
        if (key != lastKey)
        {
            lastKey = key;
            DwmInvalidateIconicBitmaps (hwnd);
        }
    }

    void sendThumbnail (int maxW, int maxH)
    {
        if (! owner.renderPreview) return;
        // keep the window's aspect ratio inside the size DWM asks for
        const float aspect = 2.6f;
        int w = maxW, h = juce::roundToInt ((float) maxW / aspect);
        if (h > maxH) { h = maxH; w = juce::roundToInt ((float) maxH * aspect); }
        if (HBITMAP bmp = toHBITMAP (owner.renderPreview (juce::jmax (1, w), juce::jmax (1, h))))
        {
            DwmSetIconicThumbnail (hwnd, bmp, 0);
            DeleteObject (bmp);
        }
    }

    void sendLivePreview()
    {
        // Aero Peek: show the real window
        const auto snapshot = window.createComponentSnapshot (window.getLocalBounds(), true, 1.0f);
        if (HBITMAP bmp = toHBITMAP (snapshot))
        {
            DwmSetIconicLivePreviewBitmap (hwnd, bmp, nullptr, 0);
            DeleteObject (bmp);
        }
    }

    static LRESULT CALLBACK subclassProc (HWND h, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Impl*> (ref);

        if (msg == self->taskbarCreatedMessage)
        {
            self->buttonsAdded = false;   // explorer restarted or button (re)created
            if (self->enabled) self->setEnabled (true);
        }
        else if (msg == WM_COMMAND && HIWORD (wParam) == THBN_CLICKED)
        {
            switch (LOWORD (wParam))
            {
                case prevId: if (self->owner.onPrev) self->owner.onPrev(); break;
                case playId: if (self->owner.onPlayPause) self->owner.onPlayPause(); break;
                case nextId: if (self->owner.onNext) self->owner.onNext(); break;
                default: break;
            }
            self->addOrUpdateButtons();
            DwmInvalidateIconicBitmaps (h);
            return 0;
        }
        else if (msg == WM_DWMSENDICONICTHUMBNAIL && self->enabled)
        {
            self->sendThumbnail (HIWORD (lParam), LOWORD (lParam));
            return 0;
        }
        else if (msg == WM_DWMSENDICONICLIVEPREVIEWBITMAP && self->enabled)
        {
            self->sendLivePreview();
            return 0;
        }
        else if (msg == WM_NCDESTROY)
        {
            RemoveWindowSubclass (h, &Impl::subclassProc, kSubclassId);
            self->hwnd = nullptr;
        }
        return DefSubclassProc (h, msg, wParam, lParam);
    }

    TaskbarPlayer& owner;
    juce::Component& window;
    HWND hwnd = nullptr;
    ITaskbarList3* taskbar = nullptr;
    UINT taskbarCreatedMessage = 0;
    HICON icons[4] {};
    bool enabled = false, buttonsAdded = false, lastPlaying = false;
    juce::String lastKey;
};

TaskbarPlayer::TaskbarPlayer (juce::Component& w) : impl (std::make_unique<Impl> (*this, w)) {}
TaskbarPlayer::~TaskbarPlayer() = default;
bool TaskbarPlayer::isSupported() { return true; }
void TaskbarPlayer::setEnabled (bool e) { impl->setEnabled (e); }

#else

struct TaskbarPlayer::Impl {};
TaskbarPlayer::TaskbarPlayer (juce::Component&) {}
TaskbarPlayer::~TaskbarPlayer() = default;
bool TaskbarPlayer::isSupported() { return false; }
void TaskbarPlayer::setEnabled (bool) {}

#endif
} // namespace tp
