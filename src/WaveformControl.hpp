#pragma once

#include <windows.h>

#include <deque>
#include <string>

namespace auto_mixer {

class WaveformControl final {
public:
    static bool Register(HINSTANCE instance);

    bool Create(HWND parent, int controlId, const std::wstring& title, COLORREF color);
    void AddSample(float value);
    void Clear();
    void SetThresholds(float activation, float release);
    void Move(int x, int y, int width, int height);
    [[nodiscard]] HWND Handle() const noexcept;

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();

    HWND window_ = nullptr;
    std::wstring title_;
    COLORREF color_ = RGB(40, 120, 220);
    float current_ = 0.0F;
    float activationThreshold_ = -1.0F;
    float releaseThreshold_ = -1.0F;
    std::deque<float> history_;
};

} // namespace auto_mixer
