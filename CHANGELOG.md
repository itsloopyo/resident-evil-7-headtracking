# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Initial scaffold of the Resident Evil 7 head tracking mod, ported from the
Resident Evil Requiem mod and adapted for RE7's RE Engine build. Distributed
as rolling dev builds until the first tagged release.

### Added
- REFramework plugin that injects OpenTrack head rotation into the rendered
  view while leaving the game's clean camera rotation untouched, so aim,
  raycasts, AI vision, and projectiles are unaffected (decoupled look/aim).
- Camera controller hook on `app.PlayerCamera.lateUpdate` with a dynamic
  parent-chain discovery fallback.
- 6DOF position tracking, frame-rate-independent smoothing, sample-rate
  interpolation, and auto-recenter on entering gameplay.
- Game-state detection to suppress tracking in menus, pauses, loading, and
  cutscenes.
- Hotkeys: Home (recenter), End (toggle), Page Up (cycle tracking mode),
  Page Down (toggle world/local yaw); plus Ctrl+Shift+T/Y/G/H chord
  alternatives.
