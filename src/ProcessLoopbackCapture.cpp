#include "ProcessLoopbackCapture.hpp"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

// The installed 10.0.19041 SDK predates the public process-loopback header,
// while the target OS is Windows 11. These declarations match the current
// Microsoft audioclientactivationparams.h ABI.
#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
enum PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1,
};
struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
};
enum AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1,
};
struct AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union {
        AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
    };
};
#endif

namespace auto_ducking {
namespace {

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::FtmBase;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;

class ActivationHandler final : public RuntimeClass<
    RuntimeClassFlags<ClassicCom>, FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    ActivationHandler() {
        event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    ~ActivationHandler() override {
        if (event_ != nullptr) {
            CloseHandle(event_);
        }
    }

    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        ComPtr<IUnknown> activated;
        HRESULT activateResult = E_UNEXPECTED;
        result_ = operation->GetActivateResult(&activateResult, &activated);
        if (SUCCEEDED(result_)) {
            result_ = activateResult;
        }
        if (SUCCEEDED(result_)) {
            result_ = activated.As(&audioClient_);
        }
        SetEvent(event_);
        return S_OK;
    }

    [[nodiscard]] HANDLE Event() const noexcept {
        return event_;
    }

    [[nodiscard]] HRESULT Result() const noexcept {
        return result_;
    }

    [[nodiscard]] ComPtr<IAudioClient> AudioClient() const {
        return audioClient_;
    }

private:
    HANDLE event_ = nullptr;
    HRESULT result_ = E_PENDING;
    ComPtr<IAudioClient> audioClient_;
};

} // namespace

float CalculatePcm16Peak(
    const std::int16_t* samples,
    const std::size_t sampleCount) noexcept {
    if (samples == nullptr || sampleCount == 0) {
        return 0.0F;
    }

    int maximum = 0;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        const int magnitude = std::abs(static_cast<int>(samples[index]));
        maximum = std::max(maximum, magnitude);
    }
    return static_cast<float>(maximum) / 32768.0F;
}

ProcessLoopbackCapture::ProcessLoopbackCapture(const std::uint32_t processId)
    : processId_(processId) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent_ == nullptr) {
        SetError(HRESULT_FROM_WIN32(GetLastError()), L"CreateEvent(stop)");
        return;
    }
    worker_ = std::thread(&ProcessLoopbackCapture::Run, this);
}

ProcessLoopbackCapture::~ProcessLoopbackCapture() {
    if (stopEvent_ != nullptr) {
        SetEvent(stopEvent_);
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    if (stopEvent_ != nullptr) {
        CloseHandle(stopEvent_);
    }
}

std::uint32_t ProcessLoopbackCapture::ProcessId() const noexcept {
    return processId_;
}

float ProcessLoopbackCapture::Peak() const noexcept {
    return peak_.load(std::memory_order_relaxed);
}

bool ProcessLoopbackCapture::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

std::wstring ProcessLoopbackCapture::Error() const {
    const std::lock_guard<std::mutex> lock(errorMutex_);
    return error_;
}

void ProcessLoopbackCapture::SetError(
    const HRESULT result,
    const wchar_t* operation) {
    std::wostringstream message;
    message << operation << L" failed (0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(result) << L")";
    const std::lock_guard<std::mutex> lock(errorMutex_);
    error_ = message.str();
}

