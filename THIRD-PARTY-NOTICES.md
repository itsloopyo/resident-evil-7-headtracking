# Third-Party Notices

This project uses the following third-party software.

## REFramework

- **Version:** nightly-01380 (commit `0436e043af6f81a5d3fef49ae27d35e63431e566`)
- **License:** MIT
- **Upstream:** https://github.com/praydog/REFramework
- **Usage:** Plugin host and SDK for RE Engine games. Provides method hooking, type-system access, and application-entry render callbacks.
- **Bundled:** yes. Shipped in the release ZIP at `vendor/reframework/RE7.zip` and used as the install-time source.

---

## OpenTrack

- **Version:** N/A (UDP protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** Head tracking data is received via the OpenTrack UDP protocol. No OpenTrack code is bundled.
- **Bundled:** no.

---

## MinHook

- **Version:** commit `05c06c5bbca226b72ffb40fc0caaef33bcaf6f74` (pinned via CMake FetchContent)
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Native function detouring, used by the cameraunlock-core hook manager. Compiled into the plugin DLL.
- **Bundled:** no.

---

## CameraUnlock Core Library

- **Version:** submodule commit `8ae3c98`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared C++ library providing the UDP receiver, tracking processing pipeline, smoothing, interpolation, hotkey input, and math utilities. Compiled into the plugin DLL.
- **Bundled:** no.

---
