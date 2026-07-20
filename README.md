> [!CAUTION]
> ## Experimental prototype - expect missing core features
>
> This is **not** a finished mod.
>
> Current builds may only test whether head tracking can drive the camera. Bug fixes and core features like decoupled look/aim, independent reticle behavior, correct shot direction, off-screen reticle support, movement handling, and comfort tuning may be missing at this early stage of development.

# Resident Evil 7 Head Tracking

Decoupled head tracking for Resident Evil 7 biohazard: your head moves the view while the mouse or controller still controls aim, so you can look around the room with your gun pointed where you want it, no VR headset required.

<!-- ![Mod GIF](https://raw.githubusercontent.com/itsloopyo/resident-evil-7-headtracking/main/assets/readme-clip.gif) -->

## Features

- **Decoupled look and aim** - head tracking moves the camera while your mouse or controller still controls aim.
- **6DOF positional tracking** - lean and peek by moving your head in space.

## Requirements

- [Resident Evil 7 biohazard on Steam](https://store.steampowered.com/app/418370/).
- A head tracking source that speaks the OpenTrack UDP protocol: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or VR headset, or a phone tracking app.
- Windows 10 or 11 (64-bit).

## Installation

1. Download the latest `RE7HeadTracking-vX.Y.Z-installer.zip` from the [Releases page](https://github.com/itsloopyo/resident-evil-7-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`. It auto-detects your RE7 install, installs REFramework if missing, and deploys the plugin to `reframework/plugins/`.
4. Configure OpenTrack (or your tracking app) to send UDP output to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the install folder directly. Either set the `RE7_PATH` environment variable to your install root, or pass the path as the first argument:

```powershell
install.cmd "D:\Games\RESIDENT EVIL 7 biohazard"
```

### Manual Installation

If you prefer to place files by hand:

1. If you do not already have REFramework, extract `vendor/reframework/RE7.zip` into the game folder. This places `dinput8.dll` next to `re7.exe`.
2. Copy `RE7HeadTracking.dll` and `HeadTracking.ini` into `<RE7 install>/reframework/plugins/`.

The Nexus release ZIP contains only this `reframework/` subtree, ready to extract straight into the game folder if you manage REFramework yourself.

## Setting Up OpenTrack

In OpenTrack, set the output to **UDP over network**, host `127.0.0.1`, port `4242`. Map yaw, pitch, and roll (and optionally x, y, z for 6DOF). Leave sensitivities at 1:1 and let the in-game config handle scaling.

### VR Headset Setup

You can use a VR headset purely as a head tracker (no VR rendering in-game):

1. Connect your headset via Air Link or Virtual Desktop and start SteamVR.
2. In OpenTrack, choose the **SteamVR** input.
3. Set the OpenTrack output to UDP `127.0.0.1:4242` as above.

### Webcam Setup

1. In OpenTrack, choose the **neuralnet tracker** input.
2. Select your webcam and follow OpenTrack's on-screen centering.
3. Set the output to UDP `127.0.0.1:4242`.

### Phone App Setup

If your phone tracking app already smooths its output, point it directly at your PC's IP on port `4242`. If you want OpenTrack's curve mapping and filtering, send from the phone into OpenTrack first, then relay OpenTrack's output to `127.0.0.1:4242`.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action                 | Nav-cluster | Chord           |
|------------------------|-------------|-----------------|
| Recenter               | `Home`      | `Ctrl+Shift+T`  |
| Toggle tracking        | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode    | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode        | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

The config file is `reframework/plugins/HeadTracking.ini` in your game folder. Delete it to reset to defaults. If leaning moves the view the wrong way on an axis, flip the matching `Invert` flag.

```ini
[Network]
; UDP port for OpenTrack data (default: 4242)
UDPPort=4242

[Sensitivity]
; Rotation sensitivity multipliers (1.0 = 1:1)
YawMultiplier=1.0
PitchMultiplier=1.0
RollMultiplier=1.0

[Position]
; Position tracking sensitivity (0.1-10.0, higher = more movement)
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
; Position limits in meters (how far the camera can move)
LimitX=0.30
LimitY=0.20
LimitZ=0.40
; Backward lean limit (prevents camera clipping through player model)
LimitZBack=0.10
; Smoothing factor (0.0 = none, 0.99 = maximum)
Smoothing=0.15
; Invert position axes (flip if leaning moves the view the wrong way)
InvertX=false
InvertY=false
InvertZ=false
; Enable/disable position tracking (6DOF)
Enabled=true

[Hotkeys]
; Virtual key codes (hex). Chord alternatives: Ctrl+Shift+T/Y/G/H.
ToggleKey=0x23         ; End - Enable/disable
RecenterKey=0x24       ; Home - Recenter view
PositionToggleKey=0x21 ; Page Up - Cycle tracking mode
YawModeKey=0x22        ; Page Down - Toggle world/local yaw

[General]
; Auto-enable tracking on game start
AutoEnable=true
; Yaw mode: false = camera-local, true = horizon-locked (default)
WorldSpaceYaw=true
```

`WorldSpaceYaw=true` (the default) keeps yaw locked to the world horizon, so "up" stays constant no matter where the camera is pitched. Set it to `false` for camera-local yaw, which rotates around the camera's current up-axis. This is the same mode the `Page Down` / `Ctrl+Shift+H` hotkey toggles at runtime.

## Troubleshooting

**Mod not loading**
- Launch the game once after installing so REFramework initializes.
- Confirm `dinput8.dll` sits next to `re7.exe` and `RE7HeadTracking.dll` is in `reframework/plugins/`.
- Check `re2_framework_log.txt` in the game folder for `[RE7HT]` lines.

**No tracking response**
- Confirm your tracker is sending OpenTrack UDP to `127.0.0.1:4242`.
- Make sure tracking is enabled (press `End` or `Ctrl+Shift+Y` to toggle).
- Press `Home` (or `Ctrl+Shift+T`) to recenter if the view has drifted off-center.

**Jittery or unstable tracking**
- Raise the `Smoothing` value in `HeadTracking.ini`.
- For wireless or phone trackers, prefer routing through OpenTrack so its filtering can settle the signal.

**Yaw feels wrong at extreme up/down angles**
- Toggle between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked (the default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. REFramework is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires Visual Studio 2022 (C++ workload) and CMake.

```powershell
pixi run sync     # init cameraunlock-core submodule
pixi run build    # Debug build
pixi run install  # Release build + deploy to the detected RE7 install
pixi run package  # build Release ZIPs into release/
```

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - use your iPhone or Android phone as the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- Resident Evil 7 biohazard (c) CAPCOM.
- [REFramework](https://github.com/praydog/REFramework) by praydog.
- [OpenTrack](https://github.com/opentrack/opentrack).
- Built on the itsloopyo CameraUnlock shared core.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by CAPCOM. Use at your own risk.
