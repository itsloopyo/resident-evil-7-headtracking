# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Initial scaffold of the Resident Evil 7 head tracking mod, ported from the
Resident Evil Requiem mod and adapted for RE7's RE Engine build. Distributed
as rolling dev builds (`0.1.0-nightly.<date>.<sha>`, and `0.0.0-nightly.<date>.<sha>` before
settings moved to `CameraUnlock.ini`) until the first tagged release.

### Logging

- Capped the marker-compensation trace at five lines per session. It ran every 120 frames for the whole session, about 260 KB an hour at 60 fps into REFramework's log.
- The log now names the config file it actually read (`Config Canonical: <path>`, or `Created` or `Migrated` on a first start), so an edit made to the wrong file is visible in the log instead of costing a support round trip.
- A one-shot `First tracker pose received: yaw/pitch/roll (local|remote connection)` line the first time a tracker packet reaches the mod. It is emitted ahead of every enable/gameplay gate, so its absence means the packets never arrived rather than that tracking was off or the camera hook had not engaged.
- Troubleshooting now spells out the startup lines to look for in `re2_framework_log.txt`.

### Changed
- Settings move to `reframework\plugins\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default of the earlier version that wrote `HeadTracking.ini`, as far as the file shows which version that was, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- A number in `HeadTracking.ini` that is not a number the mod can use (`nan`, `inf`) is written as `default` where the defaults the README shows set that setting to `default`, and as the built-in value elsewhere.
- Comments, and keys the mod never read, are not carried over. Nor is a sensitivity or axis inversion you changed from its default, where your old file had one. Set these in your tracker instead.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Turning positional tracking off or on with `Page Up` / `Ctrl+Shift+G`, and switching the yaw mode with `Page Down` / `Ctrl+Shift+H`, is saved to `CameraUnlock.ini` straight away, so the next start keeps your choice. Turning head tracking on or off with `End` still lasts for the session only.
- The keys are renamed to the names every head tracking mod on this format uses: `[Network] UDPPort` is `UdpPort`, `[General] AutoEnable` is `EnableOnStartup`, `[Position] Enabled` is `PositionEnabled`, `[Position] LimitX`, `LimitY`, `LimitZ` and `LimitZBack` are `PositionLimitX` and so on, and `[Hotkeys] PositionToggleKey` is `CycleTrackingModeKey`. The import carries each value across.
- Since the dev build (0.0.0-nightly.20260820.8322e7a), and not caused by the move to `CameraUnlock.ini`, reading the settings changed in one commit, 4e1cfb5, which moved the plugin onto cameraunlock-core's shared reader:
  - A number followed by anything but an inline comment (`0,15` or `0.5abc`), or written in hex (`0x1`), keeps the setting's default. The dev build read the number at the front of the text, so `LocalSmoothing=0,15` gave 0.
  - A position limit below 0.01 is kept as written, down to 0. The dev build raised it to 0.01.
  - `LimitY` sets how far the view moves down as well as up. The dev build held downward travel at 0.20 m whatever `LimitY` said.
  - A hotkey code the mod no longer accepts as a hotkey (0, a negative code, one above `0xFE`, or Shift, Ctrl or Alt, which the chords are made of) keeps that hotkey's default key. The dev build registered the code as written.
  - A sensitivity outside the dev build's range is read as written up to 5 (rotation) or 10 (position), where the dev build clamped it. Either way it is not carried over, as above.
- `Page Up` / `Ctrl+Shift+G` turns positional tracking off and on again instead
  of cycling three modes. The third mode disabled head rotation, and it sat
  directly after the mode a `[Position] Enabled=false` config starts in, so one
  press of a key labelled "toggle position" switched head rotation off.
- Close-range interaction prompts (`InteractPointGuide`) now follow their world
  target when you lean, not only when you turn your head. The frame is drawn
  from the leaned eye while the game projects the prompt's anchor from the
  un-leaned one, and the gap is lean divided by distance: a 0.30 m lateral lean
  put a 3 m prompt about 70 px off its target on a 1920x1080 canvas. The
  correction assumes a 3 m anchor, so it is exact at that range and still an
  improvement at anything closer than 6 m. Objective and far-icon markers are
  unchanged.
- The game window is centred on the first rendered frame again. That call was
  dropped when the plugin moved onto the shared driver.
- Recentring is gone entirely: the `Home` / `Ctrl+Shift+T` hotkey, the
  `RecenterKey` ini entry, and the mod's own centre. Your tracker owns the
  centre now. Set it there, with OpenTrack's Center bind, the CENTER button in
  a phone app, or your headset's own centring, and the mod applies what the
  tracker sends.
  Two centres in series was the problem: when the view was off you could not
  tell which side was wrong, and switching trackers meant centring in both.
- Smoothing is now two user-configurable parameters in a new `[Smoothing]` section of `CameraUnlock.ini`: `LocalSmoothing` (default 0.0) for a tracker running on this machine, and `RemoteSmoothing` (default 0.15) for a tracker on a remote network device. The value is picked per connection from the packet source address and is re-evaluated while the game runs, so switching between a local OpenTrack instance and a phone on WiFi takes effect without a restart.
- Removed the `[Position] Smoothing` key. Both new parameters cover rotation and position, so there is no separate position smoothing setting.
- Removed the hidden 0.15 baseline smoothing floor that silently overrode the configured value. Local users now get zero-latency tracking by default.

### Added
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- REFramework plugin that injects OpenTrack head rotation into the rendered
  view while leaving the game's clean camera rotation untouched, so aim,
  raycasts, AI vision, and projectiles are unaffected (decoupled look/aim).
- Camera controller hook on `app.PlayerCamera.lateUpdate` with a dynamic
  parent-chain discovery fallback.
- 6DOF position tracking, frame-rate-independent smoothing, and sample-rate
  interpolation.
- Game-state detection to suppress tracking in menus, pauses, loading, and
  cutscenes.
- Hotkeys: End (toggle), Page Up (toggle positional tracking), Page Down (toggle
  world/local yaw); plus Ctrl+Shift+Y/G/H chord alternatives.

### Removed
- The sensitivity and axis inversion settings (`[Sensitivity]` and `[Position] SensitivityX/Y/Z` and `InvertX/Y/Z`). Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.
