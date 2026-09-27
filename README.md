# Resident Evil 7 Head Tracking

![Resident Evil 7 biohazard running with this mod](https://raw.githubusercontent.com/itsloopyo/resident-evil-7-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Resident Evil 7 biohazard that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera while your mouse or controller still controls aim.
- **6DOF positional tracking** - lean and peek by moving your head in space.
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Resident Evil 7 biohazard on Steam](https://store.steampowered.com/app/418370/).
- A head tracking source that speaks the OpenTrack UDP protocol: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or VR headset, or a phone tracking app.
- Windows 10 or 11 (64-bit).

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Resident Evil 7 biohazard**, and click
**Play with head tracking**.

### Standalone Installer

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
2. Copy `RE7HeadTracking.dll` into `<RE7 install>/reframework/plugins/`. The mod creates `CameraUnlock.ini` beside it on first launch.

The Nexus release ZIP contains only this `reframework/` subtree, ready to extract straight into the game folder if you manage REFramework yourself.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has. These are the defaults: each
action's keys are a list under `[Hotkeys]` in `CameraUnlock.ini`, chords included, and any of them
can be changed or removed.

| Action                     | Nav-cluster | Chord          |
|----------------------------|-------------|----------------|
| Toggle tracking            | `End`       | `Ctrl+Shift+Y` |
| Toggle positional tracking | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode            | `Page Down` | `Ctrl+Shift+H` |

`Page Up` / `Ctrl+Shift+G` turns positional (6DOF) tracking off and on. Head rotation keeps running either way.

The positional tracking choice and the yaw mode are saved to `CameraUnlock.ini` the moment you
change them, so the next launch starts with the same choice. Toggling tracking on or off with `End`
lasts for the session only: each launch starts with tracking on or off as `EnableOnStartup` says.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `reframework\plugins\CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Resident Evil 7 biohazard head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup. The mode hotkey turns it on and off and saves it here.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising or lowering your head can move the view.
PositionLimitY=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, or rotation only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default
```
<!-- /cameraunlock:config -->

The mod has no sensitivity, deadzone or axis inversion settings: set those in your tracker. It
moves the view as far as the position your tracker reports, up to the position limits above.

## Troubleshooting

**Sending a log:**
- REFramework writes one log per game launch at `<game>/re2_framework_log.txt`. That generic name is used for every RE Engine title, so it is the right file for this game too. If the game folder is not writable it lands in `%APPDATA%\REFramework\<exe name>\` instead.
- The file is truncated on every launch, so it only ever holds the current session. Attach it as-is to a bug report.
- This mod's lines are prefixed `[RE7HT]`. The startup sequence to look for is: `Plugin loaded`, `Config Canonical: ...` (`Created` or `Migrated` on a first start), `UDP receiver started on port ...`, `Initialization complete`, then `First tracker pose received: ...` once the tracker sends anything.

**Mod not loading**
- Launch the game once after installing so REFramework initializes.
- Confirm `dinput8.dll` sits next to `re7.exe` and `RE7HeadTracking.dll` is in `reframework/plugins/`.
- Check `re2_framework_log.txt` in the game folder for `[RE7HT]` lines.

**No tracking response**
- Confirm your tracker is sending OpenTrack UDP to `127.0.0.1:4242`.
- Make sure tracking is enabled (press `End` or `Ctrl+Shift+Y` to toggle).
- If the view sits off-centre, centre it in your tracker app: OpenTrack's Center bind, the CENTER button in a phone app, or your headset's own centring. The mod applies what the tracker sends and keeps no centre of its own.

**Jittery or unstable tracking**
- Raise `RemoteSmoothing` (phone or other network tracker) or `LocalSmoothing` (tracker on this PC) in the `[Smoothing]` section of `CameraUnlock.ini`.
- For wireless or phone trackers, prefer routing through OpenTrack so its filtering can settle the signal.

**Yaw feels wrong at extreme up/down angles**
- Toggle between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked (the default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs and leaves `CameraUnlock.ini` and `HeadTracking.ini` in place, so a reinstall keeps your settings. REFramework is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

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
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- Resident Evil 7 biohazard (c) CAPCOM.
- [REFramework](https://github.com/praydog/REFramework) by praydog.
- [OpenTrack](https://github.com/opentrack/opentrack).
- Built on the itsloopyo CameraUnlock shared core.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by CAPCOM. Use at your own risk.
