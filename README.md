# Project Stack-chan

Voice-interactive M5Stack CoreS3 desk companion. See
[`docs/superpowers/specs/2026-06-02-stackchan-design.md`](docs/superpowers/specs/2026-06-02-stackchan-design.md)
for the full design and
[`docs/superpowers/plans/2026-06-02-stackchan-v1.md`](docs/superpowers/plans/2026-06-02-stackchan-v1.md)
for the v1 implementation plan.

## Build

```
pio run                 # firmware
pio run -t upload       # flash via USB
pio run -t uploadfs     # captive-portal UI to LittleFS
pio test -e native      # host-side unit tests
```
