#include "CoreAudioMonitor.hpp"
#include "Formatting.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

std::atomic_bool g_stopRequested = false;

BOOL WINAPI ConsoleControlHandler(const DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
        signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        g_stopRequested.store(true);
        return TRUE;
    }
    return FALSE;
}

struct Options {
    bool once = false;
    bool defaultDeviceOnly = false;
    int sampleIntervalMs = 100;
    int topologyIntervalMs = 1000;
};

void PrintHelp() {
    std::wcout
        << L"Auto Ducking - Core Audio session diagnostics\n\n"
        << L"Usage: auto-ducking-diagnostics [options]\n\n"
        << L"Options:\n"
        << L"  --once                  Print one snapshot and exit\n"
        << L"  --default-device-only   Inspect only the default multimedia output\n"
        << L"  --interval-ms N         Peak sampling/display interval (50-5000, default 100)\n"
        << L"  --topology-ms N         Device/session rediscovery interval (100-30000, default 1000)\n"
        << L"  --help                   Show this help\n\n"
        << L"By default all active render endpoints are inspected. Press Ctrl+C to exit.\n";
}

int ParseInt(const wchar_t* text, const wchar_t* option, const int low, const int high) {
    wchar_t* end = nullptr;
    const long value = std::wcstol(text, &end, 10);
    if (end == text || *end != L'\0' || value < low || value > high) {
        std::wcerr << L"Invalid value for " << option << L" (expected "
                   << low << L"-" << high << L").\n";
        std::exit(2);
    }
    return static_cast<int>(value);
}

Options ParseOptions(const int argc, wchar_t* argv[]) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--help" || argument == L"-h") {
            PrintHelp();
            std::exit(0);
        }
        if (argument == L"--once") {
            options.once = true;
        } else if (argument == L"--default-device-only") {
            options.defaultDeviceOnly = true;
        } else if (argument == L"--interval-ms" && index + 1 < argc) {
            options.sampleIntervalMs = ParseInt(argv[++index], L"--interval-ms", 50, 5000);
        } else if (argument == L"--topology-ms" && index + 1 < argc) {
            options.topologyIntervalMs = ParseInt(argv[++index], L"--topology-ms", 100, 30000);
        } else {
            std::wcerr << L"Unknown or incomplete option: " << argument << L"\n";
            std::wcerr << L"Use --help for usage.\n";
            std::exit(2);
        }
    }
    return options;
}

bool EnableVirtualTerminal() {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output == INVALID_HANDLE_VALUE || !GetConsoleMode(output, &mode)) {
        return false;
    }
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != FALSE;
}

std::wstring DeviceRoles(const auto_ducking::DeviceSnapshot& device) {
    std::wstring result;
    if (device.defaultConsole) {
        result += L"console,";
    }
    if (device.defaultMultimedia) {
        result += L"multimedia,";
    }
    if (device.defaultCommunications) {
        result += L"communications,";
    }
    if (!result.empty()) {
        result.pop_back();
    }
    return result.empty() ? L"non-default" : result;
}

void Render(const auto_ducking::MonitorSnapshot& snapshot, const bool clearScreen) {
    if (clearScreen) {
        std::wcout << L"\x1b[2J\x1b[H";
    }

    std::wcout << L"Auto Ducking - output session diagnostics"
               << L"  (Ctrl+C to exit)\n";
    std::wcout << L"Devices: " << snapshot.devices.size()
               << L"  Updated: " << std::flush;

    SYSTEMTIME time;
    GetLocalTime(&time);
    std::wcout << std::setfill(L'0') << std::setw(2) << time.wHour << L":"
               << std::setw(2) << time.wMinute << L":"
               << std::setw(2) << time.wSecond << std::setfill(L' ') << L"\n\n";

    for (const auto& warning : snapshot.warnings) {
        std::wcout << L"[warning] " << warning << L"\n";
    }
    if (!snapshot.warnings.empty()) {
        std::wcout << L"\n";
    }

    for (const auto& device : snapshot.devices) {
        std::wcout << L"[Audio] " << device.name << L" [" << DeviceRoles(device) << L"]\n";
        std::wcout << L"  PID      STATE     VOL   MUTE  PEAK*    LEVEL                PROCESS / SESSION\n";
        if (device.sessions.empty()) {
            std::wcout << L"  (no audio sessions)\n";
        }

        for (const auto& session : device.sessions) {
            const std::wstring label = !session.displayName.empty()
                ? session.processName + L" (" + session.displayName + L")"
                : session.processName;
            std::wcout << L"  " << std::right << std::setw(7) << session.processId << L"  "
                       << std::left << std::setw(8) << auto_ducking::ToString(session.state) << L"  "
                       << std::right << std::fixed << std::setprecision(0)
                       << std::setw(3) << session.volume * 100.0F << L"%  "
                       << std::left << std::setw(4) << (session.muted ? L"yes" : L"no") << L"  "
                       << std::right << std::setprecision(3) << std::setw(5) << session.peak << L"  "
                       << auto_ducking::FormatPeakBar(session.peak, 20) << L"  "
                       << auto_ducking::Truncate(label, 48) << L"\n";
            std::wcout << L"           instance: "
                       << auto_ducking::Truncate(session.instanceId, 100) << L"\n";
        }
        std::wcout << L"\n";
    }

    if (snapshot.devices.empty()) {
        std::wcout << L"No active Windows output endpoints were found. The monitor will retry.\n";
    }
    std::wcout << L"* Session-control PEAK can mirror the endpoint mix. "
                  L"The UI uses process-isolated loopback metering.\n";
    std::wcout.flush();
}

} // namespace

int wmain(const int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    const Options options = ParseOptions(argc, argv);
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
    const bool interactiveConsole = EnableVirtualTerminal();

    try {
        auto_ducking::CoreAudioMonitor monitor(options.defaultDeviceOnly);
        monitor.RefreshTopology();

        auto nextTopologyRefresh = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(options.topologyIntervalMs);
        bool firstFrame = true;

        do {
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextTopologyRefresh) {
                monitor.RefreshTopology();
                nextTopologyRefresh = now + std::chrono::milliseconds(options.topologyIntervalMs);
            }

            Render(monitor.Sample(), interactiveConsole && !firstFrame);
            firstFrame = false;
            if (options.once) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(options.sampleIntervalMs));
        } while (!g_stopRequested.load());

        if (interactiveConsole && !options.once) {
            std::wcout << L"\nStopped. Phase 1 never changes session volume; no restoration was needed.\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << "\n";
        std::cerr << "No volume changes are performed by this Phase 1 diagnostic.\n";
        return 1;
    }
}
