#include "DuckingController.hpp"

#include <algorithm>
#include <cmath>

namespace auto_mixer {
namespace {

constexpr float kMinimumRebaseAttenuation = 0.05F;
constexpr float kWriteEpsilon = 0.002F;
constexpr std::uint64_t kMissingSessionRetentionMs = 5000;

} // namespace

float InterpolateAttenuation(
    const float start,
    const float target,
    const std::uint64_t elapsedMs,
    const std::uint32_t durationMs) noexcept {
    const float boundedStart = std::clamp(start, 0.0F, 1.0F);
    const float boundedTarget = std::clamp(target, 0.0F, 1.0F);
    if (durationMs == 0 || elapsedMs >= durationMs) {
        return boundedTarget;
    }
    const float progress = static_cast<float>(elapsedMs) / static_cast<float>(durationMs);
    return boundedStart + (boundedTarget - boundedStart) * progress;
}

void DuckingController::UpdateRamp(
    const bool voiceActive,
    const DuckingConfig& config,
    const std::uint64_t nowMs) {
    const float desiredTarget = voiceActive ? config.duckFactor : 1.0F;
    const std::uint32_t desiredDuration = voiceActive ? config.attackMs : config.releaseMs;

    if (!rampInitialized_) {
        rampInitialized_ = true;
        rampStartMs_ = nowMs;
        rampStart_ = attenuation_;
        rampTarget_ = desiredTarget;
        rampDurationMs_ = desiredDuration;
    } else {
        attenuation_ = InterpolateAttenuation(
            rampStart_, rampTarget_, nowMs - rampStartMs_, rampDurationMs_);
        if (std::fabs(desiredTarget - rampTarget_) > kWriteEpsilon ||
            desiredDuration != rampDurationMs_) {
            rampStart_ = attenuation_;
            rampTarget_ = desiredTarget;
            rampStartMs_ = nowMs;
            rampDurationMs_ = desiredDuration;
        }
    }

    attenuation_ = InterpolateAttenuation(
        rampStart_, rampTarget_, nowMs - rampStartMs_, rampDurationMs_);
}

void DuckingController::Update(
    const std::vector<SessionSnapshot>& sessions,
    const std::set<std::wstring>& duckApps,
    const bool voiceActive,
    DuckingConfig config,
    const std::uint64_t nowMs,
    const VolumeWriter& writer) {
    config.duckFactor = std::clamp(config.duckFactor, 0.0F, 1.0F);
    config.externalChangeTolerance = std::clamp(
        config.externalChangeTolerance, 0.001F, 0.25F);
    UpdateRamp(voiceActive, config, nowMs);

    std::set<std::wstring> targetKeys;
    if (attenuation_ < 1.0F - kWriteEpsilon || !sessions_.empty()) {
        for (const auto& session : sessions) {
            if (!session.systemSounds && session.state != SessionState::Expired &&
                duckApps.count(session.processName) != 0) {
                targetKeys.insert(session.key);
                auto [entry, inserted] = sessions_.try_emplace(session.key);
                ControlledSession& controlled = entry->second;
                if (inserted) {
                    controlled.baseline = std::clamp(session.volume, 0.0F, 1.0F);
                    controlled.lastApplied = controlled.baseline;
                } else if (controlled.hasApplied &&
                           std::fabs(session.volume - controlled.lastApplied) >
                               config.externalChangeTolerance) {
                    controlled.baseline = std::clamp(
                        session.volume / std::max(attenuation_, kMinimumRebaseAttenuation),
                        0.0F,
                        1.0F);
                    controlled.hasApplied = false;
                    controlled.suppressWritesUntilMs = nowMs + config.externalChangeGraceMs;
                }
                controlled.lastSeenMs = nowMs;

                if (nowMs < controlled.suppressWritesUntilMs) {
                    continue;
                }
                const float desiredVolume = std::clamp(
                    controlled.baseline * attenuation_, 0.0F, 1.0F);
                if (!controlled.hasApplied ||
                    std::fabs(session.volume - desiredVolume) > kWriteEpsilon) {
                    if (writer(session.key, desiredVolume)) {
                        controlled.lastApplied = desiredVolume;
                        controlled.hasApplied = true;
                    }
                }
            }
        }
    }

    for (auto controlled = sessions_.begin(); controlled != sessions_.end();) {
        if (targetKeys.count(controlled->first) != 0) {
            ++controlled;
            continue;
        }
        const bool restored = writer(controlled->first, controlled->second.baseline);
        if (restored || nowMs - controlled->second.lastSeenMs > kMissingSessionRetentionMs) {
            controlled = sessions_.erase(controlled);
        } else {
            ++controlled;
        }
    }

    if (!voiceActive && attenuation_ >= 1.0F - kWriteEpsilon) {
        for (auto controlled = sessions_.begin(); controlled != sessions_.end();) {
            if (writer(controlled->first, controlled->second.baseline)) {
                controlled = sessions_.erase(controlled);
            } else {
                ++controlled;
            }
        }
        attenuation_ = 1.0F;
    }
}

void DuckingController::RestoreAll(const VolumeWriter& writer) noexcept {
    for (const auto& [key, controlled] : sessions_) {
        try {
            writer(key, controlled.baseline);
        } catch (...) {
            // Best-effort shutdown restoration must not block remaining sessions.
        }
    }
    sessions_.clear();
    attenuation_ = 1.0F;
    rampStart_ = 1.0F;
    rampTarget_ = 1.0F;
    rampDurationMs_ = 0;
    rampInitialized_ = false;
}

float DuckingController::Attenuation() const noexcept {
    return attenuation_;
}

bool DuckingController::IsControlling() const noexcept {
    return !sessions_.empty();
}

std::size_t DuckingController::ControlledSessionCount() const noexcept {
    return sessions_.size();
}

} // namespace auto_mixer

