#include "DuckingController.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

int RunDuckingControllerTests() {
    int failures = 0;
    const auto check = [&](const bool condition, const char* description) {
        if (!condition) {
            std::cerr << "FAILED: " << description << "\n";
            ++failures;
        }
    };

    check(std::fabs(auto_mixer::InterpolateAttenuation(1.0F, 0.25F, 100, 200) - 0.625F) < 0.0001F,
          "attack interpolation reaches halfway");
    check(auto_mixer::InterpolateAttenuation(0.25F, 1.0F, 800, 800) == 1.0F,
          "release interpolation reaches target");

    auto_mixer::SessionSnapshot music;
    music.key = L"device|spotify-session";
    music.processName = L"Spotify.exe";
    music.volume = 0.8F;
    music.state = auto_mixer::SessionState::Active;
    std::vector<auto_mixer::SessionSnapshot> sessions{music};
    const std::set<std::wstring> duckApps{L"Spotify.exe"};
    std::map<std::wstring, float> writes;
    const auto writer = [&](const std::wstring& key, const float value) {
        writes[key] = value;
        return true;
    };

    auto_mixer::DuckingController controller;
    const auto_mixer::DuckingConfig config{0.25F, 200, 800, 0.015F, 750};
    controller.Update(sessions, duckApps, false, config, 0, writer);
    check(writes.empty(), "inactive controller does not touch volume");

    controller.Update(sessions, duckApps, true, config, 1000, writer);
    check(std::fabs(controller.Attenuation() - 1.0F) < 0.0001F,
          "attack begins from baseline");
    controller.Update(sessions, duckApps, true, config, 1100, writer);
    check(std::fabs(writes[music.key] - 0.5F) < 0.0001F,
          "half attack preserves per-session baseline");
    sessions[0].volume = writes[music.key];
    controller.Update(sessions, duckApps, true, config, 1200, writer);
    check(std::fabs(writes[music.key] - 0.2F) < 0.0001F,
          "full attack applies duck factor");

    sessions[0].volume = writes[music.key];
    controller.Update(sessions, duckApps, false, config, 2000, writer);
    controller.Update(sessions, duckApps, false, config, 2400, writer);
    check(std::fabs(writes[music.key] - 0.5F) < 0.0001F,
          "release reaches halfway");
    sessions[0].volume = writes[music.key];
    controller.Update(sessions, duckApps, false, config, 2800, writer);
    check(std::fabs(writes[music.key] - 0.8F) < 0.0001F,
          "release restores original session volume");
    check(!controller.IsControlling(), "restored session is released from control");

    auto_mixer::DuckingController rebaseController;
    writes.clear();
    sessions[0].volume = 0.8F;
    rebaseController.Update(sessions, duckApps, false, config, 0, writer);
    rebaseController.Update(sessions, duckApps, true, config, 1000, writer);
    rebaseController.Update(sessions, duckApps, true, config, 1200, writer);
    sessions[0].volume = 0.1F;
    writes.clear();
    rebaseController.Update(sessions, duckApps, true, config, 1300, writer);
    check(writes.empty(), "external volume change pauses controller writes");
    rebaseController.Update(sessions, duckApps, true, config, 2050, writer);
    check(std::fabs(writes[music.key] - 0.1F) < 0.0001F,
          "external ducked volume becomes the new baseline basis");
    rebaseController.RestoreAll(writer);
    check(std::fabs(writes[music.key] - 0.4F) < 0.0001F,
          "shutdown restores the externally rebased baseline");

    return failures;
}
