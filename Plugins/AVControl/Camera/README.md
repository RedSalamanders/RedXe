# RedXe Camera source and setup

This source is part of AV Control. It is a separate static-CRT COM DLL because Windows Frame Server loads it in
Local Service, Session 0. It has no DxUi or RedXe UI dependency. AV Control itself consumes the single DxUi.lib.

The implementation is in validation. A compiled package is not proof of installed Frame Server interoperability,
hardware compatibility or release acceptance. Automated suites use explicit synthetic devices and isolated names.

## Build and review

From the RedXe repository root, use native PowerShell 7:

```powershell
.\build.ps1 -Configuration Release -Platform x64 # ARM64 on an ARM64 machine
.\package-camera.ps1 -Platform x64
.\install-camera.ps1 -Action Install -PackagePath '<returned package directory>' -WhatIf
```

Packaging copies exactly `AVControlCamera.dll`, `AVControlCameraSetup.exe`, and a SHA-256 manifest into a new immutable
`.build/packages/RedXeCamera-<architecture>-<id>` directory. Validation rejects Debug binaries, wrong PE architecture,
changed hashes, wrong version/product identity, extra entries and reparse paths. The manifest is integrity evidence
for a reviewed local build; it is not a publisher signature or a substitute for a trusted release distribution.

## Install and use

1. Close applications currently using RedXe Camera. In an elevated **native PowerShell 7** terminal, execute the
   reviewed `install-camera.ps1 -Action Install -PackagePath '<package directory>'` command. The installer copies the
   source into `%ProgramFiles%\RedSalamanders\RedXe Camera` and registers only its fixed machine COM source key in
   the native 64-bit view. It never stops Frame Server, RedXe, or capture applications.
2. Back in a **non-elevated terminal for the intended user**, execute `setup-camera.ps1 -Action Register`. This calls
   the official Windows 11 API with CurrentUser access and System lifetime. An elevated credential switch could
   choose a different account, so per-user registration/removal rejects elevated execution. `Check` is read-only.
3. Restart RedXe after installation. Select the physical camera in an AV profile. In each capture app, select
   **RedXe Camera** once (Windows may append “Windows Virtual Camera” to the name).
4. Camera On arms the route. Capture starts only while an app requests frames. Off releases physical capture and
   sends neutral NV12 frames through the virtual route. Selecting a physical webcam directly in an application
   bypasses RedXe Camera; the plugin cannot mute that application's independent physical-camera access.

Windows 10 can load the AV audio controls; the virtual-camera registration API requires Windows 11. Privacy controls
and ordinary camera permission prompts remain in force. `Denied`, `Busy`, and `Error` offer an explicit retry after
the underlying issue is resolved. A removed/replaced device never automatically resumes camera capture.

The setup wrapper contains a single owned helper with a 15-second setup timeout. Normal AV commands and capture
driver watchdogs have their separate shorter control budgets. A setup timeout requires a read-only Check before a
retry; an operation may already have reached Windows registration before its process was terminated.

## Remove or update

Run `setup-camera.ps1 -Action Remove` without elevation for each user who registered a route. Then close consumers
and run `install-camera.ps1 -Action Remove` in an elevated native terminal to remove the shared source. Machine
removal affects every user's route, so it does not impersonate other users or silently delete their camera devices.
Normal RedXe exit, tile hiding, and source DLL unload never remove the persistent device.

Updates validate the old ownership manifest and COM registration, stage the new package under the protected parent,
then replace the fixed directory and source key. Failures attempt to restore the old registration/package. Locked
old binaries can leave an explicitly reported backup for later cleanup. No recursive delete or process-name kill is
used. A mismatched/corrupt old package requires explicit repair; the installer will not overwrite unrelated contents.

## Acceptance still required

The active [AV plan](../../../Specs/Plans/WIP/RFC_Plugins_AVControl.md) owns G2: installed x64/ARM64 Frame Server loading,
privacy and principal/session boundaries, real capture applications, device/driver faults, off/switch latency,
multi-consumer behavior and measured active-camera resource budgets. Do not replace these with synthetic test counts.
