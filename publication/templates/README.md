# ky-prism

GPU photo viewer by **Krzysztof Kylen Lenartowicz**. Free to use and share.
JPEG, TIFF and camera RAW; asynchronous loading, OpenGL image/thumbnail
rendering, zoom and pan, EXIF, ICC colour management, tabs, comparison,
favourites, folder bookmarks, file copy and duplicate search.

## Install

Download this repository with **Code → Download ZIP**, unpack it and open a
terminal in the unpacked folder. Run as your normal desktop user:

```bash
bash install.sh
```

The installer detects the distribution, uses `sudo` only to install missing
system dependencies, builds locally, installs in `~/.local`, and registers
the application under **Open With**. Start it from the desktop application
menu or run `~/.local/bin/ky-prism /path/to/photo.jpg`. Log out/in if your
desktop has not yet added `~/.local/bin` to PATH. Image defaults are left to
your desktop's **Open With → Set as default** setting.

| System | Installation |
| --- | --- |
| Ubuntu 24.04 LTS / 26.04 LTS | `bash install.sh` (Ubuntu Universe must be available) |
| RHEL 9 / RHEL 10 | `bash install.sh --setup-rhel-repos` on a registered, updated desktop |
| RHEL with dependencies already managed by an administrator | `bash install.sh --no-deps` |
| Omarchy / Arch | `bash install.sh` |

For RHEL the opt-in flag enables CodeReady Builder, EPEL and Remi-safe
(libvips). It verifies upstream signing-key fingerprints and retains RPM
signature verification. Read [RHEL details](docs/RHEL.md), including the
distinction between community packages and Red Hat support.

Requires a 64-bit Linux desktop, GTK ≥4.12, Qt ≥6.4 (non-visual modules for
the decoder engine), libvips ≥8.12, LibRaw ≥0.20 and working OpenGL ≥3.3 or
OpenGL ES ≥3.0. Wayland (native) and X11 are supported; blur availability
depends on the compositor. Interface opacity is adjustable in the window
(header button or **Ctrl+Alt+O**) and never affects the photograph.
No Omarchy configuration, Hyprland, private launcher, online account or
NVIDIA GPU is required. The interface supports **Polish and English**. Click
the **PL / EN** flag button beside **About**, or press **Ctrl+Alt+L**, to switch
immediately without reopening your photo. The choice is remembered locally.
The button animation runs only during the switch. File names and paths are
never translated. You can also use `PRISM_LANGUAGE=en ky-prism` for English
at startup. Polish is the initial default. Folder and save-name choosers are
the desktop's own (XDG portal) dialogs and follow the system language.

If a process remains busy after closing its window, keep it running briefly
and collect `python3 tools/diagnose-hang.py PID --seconds 3` from the source
tree. It reads local CPU/thread/RAM information without stopping or killing
the process and sends nothing over the network. Share the report only if
you choose to. The `shutdown` regression test verifies actual process exit,
including cancellation during loading, thumbnails and open dialogs.

## NVIDIA nvJPEG: automatic when available, optional to install

**The default is AUTO at build time and at runtime.**

* With a local CUDA Toolkit ≥12 (including `nvcc` and nvJPEG), the installer
  builds the optional adapter automatically. At runtime JPEG thumbnails try
  nvJPEG first on a supported NVIDIA GPU.
* Without the adapter/GPU, or for an unsupported JPEG/colour case, decoding
  falls back to libvips / libjpeg-turbo on the CPU, with AVX2 selected on CPUs
  that support it. OpenGL rendering still uses the available graphics device.
* The adapter is primarily for JPEG **thumbnails**, not all RAW/TIFF decoding
  or every full-image decode. Colour-managed CPU fallbacks remain deliberate.
* Missing CUDA or an unsupported JPEG must not prevent the viewer from opening.

To explicitly require the optional adapter:

