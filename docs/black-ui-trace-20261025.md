# Black native UI and startup stalls: 2026-10-25

## Observed

- Both supplied screenshots show the GL viewport drawing while the top
  navigation and left native widget region are black/absent. This is broader
  than a menu background colour issue.
- `workingdoc` launched `build-surface-mix/src/orca-slicer-imagemap` directly,
  with no GDK backend selection. The older v1 launcher defaulted to x11.
- Hyprland reports the current process (16565, comm `orcaslicer_main`) with
  `xwayland=false`; its environment contains WAYLAND_DISPLAY and DISPLAY,
  but no GDK_BACKEND override. This confirms native Wayland for this run.
- Source diffs do not show the native sidebar being deliberately hidden by
  the new SurfaceMix controls. This is not proof that all UI code is correct.
- Startup log contains `post_init: glcontext not ready, postpone init`.
- The screenshot identifies 1,891,528 model triangles. During startup the
  process used approximately 79% CPU and 1.7 GiB resident memory. A separate
  `cmake --build build-surface-mix --target OrcaSlicer -j2` is active.
- System snapshot: 14 GiB RAM total, 13 GiB used, 1.8 GiB available,
  2.5 GiB swap used. Swap activity and model processing can aggravate stalls;
  this snapshot alone does not identify the blocked main-thread call.

## Narrow workaround

`workingdoc` now defaults GDK_BACKEND to x11 (XWayland), matching the older
launcher. It keeps the same executable, working directory, arguments and
default data directory. TONI_GDK_BACKEND allows explicit comparison without
changing global desktop settings. Startup stdout/stderr is captured under
`~/.local/state/tonimcqueen-slicer/` while remaining visible in the terminal.

No C++ source, product geometry, profiles, printer settings or concurrent
build was changed. No process was terminated and no second GUI was launched.

## Verification and limits

- [x] Confirm launcher/backend difference and native Wayland window state.
- [x] Inspect screenshots, running process, source diffs and startup log.
- [x] Add launcher-only backend workaround and startup output capture.
- [ ] Verify native topbar/sidebar painting after a clean X11 launch.
- [ ] Verify startup responsiveness after the parallel build finishes.
- [ ] Profile the main thread if Wait/Terminate prompts remain.

Wayland/GL/native-widget compositing is the leading black-UI hypothesis,
not a proven root cause until the X11 comparison. CPU activity is not proof
that the UI main thread is responsive or that a deadlock cannot exist.
Close/save the existing GUI normally before retesting. Do not terminate it
merely because the compositor offers a Wait/Terminate prompt during loading.

Source logs inspected:
`~/.config/OrcaSlicer-ImageMap/log/debug_Sun_Oct_25_01_06_46_6127.log.0`
and `debug_Sun_Oct_25_01_09_53_16565.log.0` in that same directory.
