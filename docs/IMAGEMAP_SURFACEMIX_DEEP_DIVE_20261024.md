# ImageMap Source Audit and SurfaceMix Painter Integration

Date: 2026-10-24. Checkout: `/home/unknown/Documents/scripts/projectcampaign/ToniMcQueen-Slicer`.
HEAD: `92548381056dbf72836b0a1bdc455f238218dbfb`, with pre-existing uncommitted integration changes.

Follow-up: importer hardening is now implemented and separately tested. See
`SURFACEMIX_IMPORTER_HARDENING_20261024.md`. Findings below retain the original
audit observations; the reported integer-wrap and dropped-band defects are
fixed in current source. Painting integration remains unimplemented.

## Decision and Scope

The user accepts the existing texture placement for this integration. Keep
ImageMap's model, UV, texture and colour-source machinery. A new vertex/ownership
audit is not a prerequisite for top-colour preview. This is an accepted scope,
not a claim that this pass proved every UV correct.

Keep the full texture-colour field, not the earlier fixed virtual mixed-filament
palette. Arbitrary texture targets still have a limited printable gamut, governed
by measured recipes, filament optics and printable pattern dimensions.

```text
existing ImageMap mapped RGBA
  -> SurfaceRecipeField: complete measured recipes across the surface
  -> SurfaceToDepthStack: ordered physical component intervals per recipe
  -> intentional lateral pattern / optional wall-depth eligibility
  -> existing Orca material regions, fills, perimeters and tool assignment
```

No repair, model rewrite, slicing, assignment or printing was performed. REA was
not run: its local README identifies an inspection/reverse-engineering tool,
not an ImageMap code donor. Earlier calling it unrelated missed the intended use.
Source inspection suffices for this static audit; the running binary and old
printed job were not verified.

## Findings

### 1. Our painters are not yet connected to ImageMap

`src/slic3r/GUI/MainFrame.cpp:3313` reads JSON into a temporary
`SurfaceRecipeField`, displays a text summary and returns. It does not attach
the field to a model, render colours or feed slicing. `VALIDATED (read-only)`
does not mean a calibrated painted result.

The existing Rust pieces are useful, but are not two finished product painters:

- `creator5-fullspectrum-rs/src/surface_recipe_field/mod.rs` defines
  `SurfaceRecipeField`, `SurfaceRecipeCell`, `StackSelection` and
  `SurfaceToDepthStack`. Its analysis entry accepts named planar fixtures only.
- `creator5-fullspectrum-rs/src/analytic_recipe_field/` handles analytic curved
  fixtures, not arbitrary Carnival OBJ painting.
- `creator5-fullspectrum-rs/src/calibration_catalog.rs` preserves measured
  recipes and separates components, filament slots and physical tools.
- `ToniMcQueen-Slicer/src/libslic3r/SurfaceRecipeField.*` currently supplies an
  inspection contract and ordered-depth lookup, not a painting adapter.

Reuse those components; add the connection, not a new UV mapper or slicer.

### 2. Side-field smoothing can lose the measured recipe

`TextureMappingOffset.cpp:1646` selects component weights, including measured
side candidates. The layer-local XY field stores those vectors.
`sample_weight_field_components_impl()` at line 2448 then averages them in its
ordinary branch. An isolated compile of that exact function reproduced:

```text
recipe A = [1, 0, 0, 0]
recipe B = [0, 1, 0, 0]
midpoint lookup -> approximately [0.5, 0.5, 0, 0]
```

The result is neither input recipe and cannot inherit either measured colour.
Stored sample normals do not disambiguate this lookup in the control, and this
field carries no surface/sheet identity. Missing samples can return aggregate
fallback weights. These are reproduced mechanisms, not the proven cause of the
historical print. The raw top-label branch selected a discrete component in its
control, making it a more promising transport for preselected recipes.

### 3. Calibration loaded is not top painting enabled

Inspected saved candidate:
`/home/unknown/Desktop/Documents/3dprints/carnival_dancer/calibrated/carnival_dancer_creator5_cmyw_imagemap_calibrated.3mf`.
Its relationship to the actual printed job remains unestablished.

| Stored setting | Value |
|---|---|
| Zone / colour model / slots | 99 / CMYW / 1,2,3,4 |
| Texture interpretation / modulation | target colour / line width |
| Top-surface image colouring | disabled |
| Dithering | disabled |
| Side calibration | nearest measured sample |
| Stored top samples / side swatches | 560 / 560 |
| Measured top stack | 19 x 0.08 mm = 1.52 mm |
| Configured top stack limit | 100 layers |

`Fill.cpp:9456` skips disabled top painting. Loading its calibration alone does
not run it. If later enabled, 100 is not permission to stretch, repeat or truncate
a measured 19-layer recipe. Actual selected intervals must remain compatible.

### 4. Top labels and wall offsets have different meanings

