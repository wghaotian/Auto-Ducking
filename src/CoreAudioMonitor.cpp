#include "CoreAudioMonitor.hpp"

#include "Formatting.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace auto_ducking {
namespace {

using Microsoft::WRL::ComPtr;

// Distinguishes this utility's changes from external mixer changes if volume
// notifications are added later.
constexpr GUID kAutoDuckingVolumeContext = {
    0x89be2a79, 0x6aa8, 0x4c23, {0xa4, 0x39, 0x33, 0xb6, 0x6d, 0x4c, 0xcd, 0x4e}};

std::wstring HResultText(const HRESULT result) {
    wchar_t* message = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(
        flags,
        nullptr,
        static_cast<DWORD>(result),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&message),
        0,
        nullptr);

    std::wostringstream stream;
    stream << L"HRESULT 0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result);
    if (length != 0 && message != nullptr) {
        std::wstring text(message, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
            text.pop_back();
        }
        stream << L" (" << text << L")";
    }
    if (message != nullptr) {
        LocalFree(message);
    }
    return stream.str();
}

void ThrowIfFailed(const HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream narrow;
        narrow << operation << " failed with HRESULT 0x" << std::hex << std::uppercase
               << static_cast<unsigned long>(result);
        throw std::runtime_error(narrow.str());
    }
}

std::wstring TakeCoTaskMemString(LPWSTR value) {
    std::wstring result = value != nullptr ? value : L"";
    CoTaskMemFree(value);
    return result;
}

std::wstring DeviceId(IMMDevice* device) {
    LPWSTR id = nullptr;
    ThrowIfFailed(device->GetId(&id), "IMMDevice::GetId");
    return TakeCoTaskMemString(id);
}

std::wstring DeviceName(IMMDevice* device) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
        return L"Unknown output device";
    }

    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT result = properties->GetValue(PKEY_Device_FriendlyName, &value);
    std::wstring name = L"Unknown output device";
    if (SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

std::wstring DefaultDeviceId(IMMDeviceEnumerator* enumerator, const ERole role) {
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, role, &device))) {
        return L"";
    }
    try {
        return DeviceId(device.Get());
    } catch (...) {
        return L"";
    }
}

std::wstring GetProcessPath(const DWORD processId) {
    if (processId == 0) {
        return L"";
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        return L"";
    }

    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL succeeded = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!succeeded) {
        return L"";
    }

    path.resize(length);
    return path;
}

std::unordered_map<DWORD, std::wstring> ReadProcessNames() {
    std::unordered_map<DWORD, std::wstring> result;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            result.emplace(entry.th32ProcessID, entry.szExeFile);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

std::wstring GetControlString(
    IAudioSessionControl2* control,
    HRESULT(STDMETHODCALLTYPE IAudioSessionControl2::*getter)(LPWSTR*)) {
    LPWSTR value = nullptr;
    if (FAILED((control->*getter)(&value))) {
        return L"";
    }
    return TakeCoTaskMemString(value);
}

std::wstring GetDisplayName(IAudioSessionControl* control) {
    LPWSTR value = nullptr;
    if (FAILED(control->GetDisplayName(&value))) {
        return L"";
    }
    return TakeCoTaskMemString(value);
}

SessionState ConvertState(const AudioSessionState state) noexcept {
    switch (state) {
    case AudioSessionStateInactive:
        return SessionState::Inactive;
    case AudioSessionStateActive:
        return SessionState::Active;
    case AudioSessionStateExpired:
        return SessionState::Expired;
    default:
        return SessionState::Unknown;
    }
}

struct SessionHandle {
    std::wstring key;
    std::wstring instanceId;
    std::wstring sessionIdentifier;
    std::wstring displayName;
    DWORD processId = 0;
    std::wstring processName;
    std::wstring processPath;
    bool systemSounds = false;
    ComPtr<IAudioSessionControl> control;
    ComPtr<ISimpleAudioVolume> volume;
    ComPtr<IAudioMeterInformation> meter;
};

struct DeviceHandle {
    std::wstring id;
    std::wstring name;
    bool defaultConsole = false;
    bool defaultMultimedia = false;
    bool defaultCommunications = false;
    std::vector<SessionHandle> sessions;
};

SessionHandle ReadSessionHandle(
    const std::wstring& deviceId,
    IAudioSessionControl* control,
    const int fallbackIndex,
    const std::unordered_map<DWORD, std::wstring>& processNames) {
    SessionHandle handle;
    handle.control = control;

    ComPtr<IAudioSessionControl2> control2;
    ThrowIfFailed(control->QueryInterface(IID_PPV_ARGS(&control2)),
                  "IAudioSessionControl2::QueryInterface");

    control2->GetProcessId(&handle.processId);
    handle.instanceId = GetControlString(
        control2.Get(), &IAudioSessionControl2::GetSessionInstanceIdentifier);
    handle.sessionIdentifier = GetControlString(
        control2.Get(), &IAudioSessionControl2::GetSessionIdentifier);
    handle.displayName = GetDisplayName(control);
    handle.systemSounds = control2->IsSystemSoundsSession() == S_OK;
    handle.processPath = GetProcessPath(handle.processId);
    handle.processName = BaseName(handle.processPath);
    if (handle.systemSounds) {
        handle.processName = L"System Sounds";
    } else if (handle.processName.empty()) {
        const auto process = processNames.find(handle.processId);
        if (process != processNames.end()) {
            handle.processName = process->second;
        }
    }
    if (handle.processName.empty()) {
        handle.processName = L"<process unavailable>";
    }

    std::wostringstream fallback;
    fallback << deviceId << L"|pid=" << handle.processId << L"|index=" << fallbackIndex;
    handle.key = deviceId + L"|" +
        (handle.instanceId.empty() ? fallback.str() : handle.instanceId);

    control->QueryInterface(IID_PPV_ARGS(&handle.volume));
    control->QueryInterface(IID_PPV_ARGS(&handle.meter));
    return handle;
}

} // namespace

