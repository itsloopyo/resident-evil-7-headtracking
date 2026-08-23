# reframework plugin SDK (vendored)

`API.h` and `API.hpp` are the REFramework plugin API headers by praydog, copied
into this repository so the plugin can be built without a REFramework checkout.
They are compiled into `RE7HeadTracking.dll`.

## Snapshot

- Upstream: https://github.com/praydog/REFramework
- Path upstream: `include/reframework/API.h`, `include/reframework/API.hpp`
- Revision: `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`
- Licence: MIT, reproduced verbatim in `LICENSE` beside these headers

## Modifications

None. Both headers are byte-identical to upstream at the revision above.

Keep it that way. If a change is ever genuinely needed, record it here and in
`THIRD-PARTY-NOTICES.md`, because an unmarked local edit turns this file into a
false provenance claim.

## Refreshing

These headers are not touched by `pixi run update-deps`, which refreshes only the
loader binary under `vendor/reframework/`. Bump them by hand, matching the
revision of the vendored loader, then verify:

```bash
rev=ec6c81fd39831b328027ae00e102bc9c9c3f8aa5
for f in API.h API.hpp; do
  curl -sSL "https://raw.githubusercontent.com/praydog/REFramework/$rev/include/reframework/$f" \
    | diff - "extern/reframework/$f" && echo "$f matches upstream"
done
```
