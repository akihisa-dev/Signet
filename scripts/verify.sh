#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
cd "$repo_root"

usage() {
  cat <<'EOF'
Usage: scripts/verify.sh <dev|sanitize|format|lint|bundle|version|version-self-test|secret|secret-self-test|all>

  dev       Configure, build, and test the development preset.
  sanitize  Configure, build, and test the sanitizer preset.
  format    Check C++ formatting with the repository .clang-format file.
  lint      Configure and build the existing clang-tidy analysis path.
  bundle    Build, install, and verify the existing release bundle.
  version   Check the CMake product version wiring and consumers.
  version-self-test  Run version policy fixtures without staging this repository.
  secret    Scan the current worktree for secret-shaped content.
  secret-self-test  Run secret guard fixtures in temporary repositories.
  all       Run version, secret, dev, sanitize, format, lint, and bundle checks.
EOF
}

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: required command not found: $1" >&2
    exit 2
  fi
}

require_macos_arm64() {
  if [ "$(uname -s)" != "Darwin" ] || [ "$(uname -m)" != "arm64" ]; then
    echo "error: Signet verification requires macOS arm64" >&2
    exit 2
  fi
}

extract_project_version() {
  awk '
    /^[[:space:]]*project[[:space:]]*\(/ {
      projects++
      in_project=1
    }
    in_project && /(^|[[:space:]])VERSION[[:space:]]+/ {
      line=$0
      sub(/^.*VERSION[[:space:]]+/, "", line)
      split(line, fields, /[[:space:]]+/)
      version=fields[1]
      versions++
    }
    in_project && /\)/ { in_project=0 }
    END {
      if (projects != 1 || versions != 1 ||
          version !~ /^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/) exit 1
      print version
    }
  ' CMakeLists.txt
}

run_dev() {
  cmake --preset dev
  cmake --build --preset dev --parallel
  ctest --preset dev --output-on-failure
}

run_sanitize() {
  cmake --preset sanitize
  cmake --build --preset sanitize --parallel
  ctest --preset sanitize --output-on-failure
}

find_clang_tidy() {
  if [ -n "${CLANG_TIDY_EXECUTABLE:-}" ] && [ -x "$CLANG_TIDY_EXECUTABLE" ]; then
    printf '%s\n' "$CLANG_TIDY_EXECUTABLE"
    return 0
  fi

  if command -v clang-tidy >/dev/null 2>&1; then
    command -v clang-tidy
    return 0
  fi

  if command -v brew >/dev/null 2>&1; then
    llvm_prefix=$(brew --prefix llvm 2>/dev/null || true)
    if [ -n "$llvm_prefix" ] && [ -x "$llvm_prefix/bin/clang-tidy" ]; then
      printf '%s\n' "$llvm_prefix/bin/clang-tidy"
      return 0
    fi
  fi

  return 1
}

find_clang_format() {
  if [ -n "${CLANG_FORMAT_EXECUTABLE:-}" ] && [ -x "$CLANG_FORMAT_EXECUTABLE" ]; then
    printf '%s\n' "$CLANG_FORMAT_EXECUTABLE"
    return 0
  fi

  if command -v clang-format >/dev/null 2>&1; then
    command -v clang-format
    return 0
  fi

  if command -v brew >/dev/null 2>&1; then
    llvm_prefix=$(brew --prefix llvm 2>/dev/null || true)
    if [ -n "$llvm_prefix" ] && [ -x "$llvm_prefix/bin/clang-format" ]; then
      printf '%s\n' "$llvm_prefix/bin/clang-format"
      return 0
    fi
  fi

  return 1
}

run_format() {
  format=$(find_clang_format || true)
  if [ -z "$format" ]; then
    echo "error: clang-format is not installed; install the existing llvm toolchain before running format" >&2
    echo "hint: brew install llvm" >&2
    exit 2
  fi

  for file in $(rg --files src tests -g '*.cpp' -g '*.h' -g '*.hpp'); do
    "$format" --dry-run --Werror "$file"
  done
}

run_lint() {
  tidy=$(find_clang_tidy || true)
  if [ -z "$tidy" ]; then
    echo "error: clang-tidy is not installed; install the existing llvm toolchain before running lint" >&2
    echo "hint: brew install llvm" >&2
    exit 2
  fi

  cmake -S . -B build/clang-tidy -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=/opt/homebrew \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DBUILD_TESTING=OFF \
    -DSIGNET_ENABLE_CLANG_TIDY=ON \
    -DCLANG_TIDY_EXECUTABLE="$tidy" \
    -DCMAKE_COMPILE_WARNING_AS_ERROR=ON
  cmake --build build/clang-tidy --parallel
}

