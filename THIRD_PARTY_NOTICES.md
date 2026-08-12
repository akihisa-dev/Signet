# Third-party software

This file records the direct dependencies used by the current source tree. It is not a substitute for the license files that must accompany a distributed application bundle.

| Dependency | Current requirement | Use | Upstream licensing reference |
|---|---:|---|---|
| Qt | 6.11.1 | Core, GUI, Widgets, and macOS platform plugin | [Qt Licensing](https://doc.qt.io/qt-6.11/licensing.html) |
| CGAL | 6.2 | Exact kernel and arrangements with history | [CGAL License](https://doc.cgal.org/6.2/Manual/license.html) |
| GMP | Homebrew-resolved version | Exact arithmetic runtime | [GNU MP Licensing](https://gmplib.org/manual/Copying) |
| MPFR | Homebrew-resolved version | Exact arithmetic support runtime | [GNU MPFR](https://www.mpfr.org/) |

CMake and Ninja are build tools and are not bundled by the current application target.

For a Release install, the CMake install step copies `LICENSE`, this notice, the
fixed runtime inventory (`licenses/runtime/manifest.tsv`), its checksum
manifest, the applicable unmodified upstream license/notice files, the
unmodified CGAL license set, GMP and MPFR `COPYING` files, and the Qt 6.11.1
official notice plus SBOM into `Signet.app/Contents/Resources/licenses/`. The exact GMP and MPFR
versions are the versions resolved by the build's dependency installation; the
checked-in manifest records the corresponding source evidence.

The bundle includes `runtime/Qt-6.11.1/LGPL-3.0.txt`, an unmodified GNU LGPL-3
text with its recorded SHA-256. The
GNU source URL (`https://www.gnu.org/licenses/lgpl-3.0.txt`) and Qt official
licensing URL (`https://doc.qt.io/qt-6.11/licensing.html`) are recorded in the
Qt notice. Qt Core, GUI, and Widgets select LGPL-3.0-only. The Qt-GPL exception
is not selected for this runtime; module-specific additional notices remain
version-dependent. This document does not assert legal completeness.

The successful Release install verification checks the manifest against the
bundled non-system Mach-O files with exact path correspondence, requires
each manifest path and mapped license file to exist, verifies the runtime
checksum manifest, recursively checks every bundled Mach-O with `otool -L`,
rejects Homebrew, workspace, and build-dependency absolute paths and RPATHs,
requires arm64-only binaries and the Qt platform plugin, and checks the
Info/version/minimum-OS metadata. QtPdf, QtSvg, and QtVirtualKeyboard plugins
are not bundled. Qt's official notice, SBOM, runtime inventory, and the
corresponding license files are included in the bundle; they do not by
themselves establish legal completeness.

This repository does not claim that a local install is signed or notarized.
Signing, notarization, and publication remain separate release operations.
