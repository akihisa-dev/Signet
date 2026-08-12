# Runtime license inventory

`manifest.tsv` is the authoritative release-bundle inventory. Each row maps a
component and version to the actual files found in the Release install, the
license/notice files installed with that component, and the local upstream
source evidence used to create those files. Bundle paths are relative to the
`.app` directory; license paths are relative to `Contents/Resources/licenses`.
Use the manifest and the files in this directory as the source of current
counts; documentation does not duplicate those values.

`checksums.sha256` covers the runtime license payload files in this directory.
`manifest.tsv` is dynamic inventory metadata and is intentionally excluded to
avoid a self-reference cycle. The bundle verifier separately checks the
manifest's content and placement, while rejecting missing, extra, or modified
payload files.

The text files under the component directories are byte-for-byte copies from
the installed Homebrew Cellar paths named by the manifest. Multiple upstream
license texts are retained where the formula metadata or upstream distribution
offers multiple terms; the manifest does not collapse those alternatives into
a single unsupported choice. CGAL's commercial alternative is retained as an
upstream notice but is not selected for this GPL build.

Qt 6.11.1 supplies aggregate and module SPDX SBOM files, while the local
Homebrew bottle has no standalone Qt core license file. `Qt-6.11.1/LGPL-3.0.txt`
is an unmodified GNU LGPL v3 text copied from the locally verified
`/opt/homebrew/Cellar/gmp/6.3.0/COPYING.LESSERv3`; its SHA-256 and GNU source
URL are recorded in `Qt-6.11.1-LICENSING-NOTICE.txt` alongside the Qt official
licensing URL. Qt Core, GUI, and Widgets select LGPL-3.0-only. The Qt-GPL
exception is not selected for this runtime; module-specific additional notices
remain version-dependent. This inventory does not claim legal completeness.

## Updating the inventory

When a dependency or Homebrew bottle changes, regenerate the Release install
in a temporary directory and compare the complete Mach-O set with the bundle.
Update `manifest.tsv` and copy the exact upstream license/notice files without
editing their contents. Keep the Qt official licensing URL, LGPL-3.0 text and
checksum, the Qt notice-pointer file, and the aggregate/module SPDX SBOM files
together.
Regenerate the checksum file from the runtime directory, excluding only
`checksums.sha256`, then run:

```sh
(cd licenses/runtime && shasum -a 256 -c checksums.sha256)
expected_version="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p' CMakeLists.txt)"
cmake -DSIGNET_BUNDLE="$stage/Signet.app" \
  -DSIGNET_EXPECTED_VERSION="$expected_version" \
  -DSIGNET_SOURCE_DIR="$PWD" \
  -DSIGNET_BUILD_DIR="$PWD/build/release" \
  -P cmake/verify_macos_bundle.cmake
```

The verifier is a read-only check. It requires every non-system Mach-O to be
listed exactly once, every manifest path to exist and be Mach-O, every mapped
license file to be non-empty, and all absolute non-system dependencies and
RPATHs to be absent.

Apple system frameworks and `/usr/lib` libraries are intentionally absent:
they are supplied by macOS and are not redistributed by this bundle.
