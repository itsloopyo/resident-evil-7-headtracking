# Third-Party Notices

RE7HeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

Nothing in this repository is derived from, or redistributes any part of,
Resident Evil 7 biohazard. That is a statement of fact about the current tree,
not a permanent one: if a gameplay clip or screenshot is ever committed for the
README, it is the publisher's copyright and this file must be corrected to say
so before that commit lands.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| REFramework (loader binary) | `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5` | MIT | Bundled verbatim in the installer ZIP |
| REFramework (plugin SDK headers) | `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5` | MIT | Copied into `extern/reframework/`; compiled into `RE7HeadTracking.dll` |
| cameraunlock-core | `f441e29427b7422a584ba492dddd7788881804b0` | MIT | Compiled into `RE7HeadTracking.dll` |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |

---

## REFramework

Copyright (c) 2019 praydog. Upstream: https://github.com/praydog/REFramework

This mod uses REFramework in two distinct ways, and both are redistribution, so
both are recorded here.

**1. The loader binary, bundled.** Vendored at `vendor/reframework/`, shipped
inside the installer ZIP and used as the install-time source. It is the upstream
release asset byte for byte; we do not repack, patch or strip it. The upstream
licence file ships beside it at `vendor/reframework/LICENSE`.

- Release: https://github.com/praydog/REFramework-nightly, tag
  `nightly-01394-ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`
- Upstream asset: `REFramework.zip`, stored here as `vendor/reframework/RE7.zip`
- REFramework source revision: `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`,
  as recorded by `reframework_revision.txt` inside the archive itself
- SHA-256 of the archive:
  `a3d24f04e41933a7a3a6e1d6402b7de18ca677245d9ca0dda9f6a5ca20e9b94e`, verified
  against the upstream download

**2. The plugin SDK headers, compiled in.** `extern/reframework/API.h` and
`extern/reframework/API.hpp` are praydog's plugin API headers, copied into this
repository verbatim and compiled into `RE7HeadTracking.dll`. They are the only
third-party source files this repository carries. Both are byte-identical to
upstream at revision `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`; we have made no
modification to either. The upstream licence ships beside them at
`extern/reframework/LICENSE`, and `extern/reframework/README.md` records where
they came from.

Note on the revision: REFramework's nightly builds are published from a separate
repository (`praydog/REFramework-nightly`) whose own commit hashes are unrelated
to REFramework's source history. The authoritative revision for both items above
is the one in the release tag and in the archive's own `reframework_revision.txt`,
quoted above.

```
MIT License

Copyright (c) 2019 praydog

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## cameraunlock-core

Git submodule at `cameraunlock-core/`, compiled into `RE7HeadTracking.dll`. Our own code,
MIT licensed, reproduced here so the notices are complete.

- Pinned commit: `f441e29427b7422a584ba492dddd7788881804b0`

```
MIT License

Copyright (c) 2026 CameraUnlock

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

Not bundled and not linked. This mod implements the OpenTrack UDP pose datagram
layout so that OpenTrack (https://github.com/opentrack/opentrack, ISC licence)
and compatible trackers can drive it. No OpenTrack code, headers or binaries
are copied, linked or redistributed, so its licence triggers no notice
obligation here. It is credited because the wire format is its work.

---

## Resident Evil 7 biohazard

Resident Evil 7 biohazard is (c) CAPCOM. That name, and the names, logos,
characters and marks associated with it, are the property of CAPCOM and are used
here only to identify the game this mod applies to. That is nominative use and
not a claim of any right in them.

This project is an unofficial, fan-made modification. It is not affiliated with,
endorsed by, or sponsored by CAPCOM, by the RE Engine's authors, or by any other
rights holder, and it requires a legitimately purchased copy of the game.

It redistributes no game code, no game assets, no game data files and no
proprietary DLLs. It ships no decompiled or disassembled game code, and no byte
signatures taken from game code. What it does reference are managed type and
method names (`app.PlayerCamera`, `via.SceneManager` and similar), which it
looks up at runtime through REFramework's public type registry. Those are
interoperability facts, in the same category as an API name, not copied
expression.

If you hold rights in anything named here and want something changed or removed,
open an issue on the repository and it will be dealt with.
