#include "ProcessLoopbackCapture.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

int RunProcessLoopbackCaptureTests() {
    int failures = 0;
    const auto check = [&](const bool condition, const char* description) {
        if (!condition) {
            std::cerr << "FAILED: " << description << "\n";
            ++failures;
        }
    };

    const std::int16_t silence[] = {0, 0, 0, 0};
    check(auto_mixer::CalculatePcm16Peak(silence, 4) == 0.0F,
          "PCM peak handles silence");

    const std::int16_t halfScale[] = {100, -16384, 1200, 0};
    check(std::fabs(auto_mixer::CalculatePcm16Peak(halfScale, 4) - 0.5F) < 0.0001F,
          "PCM peak handles signed samples");

    const std::int16_t fullScale[] = {-32768, 32767};
    check(auto_mixer::CalculatePcm16Peak(fullScale, 2) == 1.0F,
          "PCM peak handles negative full scale");
    check(auto_mixer::CalculatePcm16Peak(nullptr, 10) == 0.0F,
          "PCM peak handles a null buffer");

    return failures;
}

