#include "ProcessLoopbackCapture.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

int wmain(const int argc, wchar_t* argv[]) {
    if (argc < 2) {
        std::wcerr << L"Usage: auto-mixer-process-meter PID [PID ...]\n";
        return 2;
    }

    std::vector<std::unique_ptr<auto_mixer::ProcessLoopbackCapture>> captures;
    for (int index = 1; index < argc; ++index) {
        wchar_t* end = nullptr;
        const unsigned long value = std::wcstoul(argv[index], &end, 10);
        if (end == argv[index] || *end != L'\0' || value == 0) {
            std::wcerr << L"Invalid PID: " << argv[index] << L"\n";
            return 2;
        }
        captures.push_back(std::make_unique<auto_mixer::ProcessLoopbackCapture>(
            static_cast<std::uint32_t>(value)));
    }

    std::wcout << L"Process-loopback peaks (audio is discarded, Ctrl+C to stop)\n";
    for (int sample = 0; sample < 50; ++sample) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        std::wcout << std::setfill(L'0') << std::setw(2) << time.wHour << L":"
                   << std::setw(2) << time.wMinute << L":"
                   << std::setw(2) << time.wSecond << L"." << std::setw(3)
                   << time.wMilliseconds << std::setfill(L' ');
        for (const auto& capture : captures) {
            std::wcout << L"  PID " << capture->ProcessId() << L"="
                       << std::fixed << std::setprecision(4) << capture->Peak();
            const std::wstring error = capture->Error();
            if (!error.empty()) {
                std::wcout << L" [" << error << L"]";
            } else if (!capture->IsRunning()) {
                std::wcout << L" [starting]";
            }
        }
        std::wcout << L"\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}

