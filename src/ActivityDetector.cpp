#include "ActivityDetector.hpp"

#include <algorithm>

namespace auto_mixer {

ActivityDetector::ActivityDetector(ActivityDetectorConfig config) {
    SetConfig(config);
}

void ActivityDetector::SetConfig(ActivityDetectorConfig config) {
    config.activationThreshold = std::clamp(config.activationThreshold, 0.0F, 1.0F);
    config.releaseThreshold = std::clamp(
        config.releaseThreshold, 0.0F, config.activationThreshold);
    config_ = config;
}

void ActivityDetector::Reset() noexcept {
    active_ = false;
    aboveSince_.reset();
    belowSince_.reset();
}

bool ActivityDetector::Update(const float peak, const std::uint64_t nowMs) noexcept {
    const float boundedPeak = std::clamp(peak, 0.0F, 1.0F);

    if (!active_) {
        belowSince_.reset();
        if (boundedPeak >= config_.activationThreshold) {
            if (!aboveSince_) {
                aboveSince_ = nowMs;
            }
            if (config_.activationMs == 0 || nowMs - *aboveSince_ >= config_.activationMs) {
                active_ = true;
                aboveSince_.reset();
            }
        } else {
            aboveSince_.reset();
        }
    } else {
        aboveSince_.reset();
        if (boundedPeak <= config_.releaseThreshold) {
            if (!belowSince_) {
                belowSince_ = nowMs;
            }
            if (config_.releaseDelayMs == 0 || nowMs - *belowSince_ >= config_.releaseDelayMs) {
                active_ = false;
                belowSince_.reset();
            }
        } else {
            belowSince_.reset();
        }
    }

    return active_;
}

bool ActivityDetector::IsActive() const noexcept {
    return active_;
}

} // namespace auto_mixer

