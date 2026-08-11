#include "ActivityDetector.hpp"
#include "CoreAudioMonitor.hpp"
#include "DuckingController.hpp"
#include "ProcessLoopbackCapture.hpp"
#include "WaveformControl.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"AutoDuckingMainWindow";
constexpr int kDeviceComboId = 100;
constexpr int kVoiceListId = 101;
constexpr int kMusicListId = 102;
constexpr int kVoiceWaveId = 103;
constexpr int kMusicWaveId = 104;
constexpr int kTrackBaseId = 300;
constexpr int kEditBaseId = 400;
constexpr UINT_PTR kSampleTimerId = 1;
constexpr UINT kSampleIntervalMs = 50;
constexpr UINT kTopologyIntervalMs = 1000;

struct ParameterSpec {
    const wchar_t* label;
    const wchar_t* unit;
    int minimum;
    int maximum;
    int initial;
    int scale;
    int decimals;
};

constexpr ParameterSpec kParameterSpecs[] = {
    {L"激活阈值", L"峰值", 0, 1000, 20, 1000, 3},
    {L"释放阈值", L"峰值", 0, 1000, 10, 1000, 3},
    {L"激活持续时间", L"ms", 0, 2000, 100, 1, 0},
    {L"释放延迟", L"ms", 0, 5000, 800, 1, 0},
    {L"音乐 Duck 比例", L"倍", 0, 1000, 250, 1000, 3},
    {L"Attack 时间", L"ms", 0, 3000, 200, 1, 0},
    {L"Release 时间", L"ms", 0, 5000, 800, 1, 0},
};

struct ParameterControl {
    ParameterSpec spec{};
    HWND label = nullptr;
    HWND track = nullptr;
    HWND edit = nullptr;
    HWND unit = nullptr;
};

struct AppAggregate {
    int sessionCount = 0;
    float peak = 0.0F;
    std::set<std::uint32_t> processIds;
};

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t character) {
        return static_cast<wchar_t>(towlower(character));
    });
    return value;
}

std::wstring DeviceLabel(const auto_ducking::DeviceSnapshot& device) {
    std::wstring roles;
    if (device.defaultMultimedia) {
        roles += L"默认媒体/";
    }
    if (device.defaultCommunications) {
        roles += L"默认通讯/";
    }
    if (device.defaultConsole) {
        roles += L"默认控制台/";
    }
    if (!roles.empty()) {
        roles.pop_back();
        return device.name + L"  [" + roles + L"]";
    }
    return device.name;
}

