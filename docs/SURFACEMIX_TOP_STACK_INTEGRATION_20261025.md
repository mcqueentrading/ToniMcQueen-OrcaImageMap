# SurfaceMix Experimental Top-Stack Integration

## Scope

The user asked this session to take over the slicer integration and move beyond
the read-only preview. Work stays in ToniMcQueen-Slicer. The active Rust planner
and control-service sources are not edited, and their diagnostic gates are not
promoted to product approval.

This is an opt-in experimental connection to ImageMap's existing upper-surface
infill machinery. It is not a new slicer, arbitrary-mesh planner, wall-depth
painter, lateral dither implementation or physical colour certificate.

## Data Path

```text
existing ImageMap mapped texture target
  -> immutable measured Rust catalogue
  -> one whole nearest measured recipe
  -> explicit canonical C/M/Y/W to filament-slot mapping
  -> actual consecutive surface-to-depth slice intervals
  -> existing ImageMap component regions and Orca extrusion generation
```

`SurfaceMixTopStack` validates the transport and shares the existing exact Lab
recipe selector with the display adapter. It never synthesizes a recipe,
normalizes side offsets, truncates intervals, repeats a short stack, or sorts
the selected stack by transmission distance. Source order is surface-to-deep;
the adapter reverses it only to match Orca's bottom-to-top execution convention.

The physical slots are not tool IDs. Orca's printer/filament map still owns that
translation. The controlled fixture declares slots 1/2/3/4 to tools 0/1/2/3;
that is not an inferred universal printer mapping.

For this catalogue all complete top recipes have 19 x 0.08 mm intervals,
totalling 1.52 mm. Actual layer IDs and heights must be supplied and compatible.
Missing/short/changed intervals stop with a slicing error, not a generic colour
fallback. Lower-surface and unsupported perimeter-recolouring execution remain
rejected. Top masks and texture projection remain ImageMap-owned; no new
product-wide vertex-ownership review is introduced for this integration.

The catalogue's own assignment flag remains false: importing measurements is not
an executable plan. The separate `surfacemix.imagemap-top-stacks.v1` transport
requires explicit experimental enablement and its own colour-encoding assumption.
This does not claim that the original backing, optics or side execution metadata
has become known.

## UI And Persistence

Image Texture > Advanced Options > Options > SurfaceMix remains its own page.
It now has a separate experimental slicing checkbox and four filament-slot inputs.
The existing display mode and inward-depth inspection controls remain independent.

Slicing changes are staged until OK. Cancel does not commit the draft. Applying
uses the existing zone-change path. Disabling restores the previous top settings;
an imported transport without a restoration snapshot disables top painting rather
than silently activating the generic painter. Existing calibration fields remain
alongside the explicit new transport, and malformed replacements do not overwrite
the active zone. The page scrolls so status text cannot hide the lower controls.

## Verification Status

- [x] Existing strict importer, display adapter and bundled-catalogue checks pass.
- [x] Eight new transport test cases / 40 assertions pass.
- [x] First linked native check passes against all 1,120 retained top records.
- [x] Wrong-tool and missing-path verifier controls reject the supplied bad output.
- [x] Original Carnival OBJ and bundled calibration hashes remain unchanged.
- [x] Latest pre-fix linked native check, including the imported-disable regression (22,424 assertions).
- [x] Actual fixture path bindings pass for both cells and all 19 inward intervals; repeat report is byte-identical.
- [x] Complete shipping executable rebuild and headless fixture slicing.
- [ ] Live GUI startup/appearance verification after reopening the rebuilt application.
- [ ] Live UI acceptance in the rebuilt application.
- [ ] Full-volume/extrusion coverage and physical colour validation.

The first linked result recorded 22,423 checks. These are internal assertions,
not 22,423 independent physical tests. Later runs must be reported separately.
The fixture G-code verifier measures interior segment witnesses, not full recipe
volume, bead shape, backing optics or boundary completeness.

Evidence and retained failed attempts:
`docs/surfacemix-top-stack-integration-20261024/`.
Native build products can additionally live under
`build-surface-recipe-importer/top-stack-native-verified/`.

The first legacy CLI attempt failed an incomplete filament-vector input check.
The next attempt reproduced the pre-existing GUI-singleton dereference in the
headless extruder check; the debugger identified `get_extruders_under_cli`.
That method now uses a local texture manager, without mutating GUI presets.
The earlier isolated nullable-vector compatibility fix is ported into the host
with tests preserving nil markers and dynamic option types.