1. Install a compatible driver and CUDA Toolkit from
   [NVIDIA's installation guide](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/).
   Accept NVIDIA's separate license. Check `nvcc --version` and `nvidia-smi`.
2. Run:

   ```bash
   bash install.sh --nvjpeg ON
   ```

   If CUDA is in a custom location:

   ```bash
   CUDACXX=/usr/local/cuda/bin/nvcc CUDAToolkit_ROOT=/usr/local/cuda bash install.sh --nvjpeg ON
   ```

The default GPU code includes Turing 7.5 (RTX 2060), native Ada 8.9 (RTX 4070)
and forward PTX. Other compatible GPUs may JIT the PTX; older GPUs use CPU
fallback. Vendor/toolkit support and available device memory also matter.
The installer never downloads CUDA/driver binaries or modifies global GPU
routing. On hybrid systems PRIME settings are process-local.

Ubuntu users can install its packaged optional SDK and require a real GPU
decode test with `bash install.sh --install-cuda --nvjpeg ON --test`.
This requires Universe/Multiverse repositories and a working compatible
NVIDIA driver. See [the transfer guide](docs/TRANSFER-PL.md) and run
`bash run.sh doctor --gpu` after installation.

```bash
bash install.sh --nvjpeg AUTO              # normal default; no SDK required
bash install.sh --nvjpeg OFF               # build without NVIDIA adapter
PRISM_JPEG_BACKEND=cpu ky-prism photo.jpg # force CPU JPEG thumbnail decode
PRISM_GPU=system ky-prism photo.jpg      # follow the desktop's GPU choice
PRISM_GPU=nvidia ky-prism photo.jpg      # NVIDIA via X11/XWayland
```

## Uninstall, update and advanced usage

### Interface opacity

After installing a version with the opacity setting, try
`PRISM_UI_OPACITY=95 ~/.local/bin/ky-prism`. The value is **opacity**, from 0
(transparent) to 100 (opaque). 95 is useful over a busy wallpaper; 100 gives
solid panels. Only interface backgrounds change; photo pixels and text opacity
are preserved. No compositor-wide window-opacity setting is required.

To remember a value for launches from the desktop icon, create
`~/.config/ky-prism/appearance.json` containing:

```json
{"ui_opacity_percent":95}
```

Close and reopen the application after changing the value. `PRISM_UI_OPACITY`
overrides the file for one launch. Removing the file and unsetting the variable
restores the original glass materials. Configuration is read once per process,
without idle polling. Source backup/export and `restore-data` include this setting.

### Installer options

```bash
bash install.sh --check                   # dependencies/source checks; no install
bash install.sh --plan                    # distro package command; no writes
bash install.sh --jobs 4                  # override automatic bounded build parallelism
bash install.sh --prefix "$HOME/Apps/prism" # alternative prefix; launch its bin/ky-prism
bash install.sh --uninstall               # remove managed files, keep photos/preferences
```

Run the new source version's installer to update. It backs up managed files,
refuses to overwrite unrelated/modified files, and rolls back failed file
installation. Existing Omarchy-specific installations are not overwritten
silently; use a separate prefix to evaluate this standalone edition.
Dependencies are installed online from configured repositories. This public
source installer is not an offline system-recovery image.

## Build and test manually

```bash
bash install.sh --dependencies-only
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPRISM_WITH_NVJPEG=AUTO
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The GitHub workflow builds Ubuntu 24.04, Ubuntu 26.04 and AlmaLinux 9/10
(compatibility checks for RHEL) and produces native DEB/RPM artifacts plus
matching source. Exact RHEL validation also has a separate opt-in workflow
for your own subscribed RHEL desktop runners. A workflow recipe alone does
not certify an operating system: see [validation status](docs/TESTING.md).
For release steps see [PUBLISHING.md](docs/PUBLISHING.md).

## Keyboard

`Ctrl+O` open, `Ctrl+B` sidebar, `Ctrl+L` directory, `Ctrl+T` new tab,
`Ctrl+Tab` switch tab, `F11` fullscreen, `Esc` leave fullscreen,
`Ctrl+0` fit, `Ctrl+1` actual size, arrows browse, `Shift+arrows` pan,
`Ctrl+U` USB, `F1` keyboard help. See [KEYBOARD.md](KEYBOARD.md).

## License

Copyright © 2026 **Krzysztof Kylen Lenartowicz**. Own code is under the
[MIT license](LICENSE). Dependencies retain their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). CUDA/nvJPEG is proprietary
NVIDIA software and is not included in the source archive or CI binaries.

This software is based in part on the work of the Independent JPEG Group.