class CoreAudioMonitor::Impl final {
public:
    explicit Impl(const bool defaultDeviceOnly)
        : defaultDeviceOnly_(defaultDeviceOnly) {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
            ThrowIfFailed(init, "CoInitializeEx");
        }
        uninitializeCom_ = SUCCEEDED(init);

        ThrowIfFailed(CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator_)),
            "CoCreateInstance(MMDeviceEnumerator)");
    }

    ~Impl() {
        devices_.clear();
        enumerator_.Reset();
        if (uninitializeCom_) {
            CoUninitialize();
        }
    }

    void RefreshTopology() {
        std::vector<DeviceHandle> updated;
        std::vector<std::wstring> warnings;
        const auto processNames = ReadProcessNames();

        const std::wstring defaultConsole = DefaultDeviceId(enumerator_.Get(), eConsole);
        const std::wstring defaultMultimedia = DefaultDeviceId(enumerator_.Get(), eMultimedia);
        const std::wstring defaultCommunications = DefaultDeviceId(enumerator_.Get(), eCommunications);

        ComPtr<IMMDeviceCollection> collection;
        const HRESULT enumerateResult = enumerator_->EnumAudioEndpoints(
            eRender, DEVICE_STATE_ACTIVE, &collection);
        if (FAILED(enumerateResult)) {
            warnings.push_back(L"Could not enumerate active output devices: " +
                               HResultText(enumerateResult));
            devices_.clear();
            warnings_ = std::move(warnings);
            return;
        }

        UINT deviceCount = 0;
        collection->GetCount(&deviceCount);
        for (UINT deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex) {
            ComPtr<IMMDevice> device;
            if (FAILED(collection->Item(deviceIndex, &device))) {
                continue;
            }

            DeviceHandle output;
            try {
                output.id = DeviceId(device.Get());
            } catch (const std::exception&) {
                warnings.push_back(L"An output device had no readable endpoint ID.");
                continue;
            }
            output.defaultConsole = output.id == defaultConsole;
            output.defaultMultimedia = output.id == defaultMultimedia;
            output.defaultCommunications = output.id == defaultCommunications;
            if (defaultDeviceOnly_ && !output.defaultMultimedia) {
                continue;
            }
            output.name = DeviceName(device.Get());

            ComPtr<IAudioSessionManager2> manager;
            const HRESULT activateResult = device->Activate(
                __uuidof(IAudioSessionManager2),
                CLSCTX_ALL,
                nullptr,
                reinterpret_cast<void**>(manager.GetAddressOf()));
            if (FAILED(activateResult)) {
                warnings.push_back(L"Could not open sessions for " + output.name + L": " +
                                   HResultText(activateResult));
                updated.push_back(std::move(output));
                continue;
            }

            ComPtr<IAudioSessionEnumerator> sessions;
            const HRESULT sessionResult = manager->GetSessionEnumerator(&sessions);
            if (FAILED(sessionResult)) {
                warnings.push_back(L"Could not enumerate sessions for " + output.name + L": " +
                                   HResultText(sessionResult));
                updated.push_back(std::move(output));
                continue;
            }

            int sessionCount = 0;
            sessions->GetCount(&sessionCount);
            for (int sessionIndex = 0; sessionIndex < sessionCount; ++sessionIndex) {
                ComPtr<IAudioSessionControl> control;
                if (FAILED(sessions->GetSession(sessionIndex, &control))) {
                    continue;
                }
                try {
                    output.sessions.push_back(
                        ReadSessionHandle(output.id, control.Get(), sessionIndex, processNames));
                } catch (const std::exception&) {
                    warnings.push_back(L"Skipped one unreadable session on " + output.name + L".");
                }
            }
            updated.push_back(std::move(output));
        }

        devices_ = std::move(updated);
        warnings_ = std::move(warnings);
    }

    [[nodiscard]] MonitorSnapshot Sample() const {
        MonitorSnapshot snapshot;
        snapshot.warnings = warnings_;
        snapshot.devices.reserve(devices_.size());

        for (const auto& device : devices_) {
            DeviceSnapshot deviceSnapshot;
            deviceSnapshot.id = device.id;
            deviceSnapshot.name = device.name;
            deviceSnapshot.defaultConsole = device.defaultConsole;
            deviceSnapshot.defaultMultimedia = device.defaultMultimedia;
            deviceSnapshot.defaultCommunications = device.defaultCommunications;
            deviceSnapshot.sessions.reserve(device.sessions.size());

            for (const auto& session : device.sessions) {
                SessionSnapshot item;
                item.key = session.key;
                item.instanceId = session.instanceId;
                item.sessionIdentifier = session.sessionIdentifier;
                item.displayName = session.displayName;
                item.processId = session.processId;
                item.processName = session.processName;
                item.processPath = session.processPath;
                item.systemSounds = session.systemSounds;

                AudioSessionState state = AudioSessionStateInactive;
                if (SUCCEEDED(session.control->GetState(&state))) {
                    item.state = ConvertState(state);
                }
                if (session.volume) {
                    session.volume->GetMasterVolume(&item.volume);
                    BOOL muted = FALSE;
                    session.volume->GetMute(&muted);
                    item.muted = muted != FALSE;
                }
                if (session.meter) {
                    session.meter->GetPeakValue(&item.peak);
                }
                deviceSnapshot.sessions.push_back(std::move(item));
            }
            snapshot.devices.push_back(std::move(deviceSnapshot));
        }
        return snapshot;
    }

    bool SetSessionVolume(const std::wstring& sessionKey, const float volume) noexcept {
        const float boundedVolume = std::clamp(volume, 0.0F, 1.0F);
        for (auto& device : devices_) {
            const auto session = std::find_if(
                device.sessions.begin(), device.sessions.end(), [&](const auto& candidate) {
                    return candidate.key == sessionKey;
                });
            if (session != device.sessions.end() && session->volume) {
                return SUCCEEDED(session->volume->SetMasterVolume(
                    boundedVolume, &kAutoDuckingVolumeContext));
            }
        }
        return false;
    }

private:
    bool defaultDeviceOnly_ = false;
    bool uninitializeCom_ = false;
    ComPtr<IMMDeviceEnumerator> enumerator_;
    std::vector<DeviceHandle> devices_;
    std::vector<std::wstring> warnings_;
};

CoreAudioMonitor::CoreAudioMonitor(const bool defaultDeviceOnly)
    : impl_(std::make_unique<Impl>(defaultDeviceOnly)) {}

CoreAudioMonitor::~CoreAudioMonitor() = default;

void CoreAudioMonitor::RefreshTopology() {
    impl_->RefreshTopology();
}

MonitorSnapshot CoreAudioMonitor::Sample() const {
    return impl_->Sample();
}

bool CoreAudioMonitor::SetSessionVolume(
    const std::wstring& sessionKey,
    const float volume) noexcept {
    return impl_->SetSessionVolume(sessionKey, volume);
}

const wchar_t* ToString(const SessionState state) noexcept {
    switch (state) {
    case SessionState::Inactive:
        return L"inactive";
    case SessionState::Active:
        return L"active";
    case SessionState::Expired:
        return L"expired";
    default:
        return L"unknown";
    }
}

} // namespace auto_ducking
