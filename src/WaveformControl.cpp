#include "WaveformControl.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace auto_mixer {
namespace {

constexpr wchar_t kWaveformClass[] = L"AutoMixerWaveformControl";

} // namespace

bool WaveformControl::Register(const HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWaveformClass;
    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool WaveformControl::Create(
    const HWND parent,
    const int controlId,
    const std::wstring& title,
    const COLORREF color) {
    title_ = title;
    color_ = color;
    window_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        kWaveformClass,
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        100,
        100,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)),
        this);
    return window_ != nullptr;
}

void WaveformControl::AddSample(const float value) {
    current_ = std::clamp(value, 0.0F, 1.0F);
    history_.push_back(current_);
    while (history_.size() > 360) {
        history_.pop_front();
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void WaveformControl::Clear() {
    current_ = 0.0F;
    history_.clear();
    InvalidateRect(window_, nullptr, FALSE);
}

void WaveformControl::SetThresholds(const float activation, const float release) {
    activationThreshold_ = activation;
    releaseThreshold_ = release;
    InvalidateRect(window_, nullptr, FALSE);
}

void WaveformControl::Move(const int x, const int y, const int width, const int height) {
    MoveWindow(window_, x, y, std::max(width, 1), std::max(height, 1), TRUE);
}

HWND WaveformControl::Handle() const noexcept {
    return window_;
}

LRESULT CALLBACK WaveformControl::WindowProc(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    WaveformControl* self = reinterpret_cast<WaveformControl*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<WaveformControl*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr && message == WM_PAINT) {
        self->Paint();
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void WaveformControl::Paint() {
    PAINTSTRUCT paint{};
    HDC device = BeginPaint(window_, &paint);

    RECT bounds{};
    GetClientRect(window_, &bounds);
    const int width = std::max(bounds.right - bounds.left, 1L);
    const int height = std::max(bounds.bottom - bounds.top, 1L);

    HDC buffer = CreateCompatibleDC(device);
    HBITMAP bitmap = CreateCompatibleBitmap(device, width, height);
    const HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);

    HBRUSH background = CreateSolidBrush(RGB(18, 24, 33));
    FillRect(buffer, &bounds, background);
    DeleteObject(background);

    SetBkMode(buffer, TRANSPARENT);
    SetTextColor(buffer, RGB(225, 231, 239));
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const HGDIOBJ oldFont = SelectObject(buffer, font);
    RECT titleBounds{12, 7, width - 12, 28};
    DrawTextW(buffer, title_.c_str(), -1, &titleBounds, DT_LEFT | DT_SINGLELINE);

    std::wostringstream peakText;
    peakText << L"Peak  " << std::fixed << std::setprecision(3) << current_;
    RECT valueBounds{12, 7, width - 12, 28};
    DrawTextW(buffer, peakText.str().c_str(), -1, &valueBounds,
              DT_RIGHT | DT_SINGLELINE);

    const RECT graph{12, 34, width - 12, height - 12};
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(42, 52, 65));
    HGDIOBJ oldPen = SelectObject(buffer, gridPen);
    for (int division = 0; division <= 4; ++division) {
        const int y = graph.top + (graph.bottom - graph.top) * division / 4;
        MoveToEx(buffer, graph.left, y, nullptr);
        LineTo(buffer, graph.right, y);
    }
    DeleteObject(SelectObject(buffer, oldPen));

    const auto drawThreshold = [&](const float threshold, const COLORREF color) {
        if (threshold < 0.0F || threshold > 1.0F) {
            return;
        }
        const int y = graph.bottom - static_cast<int>(
            std::lround(threshold * static_cast<float>(graph.bottom - graph.top)));
        HPEN pen = CreatePen(PS_DOT, 1, color);
        const HGDIOBJ previous = SelectObject(buffer, pen);
        MoveToEx(buffer, graph.left, y, nullptr);
        LineTo(buffer, graph.right, y);
        SelectObject(buffer, previous);
        DeleteObject(pen);
    };
    drawThreshold(releaseThreshold_, RGB(218, 156, 71));
    drawThreshold(activationThreshold_, RGB(231, 89, 89));

    if (history_.size() >= 2 && graph.right > graph.left && graph.bottom > graph.top) {
        std::vector<POINT> points;
        points.reserve(history_.size());
        const std::size_t count = history_.size();
        for (std::size_t index = 0; index < count; ++index) {
            const int x = graph.left + static_cast<int>(
                index * static_cast<std::size_t>(graph.right - graph.left) / (count - 1));
            const int y = graph.bottom - static_cast<int>(std::lround(
                history_[index] * static_cast<float>(graph.bottom - graph.top)));
            points.push_back({x, y});
        }
        HPEN wavePen = CreatePen(PS_SOLID, 2, color_);
        const HGDIOBJ previous = SelectObject(buffer, wavePen);
        Polyline(buffer, points.data(), static_cast<int>(points.size()));
        SelectObject(buffer, previous);
        DeleteObject(wavePen);
    }

    SelectObject(buffer, oldFont);
    BitBlt(device, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(window_, &paint);
}

} // namespace auto_mixer