run_bundle() {
  expected_version=$(extract_project_version) || {
    echo "error: could not extract one SemVer project version from CMakeLists.txt" >&2
    exit 1
  }
  cmake --preset release
  cmake --build --preset release --parallel
  stage=$(mktemp -d)
  trap 'rm -rf "$stage"' EXIT HUP INT TERM
  echo "bundle: fresh temporary install stage created"
  cmake --install build/release --prefix "$stage"
  report_bundle_evidence "$stage/Signet.app"
  cmake \
    -DSIGNET_EXPECTED_VERSION="$expected_version" \
    -DSIGNET_BUNDLE="$stage/Signet.app" \
    -DSIGNET_SOURCE_DIR="$repo_root" \
    -DSIGNET_BUILD_DIR="$repo_root/build/release" \
    -P cmake/verify_macos_bundle.cmake
}

report_bundle_evidence() {
  bundle=$1
  if [ ! -d "$bundle" ]; then
    echo "bundle evidence: completed app bundle is missing" >&2
    return 1
  fi

  if [ -d "$bundle/Contents/Frameworks" ]; then
    frameworks=$(find "$bundle/Contents/Frameworks" -type f -print | wc -l | tr -d ' ')
  else
    frameworks=0
  fi
  if [ -d "$bundle/Contents/PlugIns" ]; then
    plugins=$(find "$bundle/Contents/PlugIns" -type f -print | wc -l | tr -d ' ')
  else
    plugins=0
  fi
  if [ -d "$bundle/Contents/Resources/licenses" ]; then
    licenses=$(find "$bundle/Contents/Resources/licenses" -type f -print | wc -l | tr -d ' ')
  else
    licenses=0
  fi

  manifest="$bundle/Contents/Resources/licenses/runtime/manifest.tsv"
  checksum_manifest="$bundle/Contents/Resources/licenses/runtime/checksums.sha256"
  if [ -f "$manifest" ]; then
    manifest_rows=$(awk '!/^[[:space:]]*#/ && NF { rows++ }
      END { print rows + 0 }' "$manifest")
    manifest_paths=$(awk -F'|' '!/^[[:space:]]*#/ && NF {
      paths += split($4, fields, ",")
    } END { print paths + 0 }' "$manifest")
    manifest_evidence="present (rows=${manifest_rows}, bundle-paths=${manifest_paths})"
  else
    manifest_evidence=missing
  fi
  if [ -f "$checksum_manifest" ]; then
    checksum_entries=$(awk 'NF && $1 !~ /^#/ { entries++ }
      END { print entries + 0 }' "$checksum_manifest")
    checksum_evidence="present (entries=${checksum_entries})"
  else
    checksum_evidence=missing
  fi

  macho_files=$(find "$bundle" -type f -print)
  macho_count=0
  while IFS= read -r bundle_file || [ -n "$bundle_file" ]; do
    [ -n "$bundle_file" ] || continue
    if file "$bundle_file" 2>/dev/null | grep -q 'Mach-O'; then
      macho_count=$((macho_count + 1))
    fi
  done <<EOF
$macho_files
EOF

  if [ -f "$bundle/Contents/Info.plist" ]; then
    bundle_version=$(plutil -extract CFBundleShortVersionString raw -o - \
      "$bundle/Contents/Info.plist")
  else
    bundle_version=missing
  fi
  echo "bundle evidence: Frameworks files=${frameworks}"
  echo "bundle evidence: PlugIns files=${plugins}"
  echo "bundle evidence: licenses files=${licenses}"
  echo "bundle evidence: manifest.tsv=${manifest_evidence}"
  echo "bundle evidence: checksums.sha256=${checksum_evidence}"
  echo "bundle evidence: Mach-O files=${macho_count}"
  echo "bundle evidence: CFBundleShortVersionString=${bundle_version}"
}

run_version() {
  "$script_dir/version-policy.sh" current
}

run_version_self_test() {
  "$script_dir/test-version-policy.sh"
}

run_secret() {
  "$script_dir/secret-guard.sh" worktree
}

run_secret_self_test() {
  "$script_dir/test-secret-guard.sh"
}

if [ "$#" -ne 1 ]; then
  usage >&2
  exit 2
fi

case "$1" in
  version)
    require_command git
    run_version
    exit 0
    ;;
  version-self-test)
    require_command git
    run_version_self_test
    exit 0
    ;;
  secret)
    require_command git
    run_secret
    exit 0
    ;;
  secret-self-test)
    require_command git
    run_secret_self_test
    exit 0
    ;;
esac

require_command cmake
require_command ninja
require_command ctest
require_macos_arm64

case "$1" in
  dev)
    run_dev
    ;;
  sanitize)
    run_sanitize
    ;;
  format)
    run_format
    ;;
  lint)
    run_lint
    ;;
  bundle)
    run_bundle
    ;;
  all)
    run_version
    run_version_self_test
    run_secret
    run_secret_self_test
    run_dev
    run_sanitize
    run_format
    run_lint
    run_bundle
    ;;
  -h|--help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