Initial fixture attempts are retained as failures, not advertised as successful
material execution. One generator used `texture` instead of the actual
`texture_options` wire key. A new sample-normal filter was tested and removed
as a possible cause, but removing it did not restore top output; it was not a
confirmed root cause. ImageMap's own top-region selection remains in use.
Fresh actual-path verification remains required.

## Resumed Missing-Fill Investigation

The retained region trace shows no fill surfaces in the top fixture layers,
before SurfaceMix constructs its component slices. A source review found an
input/output alias in `split_one_wall_shell_infill_surfaces`: the caller passes
the same polygon vector as the original boundary and remaining wall output.
The function replaced that vector before computing the top-fill clip from it.
The correction computes the clip first, matching the ordering already used by
the adjacent `split_top_surfaces` function.

Verification on 2026-10-25:

- `fixture-preparation-trace-09`: old splitter has zero fill regions immediately
  after perimeter generation in the top layers; all 38 requested targets fail.
- `fixture-alias-fixed-10`: both cells have witnesses at all 19 inward intervals;
  878 interior extrusion witnesses, physical tools 0/1/2/3, zero wrong-tool,
  missing-depth or interval conflicts.
- `fixture-alias-fixed-repeat-11`: the same path-binding report, byte-for-byte.
- The public `process_classic` rectangle regression fails with the old linked
  splitter and passes with its correction. An initial test-harness attempt
  omitted the adjacent-layer polygons required for an interior layer; this was
  corrected in the harness, not treated as a production slicer failure.
- The corrected native suite passes 22,426 assertions across the 1,120 retained
  records, transaction/interval checks and the new one-wall checks. Its logs are
  under `build-surface-recipe-importer/top-stack-alias-fix/` (`regression-*`).
- All five standalone CTest suites pass. The original OBJ and catalogue hashes
  remain as recorded above/in the fixture manifest.

The final shipping executable build completed successfully. The same fixture
passes in `fixture-shipping-12`, with a path-binding report byte-identical to
the isolated passing run (878 witnesses, 38 cell/depth targets, tools 0/1/2/3,
zero missing, tool or interval conflicts). All five standalone suites pass again.
These results are path witnesses, not complete area/volume coverage or
physically validated colour.

Shipping executable SHA-256:
`846d83962889d2e7e352656116bc9a00ad6193b8b2f428d4ad08b0c58266cbb8`.
Build logs: `build-resume-20261025.log` and `build-final-20261025.log` in the
evidence directory. Builds retain upstream warnings, including a TBB
`notify_type` One Definition Rule warning at final link; they are not
warning-free builds. No dependency rebuild or warning suppression was attempted.

## Testing In The Application

Save any open project before closing the older application. The existing
project `workingdoc` launcher opens the newly built executable with the
configured X11 backend and a per-startup log; it does not enable experimental
assignment automatically.

1. Open a textured model and configure its Image Texture mapping zone.
2. In Advanced Options, select the SurfaceMix page from Options.
3. Use the bundled catalogue; replacing it with a separate JSON is optional.
4. Enable "Use measured top stacks when slicing (experimental)" and explicitly
   map C/M/Y/W to the actual filament slots. Check the confirmation before OK.
5. For this catalogue, use 0.08 mm layers and at least 19 top-shell layers
   (1.52 mm). The entire selected recipe must fit; this is not a request to
   resize a measured recipe or silently truncate it on a thin feature.
6. Slice and inspect actual filament/tool paths in Preview. This remains an
   experiment, not approval to upload or print.

The generated fixture G-code and profiles are explicitly test-only, not printer
instructions. Do not send the fixture G-code to a printer. Arbitrary product
coverage, wall-depth execution, lateral striation/dithering, backing optics and
physical colour reproduction remain unverified or unimplemented.

## Product Boundaries

No Carnival geometry, original profiles, model dimensions, repair proposal,
Rust planner gate or physical printer is changed. No upload or print is started.
The optional wall threshold of 1.26 mm remains distinct from the 1.52 mm measured
top stack and from printable lateral spacing.

Next: establish passing fixture path bindings and finish the GUI rebuild. Then
extend controlled top-region/path coverage before adding a separately gated
wall-depth transport. Do not imply that a coloured preview or a successful CLI
exit establishes correct extrusion or physical colour reproduction.
