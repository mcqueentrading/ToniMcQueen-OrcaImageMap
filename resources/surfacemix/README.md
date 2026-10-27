# Bundled SurfaceMix Preview Measurements

`creator5-cmyw-top-preview.v1.json` is an exact top-source projection of the
passing Rust Delivery 1 calibration catalogue. It is not a synthetic palette or
an executable print plan. The 560 primary measured samples and their mirrored
records are preserved, giving 1,120 records and 21,280 ordered intervals.
Recipes retain their identities, measured RGB, full layer sequences/heights,
raw records, provenance and unknown execution/review statuses.

Provenance:

- Original audit SHA-256: `4ada633464503f0ff1c1ab584356ee894a48383c80a53ac9e5b8b3c839957c1c`.
- Original full catalogue content hash: `69c923f88abeb28ef45d6a30bdfbc2beb11fc65ed5a6ddf3d4248d7921db5475`.
- Original top-profile SHA-256: `5c601d66893a6c09221459d879c8317b8c176ea90b4318ba159897ceaa600679`.
- Bundled file SHA-256: `b6725b0bd5e995ad4415b347c9b5944271e16a44cbc6964925878ccbffed6efa`.
- Reproducible exporter: `scripts/export-surface-mix-preview-catalog.sh`.

The preview loads this file using the application's resources directory, not a
hardcoded developer-home path. Selecting a SurfaceMix mode needs no file picker.
Replacing the catalogue remains optional.

RGB encoding/range remains unspecified in the source, so using normalized sRGB
still requires an explicit display-only confirmation. The measurements are
filament/profile-specific; they are not universal CMYW recipes. Backing, tool
mapping, physical colour reproduction and execution compatibility are not
established by this bundle. Assignment, dithering and printing are not enabled.
