# Licensing review — 2026-10-23

Scope: original ky-prism sources, public installer and the dependencies they
link to. This is an engineering compliance review, not a legal opinion or
a guarantee about every future dependency build.

## Own code and retaining copyright

MIT keeps **Krzysztof Kylen Lenartowicz** as copyright holder. It allows
free use, modification and redistribution, including commercial use and
closed derivatives, while requiring retention of the copyright/license
notice. It does not guarantee that every downstream distributor charges zero.
GPL likewise does not prohibit selling copies; it adds copyleft requirements.
If preventing closed derivatives is a requirement, choose a copyleft license
before publication. GPL combined with proprietary NVIDIA libraries requires
separate analysis and potentially a linking permission from all relevant
copyright holders; optional dynamic loading alone is not an automatic exemption.

## Distribution model

The public source archive contains our code and notices, **not** Qt, libvips,
LibRaw, CUDA SDK headers, NVIDIA drivers, codec binaries, installed account
data or private Omarchy launchers. Native CI packages dynamically use system
libraries installed by the distribution. CI does not compile the optional
CUDA adapter. This avoids promising redistribution rights over vendor SDKs.

If you later bundle LGPL libraries in AppImage/Flatpak/static packages,
re-evaluate the corresponding-source, notice, relinking and installation
requirements for those exact library versions. Linking to an upstream web
page alone is not a substitute for every binary distributor obligation.
Native dependency licenses also remain relevant; do not add an EULA forbidding
users from replacing/relinking LGPL libraries or reverse-engineering for that
purpose. No third-party library has been modified by this packaging work.

## Components

* **GTK 4 (with GLib, Pango, cairo, graphene) and libepoxy:** GTK and GLib are
  LGPL-2.1-or-later, libepoxy is MIT. Shared distro libraries; the window,
  dialogs and OpenGL canvas use them. [GTK](https://www.gtk.org/),
  [libepoxy](https://github.com/anholt/libepoxy).
* **Qt Core/Gui/Concurrent (engine); Widgets/OpenGLWidgets/Test (optional legacy
  front end and tests):** LGPLv3 option of
  Qt Base; no GPL-only Qt add-on is required. Qt Test is a build/test dependency.
  Full LGPLv3 and GPLv3 texts are provided in LICENSES. Source installer uses
  shared distro libraries and permits replacement/rebuilding.
  [Qt licensing](https://doc.qt.io/qt-6/licensing.html),
  [Qt obligations](https://www.qt.io/development/open-source-lgpl-obligations).
* **libvips:** LGPL-2.1-or-later. Shared library; its selected optional loaders
  and their transitive licenses depend on the distributor's package build.
  A libvips installation is not a statement that all its optional loaders use
  the same license. [Upstream](https://www.libvips.org/).
* **LibRaw:** upstream offers LGPL 2.1 or CDDL 1.0; use the LGPL option here.
  Retain its third-party notices if distributing LibRaw itself. The program
  uses the system shared library. [Upstream](https://www.libraw.org/about).
* **libjpeg-turbo / JPEG:** IJG/BSD-style terms and component notices.
  Documentation acknowledges the Independent JPEG Group. The installed
  distro may select another compatible libjpeg provider: do not assume its
  version/license solely from the API name.
  [Upstream license](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md).
* **Little CMS:** MIT; used through libvips for ICC colour management.
  [Upstream](https://www.littlecms.com/color-engine/).
* **zlib:** zlib license, shared direct dependency.
  [Upstream](https://zlib.net/zlib_license.html).
* **TIFF / GLib / image codecs / OpenGL implementation / C++ runtime:**
  distribution dependencies carry their own licenses. Packaging must not
  relabel these MIT. Core source releases do not redistribute their binaries.
* **CUDA Runtime / nvJPEG:** proprietary NVIDIA SDK EULA; **not MIT and not
  open source**. Public sources contain our adapter/kernels and API imports,
  not NVIDIA implementation or headers. The user separately installs the SDK.
  nvJPEG is listed among CUDA redistributables, but redistribution is
  conditional on the applicable EULA and notices, not unrestricted permission
  to bundle the entire toolkit/driver. This release does not exercise that
  permission. [NVIDIA EULA](https://docs.nvidia.com/cuda/eula/).

## Graphics and provenance

Public source contains code-drawn vector controls and an original geometric
application icon. Reference photos, Etsy images, wallpapers, Chrome logos,
Omarchy bar animations, fonts and third-party image packs are not included.
The public open-photo button uses a new simple geometric photo symbol instead
of the private reference-inspired camera drawing. Common geometric symbols
are not a trademark license; do not imply affiliation with another product.
Copyright notices and third-party attribution are retained. This review does
not independently establish exclusive ownership of AI-assisted code or give
a jurisdiction-specific opinion on copyrightability.

## Before distributing binaries

Keep source matching the binary, build instructions, dependency versions and
license notices. Publish the workflow artifacts only after its tests pass;
label each native package for its build distribution. Do not publish the
private full-system backup or a binary linked to the development machine's
libraries as if it were portable to all four distributions.
