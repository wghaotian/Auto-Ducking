#include "Formatting.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(const bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAILED: " << description << "\n";
        ++failures;
    }
}

} // namespace

int RunActivityDetectorTests();
int RunDuckingControllerTests();
int RunProcessLoopbackCaptureTests();

int main() {
    using auto_mixer::BaseName;
    using auto_mixer::FormatPeakBar;
    using auto_mixer::Truncate;

    Check(BaseName(LR"(C:\Program Files\Spotify\Spotify.exe)") == L"Spotify.exe",
          "BaseName handles Windows paths");
    Check(BaseName(L"Discord.exe") == L"Discord.exe",
          "BaseName handles a filename");
    Check(FormatPeakBar(0.0F, 5) == L".....", "zero peak bar");
    Check(FormatPeakBar(0.5F, 6) == L"###...", "half peak bar");
    Check(FormatPeakBar(2.0F, 4) == L"####", "peak bar clamps high values");
    Check(FormatPeakBar(-1.0F, 4) == L"....", "peak bar clamps low values");
    Check(Truncate(L"short", 8) == L"short", "short text is unchanged");
    Check(Truncate(L"abcdefgh", 6) == L"abc...", "long text is truncated");

    failures += RunActivityDetectorTests();
    failures += RunDuckingControllerTests();
    failures += RunProcessLoopbackCaptureTests();
    if (failures == 0) {
        std::cout << "All unit tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
