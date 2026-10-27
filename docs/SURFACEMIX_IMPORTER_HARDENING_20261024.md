# SurfaceMix Recipe Importer Hardening

## Scope

This pass changes the recipe JSON reader, not OBJ import, UV mapping, calibration
measurements, colour selection or painting execution. The installed/running GUI
was not rebuilt or replaced. Unrelated dirty files and the Rust planner were
left alone. No commit, repair, model edit, slice, upload or print was performed.

## Changes

- CMYW components are checked as wide integers in 0..3 before conversion to
  `uint8_t`, including backing/core components. Values such as -256 or 256 no
  longer wrap to cyan. Component zero remains valid.
- Physical intervals and chart coordinates reject out-of-range int64 numbers,
  nonintegers and malformed structures rather than silently converting them.
- Transition bands retain cell IDs, bounds and explicit hard/unresolved rules.
  Hard boundaries may have zero width; unresolved bands require positive area.
  Unknown rules, reversed bounds and missing/same-cell references are rejected.
- Backing/core fields are read from the nested field or legacy envelope;
  conflicting copies are rejected instead of silently selecting or discarding one.
- The reader explicitly supports the existing fixture-v1 field schema and
  rejects conflicting/unknown schemas. It does not promote analytic reports or
  arbitrary product plans into a new accepted schema.
- Malformed JSON field types produce a reported rejection. Successful reads
  clear stale errors. Valid recipe identities, sequence and depth intervals are
  preserved without averaging, truncating or reordering.

Declared calibration hashes are preserved, not cryptographically authenticated
by this importer. Structural import validity is not geometry, colour, path or
physical-print validation. A band marked unresolved remains unresolved.

## Verification

- Added a standalone CMake test target using the repository's Catch2, without
  configuring or rebuilding Orca's GUI/dependency stack.
- Corrected extra array nesting in the pre-existing test fixture's intervals.
  With that fixture valid, the unmodified reader failed nine of twelve test
  cases; the corrected reader passes all twelve (130 assertions).
- Checked all eight saved planar fixture reports: seven fields preserve their
  stacks, declared hashes, component intervals, backing/core and transition
  records; the uncalibrated hybrid remains rejected as expected.
- The saved reports still declare geometry/path status unknown and execution
  disabled. The checker inspects their nested field, not a new runtime plan.
- Reran the isolated ImageMap source probe: seven mechanism/control checks pass,
  both integer-wrap controls are now rejected and the transition is retained.
  Repeated probe output is identical. ImageMap side-weight averaging itself is
  deliberately unchanged in this pass.

Evidence:

- `build-surface-recipe-importer/before-hardening.log`: initial discovery of the
  malformed old test fixture; not the meaningful numeric-regression baseline.
- `build-surface-recipe-importer/before-hardening-valid-fixture.log`: nine
  failures against the original reader with valid test inputs.
- `build-surface-recipe-importer/after-hardening.log`: twelve passing cases.
- `build-surface-recipe-importer/ctest-final.log`: standalone CTest pass.
- `build-surface-recipe-importer/saved-fixture-checks.json`: eight saved-report
  compatibility checks, including expected hybrid rejection.
- `docs/imagemap-audit-20261024/evidence-after-importer-hardening/`: source probe,
  repeat output, compiler command/log and protected-input hashes.

Source changes: `src/libslic3r/SurfaceRecipeField.cpp` and its tests. Test
infrastructure is in `tests/surface_recipe_field/`. Earlier audit evidence is
retained; its original importer failures describe the pre-fix code.

## Next Connection

Attach an opt-in read-only recipe overlay to ImageMap's existing mapped colour
source. Keep whole recipe IDs and ordered depth intervals. Do not reduce the
texture to a fixed virtual-filament palette or silently average selected
calibrated recipe vectors. Top and optional wall-depth execution remain later
connections, not a claimed result of this importer fix.