`ImageMapRawFilamentOffsetAtlas.cpp:657` decodes per-depth top filament labels;
`TextureMappingOffset.cpp:1997` samples mapped colours and optionally these
labels. `Fill.cpp:9171` has a raw-top route before generic contoning planning.
This is a concrete seam to evaluate for our top painter.

Side raw channels instead carry independent offset amplitudes. Line-width or
perimeter modulation is not a complete inward CMYW volume. Do not interpret
an offset weight as 1.26 mm of reserved material. The wall painter needs its
own physical-depth-to-slice/material-region adapter.

### 5. Harden our inspection importer before runtime painting

The probe found two concrete issues in our current C++ bridge:

- `SurfaceRecipeField.cpp` converts JSON components to `uint8_t` before CMYW
  range validation. Both `256` and `-256` were accepted as component `0`.
- `from_json()` does not populate `transition_bands`; an unresolved input band
  decoded as zero bands.

These are not historical print causes, since this bridge does not execute
painting. Fix and regression-test them before connecting it to runtime output.
No production fixes were made in this audit.

## Source and Reuse Map

Paths below are relative to the checkout's `src/libslic3r/`, except where noted.

| Seam | Purpose |
|---|---|
| `Format/OBJ.cpp:140`, `Model.cpp:833` | retain per-corner UV import and texture attachment |
| `TextureMappingOffset.cpp:1902`, `:1945`, `:2150` | current source priority: painted RGBA facets, UV texture, vertex fallback |
| `TextureMappingOffset.cpp:1997`, `:1646` | mapped colours before generic component decisions; primary integration boundary |
| `TextureMappingOffset.cpp:2448` | do not average whole calibrated recipe vectors here |
| `ImageMapRawFilamentOffsetAtlas.cpp:657`, `Fill/Fill.cpp:9171` | discrete per-depth top labels and component regions |
| `TextureMappingContoning.cpp:702`, `:784` | measured interval compatibility and prediction fallback |
| `LayerRegion.cpp:2602` | existing perimeter consumer; modulation alone is not our depth painter |
| `TextureMapping.cpp:4006` | global layer/component scheduling; not a per-cell calibrated stack |
| `Fill/Fill.cpp:736` | existing `ORCASLICER_TOP_SURFACE_COLORING_DEBUG` exports; reuse diagnostics |

Do not revive the legacy Rust shortcuts as our painter authority:
`creator5-fullspectrum-rs/src/calibration.rs` flattens top stacks into proportions
and assumes one-based components; `src/surface_mix.rs` has a triangle-index parity
heuristic. Neither preserves the new complete-recipe and physical-spacing model.

## Narrow Implementation Sequence

1. Harden the importer: check integer range before narrowing, preserve transition
   bands/provenance/statuses and explicitly accept only supported schemas.
2. Attach a read-only recipe overlay to the existing ImageMap zone. Keep mapped
   RGBA, source identity, complete recipe ID, calibration hash and ordered
   intervals. No new OBJ projection or global manual-ownership gate for preview.
3. Connect top output to discrete per-depth labels/regions. Translate canonical
   components to filament slots explicitly. Keep complete measured stacks and
   transitions; do not send chosen recipes back through generic mixing.
4. Connect optional wall-depth painting separately, using conditional 1.26 mm
   eligibility. Unknown/insufficient depth must not erase the top preview.
   Shorter recipes require their own calibration, not truncation of a full one.
5. Add deliberate lateral placement at printable physical spacing, preserving
   complete per-cell stacks. Compare requested intervals with actual Orca
   paths before claiming calibrated execution.

Top-stack thickness (1.52 mm in this profile), optional wall threshold (1.26 mm)
and lateral pattern width are distinct. Local-normal depth is also not identical
to build-Z layers. Converting curved wall depth to Orca paths needs an explicit
coordinate and material-region contract.

## Evidence and Limits

Evidence: `docs/imagemap-audit-20261024/evidence-02/`.

- Seven isolated mechanism/control checks passed; repeated output matched.
- Three strict-contract observations failed: two integer-wrap inputs and one
  discarded transition band. Do not call this an all-green integration test.
- Twenty source/header/shader inputs and the inspected archive were hash-
  compared before/after, with no changes.
- The probe compiles exact extracted field source plus the existing
  `SurfaceRecipeField.cpp`. This is not a full slicer build or live UI test.
- No Carnival geometry was read or rescanned. No model, original asset, Rust
  planner, slicer runtime source or print configuration was changed.

Next milestone: a visible mapped-colour/whole-recipe overlay in ImageMap, then
separate top and wall execution connections. Do not substitute another
whole-product vertex-review loop for this integration work.

Implementation follow-up: `SURFACEMIX_VISIBLE_RECIPE_PREVIEW_20261024.md` records
the new native, read-only catalogue preview and its separate build/UI acceptance
status. The findings above remain the original audit snapshot.