void ProcessLoopbackCapture::Run() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        SetError(comResult, L"CoInitializeEx");
        return;
    }

    const auto finish = [&]() {
        running_.store(false, std::memory_order_release);
        peak_.store(0.0F, std::memory_order_relaxed);
        if (uninitializeCom) {
            CoUninitialize();
        }
    };

    auto handler = Microsoft::WRL::Make<ActivationHandler>();
    if (!handler || handler->Event() == nullptr) {
        SetError(E_OUTOFMEMORY, L"ActivationHandler");
        finish();
        return;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS activation{};
    activation.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activation.ProcessLoopbackParams.TargetProcessId = processId_;
    activation.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT parameters{};
    parameters.vt = VT_BLOB;
    parameters.blob.cbSize = sizeof(activation);
    parameters.blob.pBlobData = reinterpret_cast<BYTE*>(&activation);

    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT result = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &parameters,
        handler.Get(),
        &operation);
    if (FAILED(result)) {
        SetError(result, L"ActivateAudioInterfaceAsync");
        finish();
        return;
    }

    HANDLE activationWaits[] = {stopEvent_, handler->Event()};
    const DWORD activationWait = WaitForMultipleObjects(2, activationWaits, FALSE, 5000);
    if (activationWait == WAIT_OBJECT_0) {
        finish();
        return;
    }
    if (activationWait != WAIT_OBJECT_0 + 1) {
        SetError(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Process-loopback activation");
        finish();
        return;
    }
    if (FAILED(handler->Result())) {
        SetError(handler->Result(), L"Process-loopback activation");
        finish();
        return;
    }

    ComPtr<IAudioClient> audioClient = handler->AudioClient();
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    result = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK |
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        0,
        0,
        &format,
        nullptr);
    if (FAILED(result)) {
        SetError(result, L"IAudioClient::Initialize");
        finish();
        return;
    }

    ComPtr<IAudioCaptureClient> captureClient;
    result = audioClient->GetService(IID_PPV_ARGS(&captureClient));
    if (FAILED(result)) {
        SetError(result, L"IAudioClient::GetService");
        finish();
        return;
    }

    HANDLE sampleEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (sampleEvent == nullptr) {
        SetError(HRESULT_FROM_WIN32(GetLastError()), L"CreateEvent(sample)");
        finish();
        return;
    }
    result = audioClient->SetEventHandle(sampleEvent);
    if (FAILED(result)) {
        CloseHandle(sampleEvent);
        SetError(result, L"IAudioClient::SetEventHandle");
        finish();
        return;
    }
    result = audioClient->Start();
    if (FAILED(result)) {
        CloseHandle(sampleEvent);
        SetError(result, L"IAudioClient::Start");
        finish();
        return;
    }

    running_.store(true, std::memory_order_release);
    HANDLE captureWaits[] = {stopEvent_, sampleEvent};
    bool stopping = false;
    while (!stopping) {
        const DWORD waitResult = WaitForMultipleObjects(2, captureWaits, FALSE, 100);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult == WAIT_TIMEOUT) {
            peak_.store(0.0F, std::memory_order_relaxed);
            continue;
        }
        if (waitResult != WAIT_OBJECT_0 + 1) {
            SetError(HRESULT_FROM_WIN32(GetLastError()), L"Capture wait");
            break;
        }

        float intervalPeak = 0.0F;
        UINT32 framesAvailable = 0;
        while (SUCCEEDED(captureClient->GetNextPacketSize(&framesAvailable)) && framesAvailable > 0) {
            BYTE* data = nullptr;
            DWORD flags = 0;
            UINT64 devicePosition = 0;
            UINT64 qpcPosition = 0;
            const HRESULT bufferResult = captureClient->GetBuffer(
                &data, &framesAvailable, &flags, &devicePosition, &qpcPosition);
            if (FAILED(bufferResult)) {
                SetError(bufferResult, L"IAudioCaptureClient::GetBuffer");
                stopping = true;
                break;
            }

            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data != nullptr) {
                const auto* samples = reinterpret_cast<const std::int16_t*>(data);
                intervalPeak = std::max(
                    intervalPeak,
                    CalculatePcm16Peak(samples, static_cast<std::size_t>(framesAvailable) * format.nChannels));
            }
            const HRESULT releaseResult = captureClient->ReleaseBuffer(framesAvailable);
            if (FAILED(releaseResult)) {
                SetError(releaseResult, L"IAudioCaptureClient::ReleaseBuffer");
                stopping = true;
                break;
            }
        }
        peak_.store(intervalPeak, std::memory_order_relaxed);
    }

    audioClient->Stop();
    CloseHandle(sampleEvent);
    finish();
}

} // namespace auto_ducking