void ConfigureListView(const HWND list) {
    ListView_SetExtendedListViewStyle(
        list,
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<LPWSTR>(L"应用程序");
    column.cx = 240;
    ListView_InsertColumn(list, 0, &column);
    column.pszText = const_cast<LPWSTR>(L"会话");
    column.cx = 65;
    column.iSubItem = 1;
    ListView_InsertColumn(list, 1, &column);
    column.pszText = const_cast<LPWSTR>(L"峰值");
    column.cx = 80;
    column.iSubItem = 2;
    ListView_InsertColumn(list, 2, &column);
}

class MainWindow final {
public:
    MainWindow() : monitor_(false) {}

    ~MainWindow() {
        RestoreAllVolumes();
        if (font_ != nullptr) {
            DeleteObject(font_);
        }
    }

    bool Create(const HINSTANCE instance) {
        instance_ = instance;
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        window_ = CreateWindowExW(
            0,
            kWindowClass,
            L"Auto Ducking — 自动音量闪避",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1280,
            850,
            nullptr,
            nullptr,
            instance,
            this);
        return window_ != nullptr;
    }

    int Run(const int showCommand) {
        ShowWindow(window_, showCommand);
        UpdateWindow(window_);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK WindowProc(
        const HWND window,
        const UINT message,
        const WPARAM wParam,
        const LPARAM lParam) {
        MainWindow* self = reinterpret_cast<MainWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MainWindow*>(create->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        return self->HandleMessage(message, wParam, lParam);
    }

    LRESULT HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            return CreateControls() ? 0 : -1;
        case WM_SIZE:
            Layout(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_TIMER:
            if (wParam == kSampleTimerId) {
                try {
                    Tick();
                } catch (...) {
                    KillTimer(window_, kSampleTimerId);
                    RestoreAllVolumes();
                    SetWindowTextW(
                        status_,
                        L"运行时发生错误；已停止 Ducking 并尽力恢复所有受控音量。");
                }
            }
            return 0;
        case WM_HSCROLL:
            HandleSlider(reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_COMMAND:
            HandleCommand(LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_NOTIFY:
            HandleNotification(reinterpret_cast<NMHDR*>(lParam));
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize = {980, 720};
            return 0;
        }
        case WM_DESTROY:
            KillTimer(window_, kSampleTimerId);
            RestoreAllVolumes();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window_, message, wParam, lParam);
        }
    }

    HWND CreateControl(
        const DWORD extendedStyle,
        const wchar_t* className,
        const wchar_t* text,
        const DWORD style,
        const int id) const {
        HWND control = CreateWindowExW(
            extendedStyle,
            className,
            text,
            WS_CHILD | WS_VISIBLE | style,
            0,
            0,
            10,
            10,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_,
            nullptr);
        if (control != nullptr && font_ != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        return control;
    }

    bool CreateControls() {
        font_ = CreateFontW(
            -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        deviceLabel_ = CreateControl(0, WC_STATICW, L"输出设备：", SS_LEFT, 0);
        deviceCombo_ = CreateControl(
            WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kDeviceComboId);
        voiceGroup_ = CreateControl(0, WC_BUTTONW, L"通讯软件（勾选作为触发源）", BS_GROUPBOX, 0);
        musicGroup_ = CreateControl(0, WC_BUTTONW, L"音乐软件（勾选作为 Duck 目标）", BS_GROUPBOX, 0);
        parameterGroup_ = CreateControl(0, WC_BUTTONW, L"检测与 Duck 参数", BS_GROUPBOX, 0);
        status_ = CreateControl(0, WC_STATICW, L"正在读取 Windows 音频会话并初始化音量控制…", SS_LEFT, 0);

        voiceList_ = CreateControl(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP, kVoiceListId);
        musicList_ = CreateControl(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP, kMusicListId);
        ConfigureListView(voiceList_);
        ConfigureListView(musicList_);
        SetWindowTheme(voiceList_, L"Explorer", nullptr);
        SetWindowTheme(musicList_, L"Explorer", nullptr);

        if (!voiceWave_.Create(window_, kVoiceWaveId, L"通讯音频峰值波形（不录音）", RGB(76, 176, 255)) ||
            !musicWave_.Create(window_, kMusicWaveId, L"音乐音频峰值波形（不录音）", RGB(92, 210, 148))) {
            return false;
        }

        parameters_.reserve(std::size(kParameterSpecs));
        for (std::size_t index = 0; index < std::size(kParameterSpecs); ++index) {
            ParameterControl parameter;
            parameter.spec = kParameterSpecs[index];
            parameter.label = CreateControl(0, WC_STATICW, parameter.spec.label, SS_LEFT, 0);
            parameter.track = CreateControl(
                0, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
                kTrackBaseId + static_cast<int>(index));
            parameter.edit = CreateControl(
                WS_EX_CLIENTEDGE, WC_EDITW, L"", ES_RIGHT | ES_AUTOHSCROLL | WS_TABSTOP,
                kEditBaseId + static_cast<int>(index));
            parameter.unit = CreateControl(0, WC_STATICW, parameter.spec.unit, SS_LEFT, 0);
            SendMessageW(parameter.track, TBM_SETRANGE, TRUE,
                         MAKELONG(parameter.spec.minimum, parameter.spec.maximum));
            SendMessageW(parameter.track, TBM_SETPOS, TRUE, parameter.spec.initial);
            parameters_.push_back(parameter);
        }
        for (std::size_t index = 0; index < parameters_.size(); ++index) {
            SetEditFromTrack(index);
        }
        UpdateDetectorConfig();

        monitor_.RefreshTopology();
        snapshot_ = monitor_.Sample();
        PopulateDevices();
        RebuildAppLists(true);
        lastTopologyRefreshMs_ = GetTickCount64();
        SetTimer(window_, kSampleTimerId, kSampleIntervalMs, nullptr);
        return true;
    }

    void Layout(const int width, const int height) {
        const int margin = 16;
        const int gap = 12;
        const int statusHeight = 28;
        const int parameterHeight = 270;
        const int top = 52;
        const int groupBottom = std::max(top + 260, height - statusHeight - parameterHeight - 14);
        const int groupHeight = groupBottom - top;
        const int columnWidth = std::max((width - margin * 2 - gap) / 2, 420);

        MoveWindow(deviceLabel_, margin, 17, 90, 24, TRUE);
        MoveWindow(deviceCombo_, margin + 92, 13, std::max(width - margin * 2 - 92, 200), 320, TRUE);

        const int leftX = margin;
        const int rightX = margin + columnWidth + gap;
        MoveWindow(voiceGroup_, leftX, top, columnWidth, groupHeight, TRUE);
        MoveWindow(musicGroup_, rightX, top, columnWidth, groupHeight, TRUE);

        const int listTop = top + 28;
        const int listHeight = std::min(185, std::max(groupHeight / 2 - 20, 120));
        MoveWindow(voiceList_, leftX + 12, listTop, columnWidth - 24, listHeight, TRUE);
        MoveWindow(musicList_, rightX + 12, listTop, columnWidth - 24, listHeight, TRUE);
        const int waveTop = listTop + listHeight + 10;
        const int waveHeight = std::max(top + groupHeight - waveTop - 12, 70);
        voiceWave_.Move(leftX + 12, waveTop, columnWidth - 24, waveHeight);
        musicWave_.Move(rightX + 12, waveTop, columnWidth - 24, waveHeight);

        const int parameterTop = groupBottom + 10;
        MoveWindow(parameterGroup_, margin, parameterTop, width - margin * 2, parameterHeight, TRUE);
        const int parameterColumnWidth = (width - margin * 2 - 28) / 2;
        for (std::size_t index = 0; index < parameters_.size(); ++index) {
            const int column = index < 4 ? 0 : 1;
            const int row = column == 0 ? static_cast<int>(index) : static_cast<int>(index) - 4;
            const int x = margin + 14 + column * (parameterColumnWidth + 12);
            const int y = parameterTop + 28 + row * 55;
            const int trackWidth = std::max(parameterColumnWidth - 330, 150);
            MoveWindow(parameters_[index].label, x, y + 6, 138, 24, TRUE);
            MoveWindow(parameters_[index].track, x + 140, y, trackWidth, 34, TRUE);
            MoveWindow(parameters_[index].edit, x + 148 + trackWidth, y + 3, 78, 27, TRUE);
            MoveWindow(parameters_[index].unit, x + 232 + trackWidth, y + 6, 55, 24, TRUE);
        }
        MoveWindow(status_, margin, height - statusHeight, width - margin * 2, 24, TRUE);
    }

    void PopulateDevices() {
        const std::wstring previousDeviceId = selectedDeviceId_;
        std::vector<std::wstring> ids;
        std::vector<std::wstring> labels;
        ids.reserve(snapshot_.devices.size());
        labels.reserve(snapshot_.devices.size());
        for (const auto& device : snapshot_.devices) {
            ids.push_back(device.id);
            labels.push_back(DeviceLabel(device));
        }
        if (ids == deviceIds_ && labels == deviceLabels_) {
            return;
        }

        deviceIds_ = std::move(ids);
        deviceLabels_ = std::move(labels);
        SendMessageW(deviceCombo_, WM_SETREDRAW, FALSE, 0);
        ComboBox_ResetContent(deviceCombo_);
        for (const auto& label : deviceLabels_) {
            ComboBox_AddString(deviceCombo_, label.c_str());
        }

        int selectedIndex = -1;
        for (std::size_t index = 0; index < deviceIds_.size(); ++index) {
            if (deviceIds_[index] == selectedDeviceId_) {
                selectedIndex = static_cast<int>(index);
                break;
            }
        }
        if (selectedIndex < 0) {
            for (std::size_t index = 0; index < snapshot_.devices.size(); ++index) {
                if (snapshot_.devices[index].defaultMultimedia) {
                    selectedIndex = static_cast<int>(index);
                    break;
                }
            }
        }
        if (selectedIndex < 0 && !deviceIds_.empty()) {
            selectedIndex = 0;
        }
        ComboBox_SetCurSel(deviceCombo_, selectedIndex);
        selectedDeviceId_ = selectedIndex >= 0
            ? deviceIds_[static_cast<std::size_t>(selectedIndex)]
            : L"";
        if (selectedDeviceId_ != previousDeviceId) {
            RestoreAllVolumes();
            detector_.Reset();
            voiceWave_.Clear();
            musicWave_.Clear();
        }
        SendMessageW(deviceCombo_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(deviceCombo_, nullptr, TRUE);
    }

    const auto_ducking::DeviceSnapshot* SelectedDevice() const {
        const auto match = std::find_if(
            snapshot_.devices.begin(), snapshot_.devices.end(), [&](const auto& device) {
                return device.id == selectedDeviceId_;
            });
        return match == snapshot_.devices.end() ? nullptr : &*match;
    }

    std::map<std::wstring, AppAggregate> AggregateApps() const {
        std::map<std::wstring, AppAggregate> apps;
        const auto* device = SelectedDevice();
        if (device == nullptr) {
            return apps;
        }
        for (const auto& session : device->sessions) {
            if (session.systemSounds || session.processName.empty() || session.processName.front() == L'<') {
                continue;
            }
            auto& app = apps[session.processName];
            ++app.sessionCount;
            if (session.processId != 0) {
                app.processIds.insert(session.processId);
            }
        }
        return apps;
    }

    void AutoClassifyNewApp(const std::wstring& appName) {
        if (!knownApps_.insert(appName).second) {
            return;
        }
        const std::wstring lower = Lowercase(appName);
        if (lower.find(L"discord") != std::wstring::npos ||
            lower.find(L"telegram") != std::wstring::npos ||
            lower.find(L"teams") != std::wstring::npos ||
            lower.find(L"zoom") != std::wstring::npos) {
            voiceApps_.insert(appName);
        } else if (lower.find(L"spotify") != std::wstring::npos ||
                   lower.find(L"applemusic") != std::wstring::npos ||
                   lower.find(L"foobar") != std::wstring::npos ||
                   lower.find(L"vlc") != std::wstring::npos ||
                   lower.find(L"musicbee") != std::wstring::npos) {
            musicApps_.insert(appName);
        }
    }

    void RebuildList(
        const HWND list,
        const std::map<std::wstring, AppAggregate>& apps,
        const std::set<std::wstring>& selectedApps) {
        ListView_DeleteAllItems(list);
        int row = 0;
        for (const auto& [name, aggregate] : apps) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = const_cast<LPWSTR>(name.c_str());
            ListView_InsertItem(list, &item);

            wchar_t count[16]{};
            swprintf_s(count, L"%d", aggregate.sessionCount);
            ListView_SetItemText(list, row, 1, count);
            wchar_t peak[24] = L"—";
            if (selectedApps.count(name) != 0) {
                swprintf_s(peak, L"%.3f", aggregate.peak);
            }
            ListView_SetItemText(list, row, 2, peak);
            ListView_SetCheckState(list, row, selectedApps.count(name) != 0);
            ++row;
        }
        ListView_SetColumnWidth(list, 0, LVSCW_AUTOSIZE_USEHEADER);
    }

    void RebuildAppLists(const bool force = false) {
        const auto apps = AggregateApps();
        std::vector<std::pair<std::wstring, int>> signature;
        signature.reserve(apps.size());
        for (const auto& [name, aggregate] : apps) {
            signature.emplace_back(name, aggregate.sessionCount);
        }
        if (!force && signature == appSignature_) {
            return;
        }
        appSignature_ = std::move(signature);
        for (const auto& [name, ignored] : apps) {
            static_cast<void>(ignored);
            AutoClassifyNewApp(name);
        }
        suppressListNotifications_ = true;
        RebuildList(voiceList_, apps, voiceApps_);
        RebuildList(musicList_, apps, musicApps_);
        suppressListNotifications_ = false;
    }

    void UpdateListMetrics(
        const HWND list,
        const std::map<std::wstring, AppAggregate>& apps,
        const std::set<std::wstring>& selectedApps) {
        wchar_t name[512]{};
        const int count = ListView_GetItemCount(list);
        for (int row = 0; row < count; ++row) {
            ListView_GetItemText(list, row, 0, name, static_cast<int>(std::size(name)));
            const auto app = apps.find(name);
            if (app == apps.end()) {
                continue;
            }
            wchar_t sessions[16]{};
            swprintf_s(sessions, L"%d", app->second.sessionCount);
            ListView_SetItemText(list, row, 1, sessions);
            wchar_t peak[24] = L"—";
            if (selectedApps.count(name) != 0) {
                swprintf_s(peak, L"%.3f", app->second.peak);
            }
            ListView_SetItemText(list, row, 2, peak);
        }
    }

    void ReconcileProcessCaptures(const std::map<std::wstring, AppAggregate>& apps) {
        std::set<std::uint32_t> desiredProcesses;
        for (const auto& [name, aggregate] : apps) {
            if (voiceApps_.count(name) != 0 || musicApps_.count(name) != 0) {
                desiredProcesses.insert(aggregate.processIds.begin(), aggregate.processIds.end());
            }
        }

        for (auto capture = processCaptures_.begin(); capture != processCaptures_.end();) {
            if (desiredProcesses.count(capture->first) == 0) {
                capture = processCaptures_.erase(capture);
            } else {
                ++capture;
            }
        }
        for (const std::uint32_t processId : desiredProcesses) {
            if (processCaptures_.count(processId) == 0) {
                processCaptures_.emplace(
                    processId, std::make_unique<auto_ducking::ProcessLoopbackCapture>(processId));
            }
        }
    }

    void ApplyProcessPeaks(std::map<std::wstring, AppAggregate>& apps) const {
        for (auto& [name, aggregate] : apps) {
            static_cast<void>(name);
            aggregate.peak = 0.0F;
            for (const std::uint32_t processId : aggregate.processIds) {
                const auto capture = processCaptures_.find(processId);
                if (capture != processCaptures_.end()) {
                    aggregate.peak = std::max(aggregate.peak, capture->second->Peak());
                }
            }
        }
    }

    void Tick() {
        const std::uint64_t now = GetTickCount64();
        bool topologyChanged = false;
        if (now - lastTopologyRefreshMs_ >= kTopologyIntervalMs) {
            monitor_.RefreshTopology();
            lastTopologyRefreshMs_ = now;
            topologyChanged = true;
        }
        snapshot_ = monitor_.Sample();
        PopulateDevices();
        if (topologyChanged) {
            RebuildAppLists();
        }

        auto apps = AggregateApps();
        ReconcileProcessCaptures(apps);
        ApplyProcessPeaks(apps);
        UpdateListMetrics(voiceList_, apps, voiceApps_);
        UpdateListMetrics(musicList_, apps, musicApps_);

        float voicePeak = 0.0F;
        float musicPeak = 0.0F;
        for (const auto& [name, aggregate] : apps) {
            if (voiceApps_.count(name) != 0) {
                voicePeak = std::max(voicePeak, aggregate.peak);
            }
            if (musicApps_.count(name) != 0) {
                musicPeak = std::max(musicPeak, aggregate.peak);
            }
        }
        voiceWave_.AddSample(voicePeak);
        musicWave_.AddSample(musicPeak);
        const bool active = detector_.Update(voicePeak, now);

        const auto* selectedDevice = SelectedDevice();
        if (selectedDevice != nullptr) {
            auto_ducking::DuckingConfig duckingConfig;
            duckingConfig.duckFactor = static_cast<float>(ParameterPosition(4)) / 1000.0F;
            duckingConfig.attackMs = static_cast<std::uint32_t>(ParameterPosition(5));
            duckingConfig.releaseMs = static_cast<std::uint32_t>(ParameterPosition(6));
            duckingController_.Update(
                selectedDevice->sessions,
                musicApps_,
                active,
                duckingConfig,
                now,
                [&](const std::wstring& key, const float volume) {
                    return monitor_.SetSessionVolume(key, volume);
                });
        } else {
            RestoreAllVolumes();
        }

        std::wostringstream status;
        status << L"音量控制已启用  |  通讯 " << voiceApps_.size()
               << L" 个应用，音乐 " << musicApps_.size()
               << L" 个应用  |  通讯峰值 " << static_cast<int>(voicePeak * 1000.0F) / 1000.0F
               << L"  |  检测状态：" << (active ? L"有通讯音频" : L"静音")
               << L"  |  音乐倍率 " << static_cast<int>(
                      duckingController_.Attenuation() * 100.0F) << L"%"
               << L"（" << duckingController_.ControlledSessionCount() << L" 个会话）";
        if (!snapshot_.warnings.empty()) {
            status << L"  |  警告 " << snapshot_.warnings.size();
        }
        std::size_t captureErrors = 0;
        std::size_t capturesStarting = 0;
        for (const auto& [processId, capture] : processCaptures_) {
            static_cast<void>(processId);
            if (!capture->Error().empty()) {
                ++captureErrors;
            } else if (!capture->IsRunning()) {
                ++capturesStarting;
            }
        }
        if (captureErrors != 0) {
            status << L"  |  进程采样错误 " << captureErrors;
        } else if (capturesStarting != 0) {
            status << L"  |  正在启动进程采样…";
        }
        SetWindowTextW(status_, status.str().c_str());
    }

    void HandleCommand(const int id, const int notification) {
        if (id == kDeviceComboId && notification == CBN_SELCHANGE) {
            const int index = ComboBox_GetCurSel(deviceCombo_);
            if (index >= 0 && static_cast<std::size_t>(index) < deviceIds_.size()) {
                RestoreAllVolumes();
                selectedDeviceId_ = deviceIds_[static_cast<std::size_t>(index)];
                detector_.Reset();
                voiceWave_.Clear();
                musicWave_.Clear();
                RebuildAppLists(true);
            }
            return;
        }

        if (id >= kEditBaseId &&
            id < kEditBaseId + static_cast<int>(parameters_.size())) {
            const std::size_t index = static_cast<std::size_t>(id - kEditBaseId);
            if (notification == EN_CHANGE && !suppressParameterNotifications_) {
                wchar_t text[64]{};
                GetWindowTextW(parameters_[index].edit, text, static_cast<int>(std::size(text)));
                wchar_t* end = nullptr;
                const double value = std::wcstod(text, &end);
                if (end != text && *end == L'\0') {
                    const int position = std::clamp(
                        static_cast<int>(std::lround(value * parameters_[index].spec.scale)),
                        parameters_[index].spec.minimum,
                        parameters_[index].spec.maximum);
                    SendMessageW(parameters_[index].track, TBM_SETPOS, TRUE, position);
                    NormalizeThresholds(index);
                    UpdateDetectorConfig();
                }
            } else if (notification == EN_KILLFOCUS) {
                SetEditFromTrack(index);
            }
        }
    }

    void HandleSlider(const HWND track) {
        const auto match = std::find_if(parameters_.begin(), parameters_.end(), [&](const auto& parameter) {
            return parameter.track == track;
        });
        if (match == parameters_.end()) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(match - parameters_.begin());
        SetEditFromTrack(index);
        NormalizeThresholds(index);
        UpdateDetectorConfig();
    }

    void RestoreAllVolumes() noexcept {
        try {
            duckingController_.RestoreAll([&](const std::wstring& key, const float volume) {
                return monitor_.SetSessionVolume(key, volume);
            });
        } catch (...) {
            // Shutdown/device-switch restoration is best effort.
        }
    }

    void SetEditFromTrack(const std::size_t index) {
        const auto& parameter = parameters_[index];
        const int position = static_cast<int>(SendMessageW(parameter.track, TBM_GETPOS, 0, 0));
        wchar_t value[64]{};
        if (parameter.spec.decimals == 0) {
            swprintf_s(value, L"%d", position);
        } else {
            swprintf_s(value, L"%.*f", parameter.spec.decimals,
                       static_cast<double>(position) / parameter.spec.scale);
        }
        suppressParameterNotifications_ = true;
        SetWindowTextW(parameter.edit, value);
        suppressParameterNotifications_ = false;
    }

    int ParameterPosition(const std::size_t index) const {
        return static_cast<int>(SendMessageW(parameters_[index].track, TBM_GETPOS, 0, 0));
    }

    void SetParameterPosition(const std::size_t index, const int position) {
        SendMessageW(parameters_[index].track, TBM_SETPOS, TRUE, position);
        SetEditFromTrack(index);
    }

    void NormalizeThresholds(const std::size_t changedIndex) {
        const int activation = ParameterPosition(0);
        const int release = ParameterPosition(1);
        if (release <= activation) {
            return;
        }
        if (changedIndex == 0) {
            SetParameterPosition(1, activation);
        } else {
            SetParameterPosition(0, release);
        }
    }

    void UpdateDetectorConfig() {
        auto_ducking::ActivityDetectorConfig config;
        config.activationThreshold = static_cast<float>(ParameterPosition(0)) / 1000.0F;
        config.releaseThreshold = static_cast<float>(ParameterPosition(1)) / 1000.0F;
        config.activationMs = static_cast<std::uint32_t>(ParameterPosition(2));
        config.releaseDelayMs = static_cast<std::uint32_t>(ParameterPosition(3));
        detector_.SetConfig(config);
        voiceWave_.SetThresholds(config.activationThreshold, config.releaseThreshold);
    }

    void HandleNotification(const NMHDR* notification) {
        if (suppressListNotifications_ || notification->code != LVN_ITEMCHANGED) {
            return;
        }
        if (notification->hwndFrom != voiceList_ && notification->hwndFrom != musicList_) {
            return;
        }

        const auto* change = reinterpret_cast<const NMLISTVIEW*>(notification);
        const UINT oldCheck = change->uOldState & LVIS_STATEIMAGEMASK;
        const UINT newCheck = change->uNewState & LVIS_STATEIMAGEMASK;
        if (oldCheck == newCheck || change->iItem < 0) {
            return;
        }

        wchar_t appName[512]{};
        ListView_GetItemText(
            notification->hwndFrom, change->iItem, 0, appName,
            static_cast<int>(std::size(appName)));
        const bool checked = ListView_GetCheckState(notification->hwndFrom, change->iItem) != FALSE;
        auto& own = notification->hwndFrom == voiceList_ ? voiceApps_ : musicApps_;
        auto& other = notification->hwndFrom == voiceList_ ? musicApps_ : voiceApps_;
        const HWND otherList = notification->hwndFrom == voiceList_ ? musicList_ : voiceList_;

        if (checked) {
            own.insert(appName);
            other.erase(appName);
            suppressListNotifications_ = true;
            wchar_t otherName[512]{};
            const int otherCount = ListView_GetItemCount(otherList);
            for (int row = 0; row < otherCount; ++row) {
                ListView_GetItemText(otherList, row, 0, otherName, static_cast<int>(std::size(otherName)));
                if (appName == std::wstring(otherName)) {
                    ListView_SetCheckState(otherList, row, FALSE);
                    break;
                }
            }
            suppressListNotifications_ = false;
        } else {
            own.erase(appName);
        }
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    HWND deviceLabel_ = nullptr;
    HWND deviceCombo_ = nullptr;
    HWND voiceGroup_ = nullptr;
    HWND musicGroup_ = nullptr;
    HWND parameterGroup_ = nullptr;
    HWND voiceList_ = nullptr;
    HWND musicList_ = nullptr;
    HWND status_ = nullptr;
    auto_ducking::WaveformControl voiceWave_;
    auto_ducking::WaveformControl musicWave_;
    std::vector<ParameterControl> parameters_;

    auto_ducking::CoreAudioMonitor monitor_;
    auto_ducking::MonitorSnapshot snapshot_;
    auto_ducking::ActivityDetector detector_;
    auto_ducking::DuckingController duckingController_;
    std::vector<std::wstring> deviceIds_;
    std::vector<std::wstring> deviceLabels_;
    std::wstring selectedDeviceId_;
    std::set<std::wstring> knownApps_;
    std::set<std::wstring> voiceApps_;
    std::set<std::wstring> musicApps_;
    std::vector<std::pair<std::wstring, int>> appSignature_;
    std::map<std::uint32_t, std::unique_ptr<auto_ducking::ProcessLoopbackCapture>> processCaptures_;
    std::uint64_t lastTopologyRefreshMs_ = 0;
    bool suppressListNotifications_ = false;
    bool suppressParameterNotifications_ = false;
};

} // namespace

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    const int showCommand) {
    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    if (!auto_ducking::WaveformControl::Register(instance)) {
        MessageBoxW(nullptr, L"无法注册波形控件。", L"Auto Ducking", MB_OK | MB_ICONERROR);
        return 1;
    }

    try {
        MainWindow application;
        if (!application.Create(instance)) {
            MessageBoxW(nullptr, L"无法创建主窗口。", L"Auto Ducking", MB_OK | MB_ICONERROR);
            return 1;
        }
        return application.Run(showCommand);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Auto Ducking", MB_OK | MB_ICONERROR);
        return 1;
    }
}
