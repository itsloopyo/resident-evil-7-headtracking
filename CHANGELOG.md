# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Initial scaffold of the Resident Evil 7 head tracking mod, ported from the
Resident Evil Requiem mod and adapted for RE7's RE Engine build. Distributed
as rolling dev builds until the first tagged release.

### Logging

- Capped the marker-compensation trace at five lines per session. It ran every 120 frames for the whole session, about 260 KB an hour at 60 fps into REFramework's log.
- The log now names the config file it actually read (`Config loaded from <path>`), so an edit made to the wrong `HeadTracking.ini` is visible in the log instead of costing a support round trip.
- A one-shot `First tracker pose received: yaw/pitch/roll (local|remote connection)` line the first time a tracker packet reaches the mod. It is emitted ahead of every enable/gameplay gate, so its absence means the packets never arrived rather than that tracking was off or the camera hook had not engaged.
- Troubleshooting now spells out the startup lines to look for in `re2_framework_log.txt`.

### Changed
- Recentring is gone entirely: the `Home` / `Ctrl+Shift+T` hotkey, the
  `RecenterKey` ini entry, and the mod's own centre. Your tracker owns the
  centre now. Set it there, with OpenTrack's Center bind, the CENTER button in
  a phone app, or your headset's own centring, and the mod applies what the
  tracker sends.
  Two centres in series was the problem: when the view was off you could not
  tell which side was wrong, and switching trackers meant centring in both.
- Smoothing is now two user-configurable parameters in a new `[Smoothing]` section of `HeadTracking.ini`: `LocalSmoothing` (default 0.0) for a tracker running on this machine, and `RemoteSmoothing` (default 0.15) for a tracker on a remote network device. The value is picked per connection from the packet source address and is re-evaluated while the game runs, so switching between a local OpenTrack instance and a phone on WiFi takes effect without a restart.
- Removed the `[Position] Smoothing` key. Both new parameters cover rotation and position, so there is no separate position smoothing setting.
- Removed the hidden 0.15 baseline smoothing floor that silently overrode the configured value. Local users now get zero-latency tracking by default.

### Added
- REFramework plugin that injects OpenTrack head rotation into the rendered
  view while leaving the game's clean camera rotation untouched, so aim,
  raycasts, AI vision, and projectiles are unaffected (decoupled look/aim).
- Camera controller hook on `app.PlayerCamera.lateUpdate` with a dynamic
  parent-chain discovery fallback.
- 6DOF position tracking, frame-rate-independent smoothing, and sample-rate
  interpolation.
- Game-state detection to suppress tracking in menus, pauses, loading, and
  cutscenes.
- Hotkeys: End (toggle), Page Up (cycle tracking mode), Page Down (toggle
  world/local yaw); plus Ctrl+Shift+Y/G/H chord alternatives.
