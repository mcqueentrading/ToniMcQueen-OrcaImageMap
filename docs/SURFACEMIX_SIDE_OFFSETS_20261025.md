# SurfaceMix Measured Side Offsets

## Scope

Experimental connection of the existing Rust side catalogue to ImageMap's
wall-offset executor and preview. This is not a radial 1.26 mm depth painter,
new optical model, arbitrary curved-volume certificate or physical print approval.
The working measured top-stack path remains separate.

## Data And Ownership

The immutable bundle is `resources/surfacemix/creator5-cmyw-side-offsets.v1.json`.
It is exported from the passing Delivery 1 Rust calibration audit without editing
the Rust project, the source profile, the product OBJ or the top catalogue.
The export retains all 1,680 side records, including catalogue mirrors; it does
not claim 1,680 independent physical swatches. Original RGB precision, raw
offsets, independent amplitudes, source pointers, sample IDs, unknown statuses
and raw profile content remain in the transport.

The independent amplitudes are not normalized to sum to one and are not inward
depths. For example, `[0.8627, 0.8471, 1.0, 0.6667]` stays as supplied. Raw offsets
are checked against max_offset using the source's four-decimal amplitude
precision. This numeric consistency allowance is not a manufacturing tolerance.

`SurfaceMixSideOffsets` strictly decodes the side records. `SurfaceMixSideBridge`
maps canonical C/M/Y/W amplitudes to explicit, distinct physical filament slots
in the actual executor's component order. Display colour matching cannot
silently substitute a different physical slot. ImageMap retains its existing
wall geometry, width/offset application and physical tool translation.

## Opt-In Contract

The side calibration JSON carries a separate `surfacemix_side_offsets` object:

- schema: `surfacemix.imagemap-side-offsets.v1`
- enabled and experimental: true
- execution_model: `imagemap_independent_offsets`
- canonical_to_filament_slot: four distinct selected slots
- catalogue: original decoded side catalogue
- assume_normalized_srgb: explicit assumption if source encoding is undeclared
- previous_side_settings: restoration metadata for the staged UI change

No transport means legacy behaviour. Invalid enabled records or mappings stop
the wall executor rather than using generic colours. This first version requires
exactly four components, unit gamma, full global gradient strength, and no
compact-offset or dithering transformation. Raw filament-offset texture atlases
cannot replace the selected measured amplitudes. Unknown historical backing,
layer cycle, original line settings and optical execution remain unknown.
The existing ImageMap layer cycle is an experimental execution choice, not
proof of the calibration sheet's historical chronological order.

## UI

The existing SurfaceMix page gains "Use measured side offsets (experimental)"
and independent C/M/Y/W slot controls. The bundled catalogue is used on request;
the user does not have to find a JSON file. Confirmation explains the assumptions.
Changes are staged until OK, and Cancel does not commit them. Enabling selects
measured side mode, unit gamma and disables dithering/compact offsets. Disabling
restores the previous calibration JSON, name and saved settings. Imported
transports without restoration metadata disable measured side mode rather than
silently leaving it active. To execute side colouring, the ImageMap zone must
actually be assigned to walls, not only to solid infill.

## Verification

- [x] Seven focused CTest suites pass, including 41 side-importer assertions
  in five C++ cases and five wall-witness verifier controls.
- [x] Initial linked native checks pass: 29,156 assertions, including all 1,680
  records and explicit reordered-slot coefficients; these are not physical tests.
- [x] Separate side wall smoke generates outer-wall paths using tools 0/1/2/3
  on both constant-colour fixture blocks.
- [x] Duplicate-slot slicing control stops before G-code export.
- [x] Combined fixture retains all 38 top cell/depth targets with zero wrong-tool,
  missing-depth or interval conflicts, alongside four-tool wall generation.
- [x] Final finite-value guard/native rerun: 29,157 assertions passed, including
  rejection of non-finite gamma. Counts overlap the initial linked run.
- [x] Shipping executable rebuild, native checks, combined top/side fixture and invalid-slot rejection pass. Evidence: `docs/surfacemix-side-offset-20261025/shipping-final/SHIPPING_VERIFICATION.json`.
- [ ] Live GUI startup/appearance and user interaction.
- [ ] Exact side exposure/recipe-volume coverage and historical execution parity.
- [ ] Physical colour, backing optics and print validation.

Fixture evidence is under `docs/surfacemix-side-offset-20261025/`.
Initial linked checks are under `build-surface-recipe-importer/side-offset-integration/`.
The full application rebuild log is `docs/surfacemix-side-offset-build-20261025.log`.
It was interrupted cleanly and resumed with four compile jobs after the native
checks released memory; continued log:
`docs/surfacemix-side-offset-build-resume-20261025.log`. Completed objects are
reused. The old executable is not advertised as containing the new controls
until the shipping rebuild and fixture checks pass.
Shipping checks are queued behind the active build, using
`scripts/verify-surface-mix-side-integration.py`. After they finish, the durable
result will be `docs/surfacemix-side-offset-20261025/shipping-final/SHIPPING_VERIFICATION.json`.
An absent result is not a pass; inspect the build logs above. The queued checks
run with separate fixture data directories and cannot upload or print.

The generated fixture G-code is explicitly NOT FOR PRINTING. The positive wall
test proves path/tool presence, not full coverage, exact offsets at every point
or optical accuracy. Interpolation/transitions between different side recipes
and the effects of width/offset clamping remain to be characterised. No printer
upload or print is started. Product geometry and the active Rust planner's
approval gates remain unchanged.
