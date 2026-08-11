#include "ActivityDetector.hpp"

#include <iostream>

int RunActivityDetectorTests() {
    int failures = 0;
    const auto check = [&](const bool condition, const char* description) {
        if (!condition) {
            std::cerr << "FAILED: " << description << "\n";
            ++failures;
        }
    };

    auto_ducking::ActivityDetector detector({0.02F, 0.01F, 100, 800});
    check(!detector.Update(0.03F, 0), "activation does not happen immediately");
    check(!detector.Update(0.03F, 99), "activation dwell is respected");
    check(detector.Update(0.03F, 100), "activation occurs after dwell");
    check(detector.Update(0.015F, 500), "hysteresis band keeps activity active");
    check(detector.Update(0.0F, 600), "release delay starts below threshold");
    check(detector.Update(0.0F, 1399), "release delay remains active until elapsed");
    check(!detector.Update(0.0F, 1400), "release occurs after delay");

    detector.Reset();
    check(!detector.Update(0.03F, 2000), "reset clears active state");
    check(!detector.Update(0.0F, 2050), "short spike does not activate");

    detector.SetConfig({0.2F, 0.8F, 0, 0});
    check(detector.Update(0.2F, 3000), "zero activation delay is immediate");
    check(!detector.Update(0.2F, 3001), "release threshold is clamped to activation threshold");

    return failures;
}
