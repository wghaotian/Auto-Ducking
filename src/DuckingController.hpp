#pragma once

#include "CoreAudioMonitor.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace auto_ducking {

struct DuckingConfig {
    float duckFactor = 0.25F;
    std::uint32_t attackMs = 200;
    std::uint32_t releaseMs = 800;
    float externalChangeTolerance = 0.015F;
    std::uint32_t externalChangeGraceMs = 750;
};

using VolumeWriter = std::function<bool(const std::wstring&, float)>;

float InterpolateAttenuation(
    float start,
    float target,
    std::uint64_t elapsedMs,
    std::uint32_t durationMs) noexcept;

class DuckingController final {
public:
    void Update(
        const std::vector<SessionSnapshot>& sessions,
        const std::set<std::wstring>& duckApps,
        bool voiceActive,
        DuckingConfig config,
        std::uint64_t nowMs,
        const VolumeWriter& writer);

    void RestoreAll(const VolumeWriter& writer) noexcept;

    [[nodiscard]] float Attenuation() const noexcept;
    [[nodiscard]] bool IsControlling() const noexcept;
    [[nodiscard]] std::size_t ControlledSessionCount() const noexcept;

private:
    struct ControlledSession {
        float baseline = 1.0F;
        float lastApplied = 1.0F;
        bool hasApplied = false;
        std::uint64_t suppressWritesUntilMs = 0;
        std::uint64_t lastSeenMs = 0;
    };

    void UpdateRamp(bool voiceActive, const DuckingConfig& config, std::uint64_t nowMs);

    std::map<std::wstring, ControlledSession> sessions_;
    float attenuation_ = 1.0F;
    float rampStart_ = 1.0F;
    float rampTarget_ = 1.0F;
    std::uint64_t rampStartMs_ = 0;
    std::uint32_t rampDurationMs_ = 0;
    bool rampInitialized_ = false;
};

} // namespace auto_ducking
