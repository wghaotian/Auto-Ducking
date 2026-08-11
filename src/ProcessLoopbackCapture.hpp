#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace auto_ducking {

float CalculatePcm16Peak(const std::int16_t* samples, std::size_t sampleCount) noexcept;

// Captures only the render streams owned by one process tree, computes a peak
// in memory, and immediately releases every WASAPI buffer. No audio is stored.
class ProcessLoopbackCapture final {
public:
    explicit ProcessLoopbackCapture(std::uint32_t processId);
    ~ProcessLoopbackCapture();

    ProcessLoopbackCapture(const ProcessLoopbackCapture&) = delete;
    ProcessLoopbackCapture& operator=(const ProcessLoopbackCapture&) = delete;

    [[nodiscard]] std::uint32_t ProcessId() const noexcept;
    [[nodiscard]] float Peak() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] std::wstring Error() const;

private:
    void Run();
    void SetError(HRESULT result, const wchar_t* operation);

    std::uint32_t processId_ = 0;
    HANDLE stopEvent_ = nullptr;
    std::thread worker_;
    std::atomic<float> peak_{0.0F};
    std::atomic<bool> running_{false};
    mutable std::mutex errorMutex_;
    std::wstring error_;
};

} // namespace auto_ducking
