#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace auto_ducking {

enum class SessionState {
    Inactive,
    Active,
    Expired,
    Unknown,
};

struct SessionSnapshot {
    std::wstring key;
    std::wstring instanceId;
    std::wstring sessionIdentifier;
    std::wstring displayName;
    std::uint32_t processId = 0;
    std::wstring processName;
    std::wstring processPath;
    float volume = 0.0F;
    // Diagnostic session-control meter value. This is not guaranteed to be
    // process-isolated and can mirror the endpoint mix on some systems.
    float peak = 0.0F;
    bool muted = false;
    bool systemSounds = false;
    SessionState state = SessionState::Unknown;
};

struct DeviceSnapshot {
    std::wstring id;
    std::wstring name;
    bool defaultConsole = false;
    bool defaultMultimedia = false;
    bool defaultCommunications = false;
    std::vector<SessionSnapshot> sessions;
};

struct MonitorSnapshot {
    std::vector<DeviceSnapshot> devices;
    std::vector<std::wstring> warnings;
};

class CoreAudioMonitor final {
public:
    explicit CoreAudioMonitor(bool defaultDeviceOnly);
    ~CoreAudioMonitor();

    CoreAudioMonitor(const CoreAudioMonitor&) = delete;
    CoreAudioMonitor& operator=(const CoreAudioMonitor&) = delete;

    void RefreshTopology();
    [[nodiscard]] MonitorSnapshot Sample() const;
    bool SetSessionVolume(const std::wstring& sessionKey, float volume) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

const wchar_t* ToString(SessionState state) noexcept;

} // namespace auto_ducking
