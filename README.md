# Auto Mixer

A lightweight Windows 11 audio-ducking utility. When a selected communication app produces audio, Auto Mixer smoothly lowers selected music apps and restores their original session volumes when the communication ends.

轻量级 Windows 11 自动音量闪避工具。当选定的通讯软件产生声音时，Auto Mixer 会平滑降低音乐软件的音量，并在通讯结束后恢复各个音频会话原有的音量。

[中文说明](#中文说明) · [English](#english)

> [!IMPORTANT]
> Auto Mixer currently stores settings in memory only. Normal shutdown restores controlled volumes, but forced termination, power loss, or an operating-system crash cannot run the restoration code. Persistent crash recovery has not yet been implemented.
>
> Auto Mixer 目前不会持久化设置。正常关闭程序时会恢复受控音量，但强制结束进程、断电或系统崩溃时无法执行恢复代码；持久化崩溃恢复功能尚未实现。

## 中文说明

### 功能特点

- 选择任意可用的 Windows 音频输出设备。
- 分别选择通讯软件和需要降低音量的音乐软件。
- 使用进程隔离的 WASAPI Loopback 计算实时音频峰值，避免不同软件的波形互相复制。
- 显示通讯软件与音乐软件的实时峰值波形。
- 支持滑块和数字输入，并保持两者同步。
- 可调整检测阈值、持续时间、释放延迟、Duck 比例、Attack 和 Release 时间。
- 为每个音乐音频会话保留独立的原始音量，维持应用之间原有的音量比例。
- 当用户在 Windows 音量合成器中手动调节音量时，重新计算恢复基准，避免立即抢回滑块。
- 取消勾选音乐软件、切换输出设备、发生运行错误或正常关闭程序时，尽力恢复受控音量。
- 只修改所选应用的会话音量，不修改输出设备的系统主音量。
- 不保存、录制或上传音频。

### 系统要求

- Windows 11 x64
- 直接运行已编译版本不需要安装额外运行库

进程 Loopback 功能要求 Windows 10 Build 20348 或更高版本，因此本项目当前以 Windows 11 为支持目标。

### 获取与运行

如果仓库的 **Releases** 页面提供了预编译版本：

1. 下载并解压最新的 x64 压缩包。
2. 运行 `auto-mixer-ui.exe`。

如果没有预编译版本，请按照[从源码构建](#从源码构建)进行操作。

### 快速上手

1. 启动需要使用的通讯和音乐软件，并让它们至少播放过一次声音，以便 Windows 创建音频会话。
2. 启动 `auto-mixer-ui.exe`。
3. 在顶部选择这些应用实际使用的输出设备。
4. 在“通讯软件”列表中勾选 Discord、Teams、Zoom 等声音来源。
5. 在“音乐软件”列表中勾选 Spotify、Chrome、VLC 等需要降低音量的应用。
6. 播放音乐，并让通讯软件产生声音。状态栏应显示检测状态与当前音乐倍率。
7. 根据实际音量和环境调整阈值与时间参数。

同一个可执行文件只能属于一个分类。Discord、Telegram、Teams、Zoom、Spotify、VLC、foobar2000 等常见程序会在发现时自动建议分类；浏览器不会自动分类，因为同一个浏览器可能同时用于通话和音乐。

### 参数说明

| 界面参数 | 默认值 | 作用 |
| --- | ---: | --- |
| 激活阈值 | `0.020` | 通讯峰值高于此值时，开始计算激活持续时间。 |
| 释放阈值 | `0.010` | 通讯峰值低于此值时，开始计算释放延迟；不能高于激活阈值。 |
| 激活持续时间 | `100 ms` | 声音持续超过激活阈值多久后开始 Ducking，可过滤很短的瞬态声音。 |
| 释放延迟 | `800 ms` | 声音低于释放阈值多久后判定通讯结束，避免语句停顿造成音量反复变化。 |
| 音乐 Duck 比例 | `0.250` | Duck 后音量与基准音量的比例。`0.25` 表示降低到原来的 25%，`1.0` 表示不降低。 |
| Attack 时间 | `200 ms` | 从原始音量平滑降低到目标音量所需的时间；`0` 表示立即变化。 |
| Release 时间 | `800 ms` | 从 Duck 音量平滑恢复到基准音量所需的时间；`0` 表示立即恢复。 |

推荐先使用默认值。如果 Discord 通知音也会触发 Ducking，可以适当提高激活阈值或延长激活持续时间。Auto Mixer 检测的是通讯软件的输出声音，并不能判断声音是否一定是人声。

### 从源码构建

需要安装：

- Visual Studio 2019 或更高版本的 **Desktop development with C++ / 使用 C++ 的桌面开发** 工作负载
- Windows 10/11 SDK
- CMake 3.15 或更高版本（也可使用 Visual Studio 附带的 CMake）

在普通 PowerShell 中运行项目自带脚本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

脚本会配置 Release x64 构建、编译所有程序并运行单元测试。生成的 UI 位于：

```text
build\Release\auto-mixer-ui.exe
```

也可以在 **Developer PowerShell for Visual Studio** 中手动构建：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### 诊断工具

生成一次所有活动输出设备和音频会话的快照：

```powershell
.\build\Release\auto-mixer-diagnostics.exe --once
```

持续监视音频会话：

```powershell
.\build\Release\auto-mixer-diagnostics.exe
```

检查指定进程 ID 的隔离峰值：

```powershell
.\build\Release\auto-mixer-process-meter.exe 1234 5678
```

诊断程序中的 `PEAK*` 是旧式会话控制诊断值，可能与整个输出设备的波形一致。桌面 UI 不使用该值，而是使用进程隔离的 Loopback 峰值。

### 隐私与安全

- Auto Mixer 只读取所选进程的音频输出缓冲区以计算绝对峰值。
- 音频缓冲区计算完成后会立即释放；程序不保存或解码音频内容。
- 程序不会访问麦克风，也不会把数据发送到网络。
- 音量写入通过 Windows 应用音频会话完成，不修改输出设备主音量。
- 正常关闭时会尽力恢复仍然存在的受控会话；应用提前退出时，对应会话可能已经无法恢复。

### 已知限制

- 应用分类和参数在重启后不会保留。
- 尚无托盘图标、后台服务或开机启动功能。
- 强制结束、断电或系统崩溃时不能保证恢复音量，目前没有持久化恢复日志。
- 分类基于可执行文件名。将 `chrome.exe` 选为音乐软件会影响所选输出设备上符合条件的 Chrome 音频会话，无法区分不同标签页的用途。
- 通讯软件自身的通知音、提示音或媒体播放也可能触发检测。
- 受保护的音频内容、设备重新配置或特殊应用路由可能导致进程 Loopback 或会话写入不可用。

更详细的内部设计请参阅 [docs/architecture.md](docs/architecture.md)，硬件测试步骤请参阅 [docs/manual-test-checklist.md](docs/manual-test-checklist.md)。

---

## English

### Features

- Select any active Windows audio output device.
- Choose communication sources and music targets independently.
- Use process-isolated WASAPI loopback metering so unrelated applications do not mirror one another's waveforms.
- Display live peak-envelope histories for communication and music applications.
- Edit settings with synchronized sliders and numeric inputs.
- Configure detection thresholds, dwell time, release delay, duck factor, attack time, and release time.
- Preserve a separate baseline volume for every selected music session, retaining the existing balance between applications.
- Cooperatively rebase the volume if the user adjusts a controlled session in Windows Volume Mixer.
- Attempt to restore controlled volumes when a music target is unchecked, the output device changes, an update error occurs, or the UI closes normally.
- Change application session volumes only; the output device's master volume is not modified.
- Never record, save, or upload audio.

### Requirements

- Windows 11 x64
- No additional runtime package is required for a prebuilt Release executable

Process loopback requires Windows 10 build 20348 or later, so Windows 11 is the project's current supported target.

### Download and run

If a prebuilt package is available on the repository's **Releases** page:

1. Download and extract the latest x64 archive.
2. Run `auto-mixer-ui.exe`.

Otherwise, follow [Build from source](#build-from-source).

### Quick start

1. Start the communication and music applications you intend to use. Let each application play audio at least once so Windows creates its audio session.
2. Launch `auto-mixer-ui.exe`.
3. Select the output device to which those applications are routed.
4. Under the communication list, check sources such as Discord, Teams, or Zoom.
5. Under the music list, check targets such as Spotify, Chrome, or VLC.
6. Play music and let the communication application produce audio. The status bar should show the detector state and current music multiplier.
7. Adjust the thresholds and timing parameters for your setup.

An executable can belong to only one category. Common applications including Discord, Telegram, Teams, Zoom, Spotify, VLC, and foobar2000 are suggested automatically when discovered. Browsers are intentionally not classified automatically because one browser can contain both calls and music.

### Settings

The current UI uses Chinese labels; this table provides their English meaning.

| UI label | English name | Default | Description |
| --- | --- | ---: | --- |
| 激活阈值 | Activation threshold | `0.020` | Starts the activation dwell timer while the communication peak remains above this value. |
| 释放阈值 | Release threshold | `0.010` | Starts the release-delay timer while the peak remains below this value. It cannot exceed the activation threshold. |
| 激活持续时间 | Activation dwell | `100 ms` | How long the signal must remain above the activation threshold before ducking begins. This filters brief transients. |
| 释放延迟 | Release delay | `800 ms` | How long the signal must remain below the release threshold before communication is considered finished. |
| 音乐 Duck 比例 | Duck factor | `0.250` | Ducked volume relative to the session baseline. `0.25` means 25% of the baseline; `1.0` means no attenuation. |
| Attack 时间 | Attack time | `200 ms` | Time used to ramp from the baseline to the ducked volume. `0` changes it immediately. |
| Release 时间 | Release time | `800 ms` | Time used to ramp back to the baseline. `0` restores it immediately. |

Start with the defaults. If Discord notifications trigger ducking, try increasing the activation threshold or activation dwell. Auto Mixer detects output from the communication process; it cannot determine whether that output is actually speech.

### Build from source

Install:

- Visual Studio 2019 or later with the **Desktop development with C++** workload
- Windows 10/11 SDK
- CMake 3.15 or later, or the CMake bundled with Visual Studio

Run the included script from a regular PowerShell window:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

The script configures an x64 Release build, compiles every target, and runs the unit tests. The resulting UI executable is:

```text
build\Release\auto-mixer-ui.exe
```

Alternatively, build manually from a **Developer PowerShell for Visual Studio**:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Diagnostic tools

Capture one snapshot of all active output devices and sessions:

```powershell
.\build\Release\auto-mixer-diagnostics.exe --once
```

Continuously monitor audio sessions:

```powershell
.\build\Release\auto-mixer-diagnostics.exe
```

Inspect isolated peaks for specific process IDs:

```powershell
.\build\Release\auto-mixer-process-meter.exe 1234 5678
```

The diagnostic CLI's `PEAK*` value is a legacy session-control diagnostic and can mirror the complete endpoint mix. The desktop UI does not use it; it uses process-isolated loopback peaks.

### Privacy and safety

- Auto Mixer reads output buffers for selected processes only to calculate an absolute peak.
- Each buffer is released immediately after measurement; audio content is not saved or decoded.
- The application does not access the microphone or transmit data over the network.
- Volume writes target Windows application audio sessions and do not change the output endpoint's master volume.
- On normal shutdown, the application attempts to restore every controlled session that still exists. If a target application exits first, its session may no longer be writable.

### Known limitations

- Application classifications and settings are not persisted between launches.
- There is no tray icon, background service, or start-with-Windows option yet.
- Volume restoration cannot be guaranteed after forced termination, power loss, or an OS crash; persistent recovery journaling is not implemented.
- Classification is based on executable names. Selecting `chrome.exe` as music affects eligible Chrome sessions on the selected endpoint and cannot distinguish the purpose of individual tabs.
- Notifications, sound effects, or media played by a communication application can also trigger detection.
- Protected audio, device reconfiguration, or unusual application routing may prevent process loopback capture or session-volume writes.

For implementation details, see [docs/architecture.md](docs/architecture.md). For hardware-backed acceptance tests, see [docs/manual-test-checklist.md](docs/manual-test-checklist.md).
