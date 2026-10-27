# SurfaceMix Visible Recipe Preview

## Scope

Read-only native ImageMap preview, using immutable measured recipes from the
Rust calibration catalogue. This does not implement an arbitrary-OBJ
SurfaceRecipeField, local thickness certification or executable paint volumes.
It does not change the OBJ, UVs, painted facets, vertex colours, zone configuration,
filament assignments, Rust draft, undo history, slicing or G-code.

## Controls

Open **Image Texture > Advanced Options**, then choose **SurfaceMix** in the
**Options** dropdown. It is its own entry alongside Image Options, Filament
Calibration and the other pages, not a control inside Image Options.

1. Select **SurfaceMix calibrated colours**. The slicer supplies the earlier
   measured CMYW recipes from its bundled resources; no recipe file is needed.
   **Replace recipe catalogue...** optionally loads another Rust catalogue.
2. If prompted, explicitly accept normalized sRGB as a display-only assumption.
   The saved catalogue does not establish this encoding or physical colour accuracy.
3. **SurfaceMix calibrated colours** maps ImageMap's existing colour targets to whole
   measured top recipes, displaying their measured RGB rather than generic CMYW mixes.
4. **SurfaceMix depth components** projects the selected recipe's component at
   the chosen inward depth onto the same surface. This is not a physical cutaway.
5. **ImageMap (existing colours)** restores the pre-existing ImageMap view.

The dropdown updates the visible preview immediately. **OK** retains its session
settings; **Cancel** restores the preview that was active before opening Advanced
Options, including a preview from another zone. The original texture file is
never overwritten, and none of these choices enables executable CMYW assignment.

Use a textured/coloured model already assigned to that ImageMap zone. Loading
recipes does not assign the zone to a model. One session preview zone is active
at a time. Raw offset/label atlases are deliberately not recoloured.

Known catalogue, preserved unchanged:

`/home/unknown/Desktop/Documents/3dprints/OUR-TRIPO-TO-PRINT-pipeline/surfacemix-delivery1-20261023-160637/reports/calibration-audit.json`

This is a `surfacemix.calibration-audit.v1` report, not a planar fixture report
or a product colour-assignment plan. A direct `surfacemix.calibration-catalog.v1`
catalogue is also supported. Loading does not update the catalogue or Rust state.

The earlier Tools > SurfaceMix Diagnostics report inspector is unchanged.

## Bundled Catalogue

`resources/surfacemix/creator5-cmyw-top-preview.v1.json` is a 2,086,876-byte
top-only projection of the saved Rust catalogue. All top-source data is preserved
verbatim, including raw records, sample IDs, mirrored collection entries,
measured values, sequence order, individual heights and unknown statuses. Side
offsets are not imported as depth stacks. No new recipes were invented.

The native importer checked all 1,120 records and 21,280 intervals, and an
independent JSON comparison confirmed exact equality to the top-only source
projection. SHA-256:
`b6725b0bd5e995ad4415b347c9b5944271e16a44cbc6964925878ccbffed6efa`.

The source audit remains unchanged. The bundle is resolved through the normal
application resources directory and does not depend on the developer archive
path. It is profile/filament-specific and has not acquired physical validation.
Default loading does not activate painting or bypass the display-assumption
confirmation. Missing/corrupt resources remain explicit errors with optional
manual replacement, not fabricated fallback colours.

## Data And Rendering

- The Rust catalogue remains authoritative. The C++ adapter consumes the decoded
  `surface_to_deep` records; it does not reinterpret the original profile JSON.
- All top records are retained, including catalogue mirrors and equal-colour
  records with different stack order. The supplied report contains 1,120 records
  representing 560 primary samples and their mirrored collection.
- Every selected recipe retains its sample identity, source pointer, source hash,
  measured RGB, exact component order, individual layer heights and total depth.
- Nearest-recipe selection uses exact CIELAB D65 squared distance (Delta E 76),
  with stable source-order tie-breaking and a tested k-d index. No four/sixteen
  palette reduction, generated hybrid, strength normalization, TD reordering,
  dithering or shortened stack is introduced.
- UV preview copies source pixels into the existing cancellable background queue.
  Nearest spatial sampling, original row/column orientation and alpha bytes are
  preserved; the normal preview resolution limit still applies (up to 2048 edge).
  Categorical texture filtering avoids invented in-between component labels.
- Existing painted-RGBA and vertex-colour preview routes consume the same recipe
  selector. Existing renderer geometry/interpolation remains a display limitation,
  not a per-path or whole-surface coverage certificate.
- Depth labels: C cyan, M magenta, Y yellow, W white. Grey means no interval at
  that depth; it never credits structural core or default material as a recipe.
- Session settings are scoped to model identity and zone stable ID/zone ID.
  Background jobs own immutable snapshots, with revisioned cache invalidation.
- The panel never enables ImageMap top printing, dithering or global colour
  simulation to obtain the overlay. Switching off restores those existing settings.
- A 32 MiB file limit and 10,000 top-record limit bound the preview import. Bad
  records reject the load with a source-pointer error; the prior preview survives.
- Hash strings preserve provenance only. This preview does not authenticate a
  catalogue, prove its relationship to a model or verify physical execution.

## Verification

- [x] Headless preview tests: 13 cases, 377 assertions passed.
- [x] Existing strict field importer regression tests passed.
- [x] Saved Rust audit imported: 1,120 records and 21,280 ordered intervals checked.
- [x] Exact self-colour lookup and all interval midpoint components checked.
- [x] Indexed nearest search compared against brute force on 256 deterministic targets.
- [x] New GUI panel, native preview renderer, ImageMap sidebar and scene integration passed C++17 syntax checks using the GUI build flags.
- [x] Source OBJ SHA-256 remains `f9931017dfbdc8c4adae28772e7c2865f9767212aa5406dcd91c5fbeeff931d4`.
- [x] Saved audit SHA-256: `4ada633464503f0ff1c1ab584356ee894a48383c80a53ac9e5b8b3c839957c1c`.
- [x] Prior GUI rebuild/link and `--help` startup check completed successfully.
- [x] Bundled top-only catalogue validated and independently reconciled to its Rust source.
- [x] Separate SurfaceMix page and automatic bundled-data correction rebuilt and startup-checked; both build and `--help` returned 0.
- [ ] Live ImageMap screenshot/interaction acceptance for textured and painted sources.
- [ ] Physical colour reproduction, wall eligibility and extrusion-path verification.

Executable target:

`/home/unknown/Documents/scripts/projectcampaign/ToniMcQueen-Slicer/build-surface-mix/src/orca-slicer-imagemap`

No material assignment, slicing, upload or print authorization is implied.

The corrected incremental build completed using two parallel jobs, recompiling
only Plater and the recipe panel before relinking. The executable is dated
2026-10-24 22:17:57 +0100 and is 164,505,952 bytes. Its SHA-256 is
`f9174ff273f3a4f70a08af225b674e906d10fade703b93ea38f65899a151eb56`.
The basic startup check reports `OrcaSlicer-ImageMap-1.0.22` and exits 0.
The build emitted warnings, including a TBB One Definition Rule warning; build
success is not a claim that all upstream warnings or runtime risks are resolved.
Save and close any older slicer window before opening this rebuilt executable.
Live GUI acceptance and all physical verification checkboxes above remain open.
