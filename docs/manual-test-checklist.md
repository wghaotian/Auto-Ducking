# Manual test checklist

## Desktop UI acceptance tests

- [ ] The output-device dropdown lists all active render endpoints and marks default roles.
- [ ] Changing devices replaces the application lists and clears old waveform history.
- [ ] Checking an application under communication produces a blue peak history when it outputs audio.
- [ ] Checking an application under music produces a green peak history when it outputs audio.
- [ ] With Chrome playing and Discord silent, Chrome has a waveform while Discord stays at zero apart from genuine Discord sounds.
- [ ] Checking the same application in the other category automatically clears its previous category.
- [ ] Each slider updates its numeric input.
- [ ] Typing a valid number updates its slider; out-of-range values are clamped.
- [ ] Release threshold cannot exceed activation threshold.
- [ ] Sustained communication audio changes the detector status to active; short/quiet activity respects the configured dwell and release delay.
- [ ] While communication is active, each selected music session reaches its own baseline multiplied by the duck factor after the configured attack time.
- [ ] When communication ends, selected music sessions return to their respective baselines after the configured release time.
- [ ] Multiple selected music applications retain their relative volumes while ducked.
- [ ] Unchecking a music target or changing the output device restores its controlled volume immediately.
- [ ] Changing a ducked session in the Windows volume mixer is respected after the brief external-change grace interval and release returns to the rebased level.
- [ ] Starting or closing an audio application updates the lists within about one second.
- [ ] Resizing the window down to its minimum leaves every control usable.
- [ ] Closing the UI normally while ducked restores every still-existing controlled session to its baseline.

## Diagnostic CLI acceptance tests

For each scenario, start `auto-ducking-diagnostics.exe`, confirm the expected executable and PID are shown, then play/silence audio and verify that only the relevant session's `PEAK` and bar respond.

- [ ] Discord + Spotify: both sessions appear; Discord speech and Spotify music have independent meters.
- [ ] Discord + YouTube in Chrome: Discord and one or more Chrome sessions appear independently.
- [ ] Discord + Spotify + YouTube: all concurrent output sessions remain visible.
- [ ] Discord + a second voice application: both voice-output meters respond.
- [ ] Voice application open but silent: its session may remain present/inactive and peak stays near zero.
- [ ] Very quiet incoming voice: peak is non-zero and can be recorded for later threshold tuning.
- [ ] Discord notification: record peak and duration; the diagnostic intentionally does not classify it as speech.
- [ ] Change Spotify's mixer volume: displayed `VOL` follows the external change; the utility never writes it.
- [ ] Close Spotify during playback: its session disappears after it expires/topology refreshes.
- [ ] Close Discord while audio is active: the monitor remains stable and the session expires/disappears.
- [ ] Change the default output: role labels move to the new endpoint within the topology interval.
- [ ] Disconnect/reconnect Bluetooth audio: endpoint list recovers without restarting the monitor.
- [ ] Force-close the diagnostic monitor: mixer volumes are unchanged because the CLI is read-only.
- [ ] Route an application to a non-default endpoint: it is visible in the default all-device mode.
- [ ] Start an audio application after the monitor: its session appears within the topology interval.
- [ ] Browser with several audible tabs/processes: all exposed sessions are listed; none are collapsed by PID.

## Evidence to capture

When a check fails, run the following while the relevant applications are producing audio and attach the output:

```powershell
.\build\Release\auto-ducking-diagnostics.exe --once
```

Also note the Windows output device, whether per-application audio routing is enabled, and whether the application is installed from the Microsoft Store. Protected/system sessions may not expose a usable process path; the PID, session ID, and meter can still be available.

## Known limitation

Normal shutdown restoration is implemented, but forcible process termination, power loss, or an OS crash cannot run cleanup. Persistent crash-recovery journaling is not yet implemented.
