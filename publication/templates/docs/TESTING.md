# Validation status

## Transfer validation — 2026-11-09

Follow-up UI test fix: resizing is verified at two sizes above the actual Qt
layout minimum, rather than assuming every system accepts exactly 1010 pixels.
The additional `ui_size_constraints` test covers a 1100-pixel minimum. The updated
standalone suite passed 22 tests; the NVIDIA hardware test was skipped locally.
The destination user's original run passed its NVIDIA decode test. This fix
changes test coverage only; application and GPU code are unchanged.

On the source Omarchy 4.5 system: clean standalone Release build with CUDA
13.4 succeeded. CUDA inspection confirmed sm_75 and sm_89 cubins plus compute_75
PTX. 21 C++ tests passed; the NVIDIA device test was explicitly skipped because
the execution environment could not access a working NVIDIA driver. 19 Python
publication/transfer/installer tests passed. An isolated installation launched
successfully and its CPU decode diagnostic reported AVX2; all dynamic library
dependencies resolved. Existing application packaging (12 tests) and system
migration tests (80) also passed.

This is **not** an Ubuntu 26.04.1 / RTX 4070 desktop certification. No such
machine was available here; the container daemon was inaccessible. On the
destination run `bash run.sh install --install-cuda --nvjpeg ON --test` followed
by `bash run.sh doctor --gpu`, then the desktop smoke test below. Ubuntu
dependencies are downloaded on installation, not bundled in the source archive.

The source installer detects Ubuntu 24.04/26.04 and RHEL 9/10. RHEL-compatible
Alma/Rocky/CentOS 9/10 and Arch are also accepted. Package plans and installer
transaction handling are tested with isolated temporary prefixes. The public
source tree is compiled and its tests run locally before handoff; consult the
publication handoff report for exact results.

The provided GitHub matrix builds in Ubuntu 24.04/26.04 and AlmaLinux 9/10
containers. These jobs have **not** run merely because the YAML exists. They
must pass on GitHub before the artifacts are called tested builds for those
environments. AlmaLinux is not actual RHEL. Exact RHEL desktop validation uses
the opt-in self-hosted workflow on subscribed RHEL 9/10 systems.

Offscreen Qt tests check UI logic, not native compositor behaviour. EGL shader
tests can return SKIP (77) if the runner has no graphics device. Ordinary
GitHub runners have no NVIDIA GPU: a skipped nvJPEG hardware test does not
prove that CUDA works. Validate `--nvjpeg ON` on a real supported NVIDIA GPU
before describing a release as tested with nvJPEG. CPU fallback is tested.

Desktop smoke test on every target: open JPEG/TIFF/RAW, resize/zoom/pan,
compare, check Unicode/long paths, fullscreen/Esc/maximize, Open With, USB,
copy/cancel, favourites, repeated thumbnail scrolling and orderly exit.
Verify image/ICC fidelity with known references; monitor RAM/VRAM over many
repeated cycles. A finite passing test run cannot prove absence of all leaks.

## GTK front end (0.13)

`gtk_settings` checks the opacity preference without a display. `gtk_ui`
(ctest label `display`) drives a real GTK window on the session compositor
through the program's named actions: listing, open, 1:1 zoom, tabs, comparison,
bookmarks, favourites, language, dialogs, copy/duplicates and the opacity
slider, including that the photograph's pixels are identical at every chrome
opacity. It skips without a display and is excluded from the installer's
`--test` gate (`ctest -LE display`), because its outcome depends on the running
compositor. It does not synthesize keyboard, pointer, drag-and-drop or pinch
input; verify those by hand. The previous Qt Widgets window stays buildable as
`ky-prism-qt` with its offscreen event tests when Qt Widgets is installed.
