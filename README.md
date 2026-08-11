# Auto Mixer

This repository contains a lightweight Windows 11 automatic audio-ducking tool. The native desktop UI discovers active render endpoints and sessions, measures selected communication applications with process-isolated WASAPI loopback, and smoothly attenuates selected music sessions while communication audio is active. Captured audio is never saved.

## Why native Core Audio

The prototype is C++17 and calls the Windows Core Audio COM APIs directly. This keeps the executable small, adds no runtime package dependency, and validates the same APIs that the ducking service will later use:

- `IMMDeviceEnumerator` discovers active render endpoints and the current console, multimedia, and communications defaults.
- `IAudioSessionManager2` and `IAudioSessionEnumerator` enumerate application audio sessions on each endpoint.
- `IAudioSessionControl2` supplies the process ID, session identifier, and session-instance identifier.
- `ISimpleAudioVolume` reads and writes each selected music session's volume without changing the endpoint master volume.
- `IAudioMeterInformation::GetPeakValue` is retained in the diagnostic CLI, but Microsoft documents it as an endpoint stream meter. On the tested Windows 11 setup, querying it through session controls produced endpoint-correlated values and is not used for UI detection.
- `ActivateAudioInterfaceAsync` with `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` creates a process-filtered stream for each selected PID. `IAudioCaptureClient` buffers are scanned for their absolute PCM peak and immediately released; no audio is decoded, written, or retained.

Release builds statically link the MSVC runtime, so deployment is a single executable on Windows 11.

## Desktop UI

`auto-mixer-ui.exe` provides:

- selection of any active Windows output endpoint;
- separate checkable application lists for communication sources and music targets;
- mutually exclusive VOICE/MUSIC classification by executable name;
- process-isolated real-time peak-envelope history for the selected communication and music applications;
- synchronized sliders and numeric inputs for activation/release thresholds, activation time, release delay, duck factor, attack, and release;
- live hysteresis/delay activity detection using the selected communication sessions;
- per-session volume ducking with configurable factor, attack, and release times;
- best-effort restoration of controlled session volumes when targets/devices change, the UI closes, or the update loop fails.

Common applications such as Discord, Telegram, Teams, Zoom, Spotify, VLC, and foobar2000 are suggested automatically when discovered. Browsers are intentionally left unclassified because the same browser may contain both calls and music. The user can check either list manually.

The waveforms are histories of computed process peaks, not stored audio samples. Settings and application classifications currently remain in memory for the lifetime of the UI. Process loopback requires Windows 10 build 20348 or later; Windows 11 satisfies this requirement.

The default mode inspects every active output endpoint, because Windows can route individual applications to non-default devices. A topology refresh every second rediscovers applications, sessions, and devices; peak and volume values are sampled every 100 ms. Both intervals are configurable.

See [docs/architecture.md](docs/architecture.md) for implementation details and trade-offs.

## Build

Prerequisites:

- Windows 10/11 SDK
- Visual Studio 2019 or newer C++ Build Tools
- CMake 3.15 or newer

From a **Developer PowerShell for Visual Studio**:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On the current development machine, CMake is bundled with Visual Studio and can also be invoked through `scripts/build.ps1` from an ordinary PowerShell window:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

## Run

Launch the desktop UI:

```powershell
.\build\Release\auto-mixer-ui.exe
```

Choose an output device, then check applications in the communication or music list. A process can belong to only one category at a time. Adjust a setting with either its slider or numeric box; the paired control updates automatically. The red and amber lines in the communication waveform represent activation and release thresholds.

When communication becomes active, every selected music session ramps from its own current baseline to `baseline × duck factor`. When communication ends, it ramps back to that baseline. If the user changes a controlled session in the Windows volume mixer, Auto Mixer rebases to the observed value and briefly pauses its writes instead of immediately fighting the manual change.

### Diagnostic CLI

Live view of all active render endpoints (10 peak samples per second, topology refresh once per second):

```powershell
.\build\Release\auto-mixer-diagnostics.exe
```

One machine-readable-ish snapshot suitable for copying into an issue:

```powershell
.\build\Release\auto-mixer-diagnostics.exe --once
```

The CLI's `PEAK*` column is a legacy session-control diagnostic and may mirror the endpoint mix. To verify isolated process metering for one or more PIDs:

```powershell
.\build\Release\auto-mixer-process-meter.exe 1234 5678
```

Only the default multimedia endpoint, sampled at 20 Hz:

```powershell
.\build\Release\auto-mixer-diagnostics.exe --default-device-only --interval-ms 50
```

Run with `--help` for all options. Press Ctrl+C to stop the live view.

## Current scope

Implemented:

- native Windows desktop UI without an additional GUI framework;
- output-device selection and dynamic session-list refresh;
- communication/music application classification and process-isolated aggregate waveforms;
- synchronized slider/numeric parameter editing with threshold validation;
- reusable, unit-tested activation hysteresis and delay state machine;
- reusable, unit-tested ducking controller with smooth attack/release ramps;
- per-session baseline preservation, cooperative external-change rebasing, and normal-shutdown restoration;
- all active Windows render endpoints;
- default-role identification;
- multiple sessions per process;
- stable endpoint + session-instance keys;
- process ID, executable name (with a process-snapshot fallback), executable path when accessible, display name, state, volume, mute, and peak;
- periodic device/session rediscovery;
- Ctrl+C handling for the read-only diagnostic CLI and failure-safe UI cleanup;
- small unit tests for output helpers.

Not yet implemented by design:

- persisted VOICE/DUCK/IGNORE classification and JSON configuration;
- crash/power-loss recovery journaling (normal UI shutdown is restored in memory);
- tray UI.

Hardware-backed behavior must be verified on the target Windows 11 audio setup. Follow [docs/manual-test-checklist.md](docs/manual-test-checklist.md) and retain a `--once` snapshot when reporting a mismatch.
