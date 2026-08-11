#pragma once

#include <cstdint>
#include <optional>

namespace auto_ducking {

struct ActivityDetectorConfig {
    float activationThreshold = 0.02F;
    float releaseThreshold = 0.01F;
    std::uint32_t activationMs = 100;
    std::uint32_t releaseDelayMs = 800;
};

class ActivityDetector final {
public:
    explicit ActivityDetector(ActivityDetectorConfig config = {});

    void SetConfig(ActivityDetectorConfig config);
    void Reset() noexcept;
    bool Update(float peak, std::uint64_t nowMs) noexcept;

    [[nodiscard]] bool IsActive() const noexcept;

private:
    ActivityDetectorConfig config_;
    bool active_ = false;
    std::optional<std::uint64_t> aboveSince_;
    std::optional<std::uint64_t> belowSince_;
};

} // namespace auto_ducking
