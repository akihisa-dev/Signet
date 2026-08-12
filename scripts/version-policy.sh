#!/bin/sh

# Signet version policy checker.  It intentionally uses only POSIX shell,
# Git, and the standard text utilities available on the supported runner.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=${SIGNET_VERSION_POLICY_ROOT:-$(CDPATH= cd -- "$script_dir/.." && pwd)}
cd "$repo_root"

usage() {
  cat <<'EOF'
Usage: scripts/version-policy.sh [--allow-major] <current|message|staged|range> ...

  current                 Check the CMake version plumbing and static consumers.
  message <file-or-args>  Check one commit message. A file is read as-is;
                          otherwise the arguments are joined with newlines.
  staged [message-file]   Check the index and a proposed message. The message
                          comes from the file argument, SIGNET_COMMIT_MESSAGE_FILE,
                          or SIGNET_COMMIT_MESSAGE, in that order.
  range <base> <head>     Check linear commits in base..head in chronological order.
                          Use --migration-base <commit> to exclude one bootstrap
                          commit from policy and use its CMake version as baseline.

Major version changes require a `!` subject marker and explicit
--allow-major or SIGNET_ALLOW_MAJOR=1 (SIGNET_ALLOW_BREAKING=1 is accepted too).
EOF
}

die() {
  echo "version-policy: $*" >&2
  exit 1
}

require_file() {
  [ -f "$1" ] || die "missing file: $1"
}

valid_version() {
  awk -v value="$1" 'BEGIN {
    exit !(value ~ /^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/)
  }'
}

version_from_cmake() {
  input=${1:-/dev/stdin}
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
      if (projects != 1 || versions != 1 || version == "") exit 1
      print version
    }
  ' "$input"
}

version_at_commit() {
  commit=$1
  git cat-file -e "$commit:CMakeLists.txt" 2>/dev/null || return 1
  git show "$commit:CMakeLists.txt" | version_from_cmake
}

version_part() {
  printf '%s\n' "$1" | cut -d. -f"$2"
}

expected_bump() {
  previous=$1
  type=$2
  breaking=$3
  previous_major=$(version_part "$previous" 1)
  previous_minor=$(version_part "$previous" 2)
  previous_patch=$(version_part "$previous" 3)
  if [ "$breaking" -eq 1 ]; then
    BUMPED_VERSION=$(awk -v major="$previous_major" 'BEGIN { print (major + 1) ".0.0" }')
  elif [ "$type" = feat ]; then
    BUMPED_VERSION=$(awk -v major="$previous_major" -v minor="$previous_minor" \
      'BEGIN { print major "." (minor + 1) ".0" }')
  else
    BUMPED_VERSION=$(awk -v major="$previous_major" -v minor="$previous_minor" \
      -v patch="$previous_patch" 'BEGIN { print major "." minor "." (patch + 1) }')
  fi
}

validate_body() {
  body=$1
  for label in scope purpose content check impact; do
    case "$label" in
      scope) pattern='^Scope:[[:space:]]+[a-z][a-z0-9_-]*[[:space:]]*$' ;;
      purpose) pattern='^目的:[[:space:]]+[^[:space:]]' ;;
      content) pattern='^内容:[[:space:]]+[^[:space:]]' ;;
      check) pattern='^確認:[[:space:]]+[^[:space:]]' ;;
      impact) pattern='^影響:[[:space:]]+[^[:space:]]' ;;
    esac
    printf '%s\n' "$body" | grep -Eq "$pattern" || die "commit body must contain a non-empty ${label} field"
  done
}

validate_message() {
  message=$1
  subject=$(printf '%s\n' "$message" | sed -n '1p')
  body=$(printf '%s\n' "$message" | sed '1d')
  [ -n "$subject" ] || die "commit subject is empty"

  SUBJECT_TYPE=$(printf '%s\n' "$subject" | sed -nE \
    's/^([a-z]+)(!)?: ([0-9]+\.[0-9]+\.[0-9]+) (.+)$/\1/p')
  SUBJECT_VERSION=$(printf '%s\n' "$subject" | sed -nE \
    's/^([a-z]+)(!)?: ([0-9]+\.[0-9]+\.[0-9]+) (.+)$/\3/p')
  SUBJECT_DESCRIPTION=$(printf '%s\n' "$subject" | sed -nE \
    's/^([a-z]+)(!)?: ([0-9]+\.[0-9]+\.[0-9]+) (.+)$/\4/p')
  [ -n "$SUBJECT_TYPE" ] || die "subject must match '<type>[!]: <version> <日本語説明>'"
  valid_version "$SUBJECT_VERSION" || die "subject version is not SemVer: $SUBJECT_VERSION"
  case "$SUBJECT_TYPE" in
    feat|fix|docs|test|refactor|perf|build|ci|chore|style|revert) ;;
    *) die "unsupported commit type: $SUBJECT_TYPE" ;;
  esac
  printf '%s\n' "$SUBJECT_DESCRIPTION" | grep -Eq '[一-龯ぁ-んァ-ヶー]' \
    || die "subject description must contain Japanese text"
  case "$subject" in
    *'!: '* ) SUBJECT_BREAKING=1 ;;
    *) SUBJECT_BREAKING=0 ;;
  esac
  validate_body "$body"
}

allow_major=0
if [ "${SIGNET_ALLOW_MAJOR:-}" = 1 ] || [ "${SIGNET_ALLOW_BREAKING:-}" = 1 ]; then
  allow_major=1
fi
if [ "$#" -gt 0 ]; then
  case "$1" in
    --allow-major|--allow-breaking)
      allow_major=1
      shift
      ;;
  esac
fi
[ "$#" -gt 0 ] || { usage >&2; exit 2; }
mode=$1
shift

check_bump() {
  previous=$1
  next=$2
  type=$3
  breaking=$4
  expected_bump "$previous" "$type" "$breaking"
  if [ "$next" != "$BUMPED_VERSION" ]; then
    die "${type} must bump ${previous} to ${BUMPED_VERSION}, got ${next}"
  fi
  previous_major=$(version_part "$previous" 1)
  next_major=$(version_part "$next" 1)
  if [ "$breaking" -eq 1 ] && [ "$next_major" = "$previous_major" ]; then
    die "a breaking subject must make a major version change"
  fi
  if [ "$next_major" != "$previous_major" ]; then
    [ "$breaking" -eq 1 ] || die "major version changes require a ! subject marker"
    [ "$allow_major" -eq 1 ] || die "major version change requires --allow-major or SIGNET_ALLOW_MAJOR=1"
  fi
}

read_message_file_or_args() {
  [ "$#" -gt 0 ] || die "message requires a file or subject/body arguments"
  if [ "$#" -eq 1 ] && [ -f "$1" ]; then
    MESSAGE=$(cat "$1")
    return
  fi
  MESSAGE=$1
  shift
  while [ "$#" -gt 0 ]; do
    MESSAGE="$MESSAGE
$1"
    shift
  done
}

check_current() {
  require_file CMakeLists.txt
  require_file cmake/SignetVersion.h.in
  require_file src/main.cpp
  require_file cmake/verify_macos_bundle.cmake
  current_version=$(version_from_cmake CMakeLists.txt) \
    || die "CMakeLists.txt must contain exactly one project VERSION"
  valid_version "$current_version" || die "CMake project version is not SemVer: $current_version"

  grep -Fq 'configure_file(' CMakeLists.txt || die "CMake must configure the generated version header"
  grep -Fq 'cmake/SignetVersion.h.in' CMakeLists.txt || die "CMake version template is not wired"
  grep -Fq '@PROJECT_VERSION@' cmake/SignetVersion.h.in || die "version template must use PROJECT_VERSION"
  grep -Fq 'CMAKE_CURRENT_BINARY_DIR' CMakeLists.txt || die "generated version header must use the binary directory"
  grep -Fq 'target_include_directories(Signet PRIVATE' CMakeLists.txt \
    || die "Signet target must include the generated header directory"
  grep -Fq 'signet_version.h' src/main.cpp || die "main.cpp must include the generated version header"
  grep -Fq 'SIGNET_VERSION_STRING' src/main.cpp || die "main.cpp must display the generated version"
  if grep -Eq 'QStringLiteral\("[0-9]+\.[0-9]+\.[0-9]+"\)' src/main.cpp; then
    die "main.cpp contains a duplicated product version"
  fi
  grep -Fq 'MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"' CMakeLists.txt \
    || die "bundle version must use PROJECT_VERSION"
  grep -Fq 'MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"' CMakeLists.txt \
    || die "bundle short version must use PROJECT_VERSION"
  grep -Fq 'SIGNET_EXPECTED_VERSION' cmake/verify_macos_bundle.cmake \
    || die "bundle verifier must receive an expected version"
  grep -Fq 'SIGNET_EXPECTED_VERSION is required' cmake/verify_macos_bundle.cmake \
    || die "bundle verifier must require the CMake expected version"
  if awk -v product_version="$current_version" '
      /SIGNET_(SHORT|BUNDLE)_VERSION/ && index($0, product_version) { found=1 }
      END { exit found ? 0 : 1 }
    ' cmake/verify_macos_bundle.cmake; then
    die "bundle verifier contains a duplicated product version"
  fi
  echo "version-policy: current version ${current_version} is wired to display and bundle metadata"
}

check_message_mode() {
  read_message_file_or_args "$@"
  validate_message "$MESSAGE"
  echo "version-policy: commit message is valid (${SUBJECT_TYPE} ${SUBJECT_VERSION})"
}

staged_message() {
  if [ "$#" -gt 1 ]; then
    die "staged accepts at most one message file"
  elif [ "$#" -eq 1 ]; then
    [ -f "$1" ] || die "staged message file does not exist: $1"
    MESSAGE=$(cat "$1")
  elif [ -n "${SIGNET_COMMIT_MESSAGE_FILE:-}" ]; then
    [ -f "$SIGNET_COMMIT_MESSAGE_FILE" ] || die "SIGNET_COMMIT_MESSAGE_FILE does not exist"
    MESSAGE=$(cat "$SIGNET_COMMIT_MESSAGE_FILE")
  elif [ -n "${SIGNET_COMMIT_MESSAGE:-}" ]; then
    MESSAGE=$SIGNET_COMMIT_MESSAGE
  else
    die "staged message source is required: file argument, SIGNET_COMMIT_MESSAGE_FILE, or SIGNET_COMMIT_MESSAGE"
  fi
}

check_staged() {
  staged_message "$@"
  validate_message "$MESSAGE"
  staged_version=$(git show :CMakeLists.txt 2>/dev/null | version_from_cmake) \
    || die "staged index must contain CMakeLists.txt with one project VERSION"
  valid_version "$staged_version" || die "staged CMake version is not SemVer: $staged_version"
  [ "$staged_version" = "$SUBJECT_VERSION" ] \
    || die "staged CMake version ${staged_version} does not match subject ${SUBJECT_VERSION}"
  staged_paths=$(git diff --cached --name-only)
  printf '%s\n' "$staged_paths" | grep -Fxq CMakeLists.txt \
    || die "staged change must modify CMakeLists.txt version"
  non_cmake=$(printf '%s\n' "$staged_paths" | grep -Fvx CMakeLists.txt || true)
  [ -n "$non_cmake" ] || die "version-only staged changes are not allowed"
  previous_version=$(git show HEAD:CMakeLists.txt 2>/dev/null | version_from_cmake) \
    || previous_version=${SIGNET_BASE_VERSION:-}
  [ -n "$previous_version" ] || die "cannot determine previous version; set SIGNET_BASE_VERSION for migration"
  valid_version "$previous_version" || die "previous version is not SemVer: $previous_version"
  check_bump "$previous_version" "$staged_version" "$SUBJECT_TYPE" "$SUBJECT_BREAKING"
  echo "version-policy: staged change is valid (${SUBJECT_TYPE} ${previous_version} -> ${staged_version})"
}

check_range() {
  migration_base=
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --migration-base)
        [ "$#" -ge 2 ] || die "--migration-base requires a commit"
        migration_base=$2
        shift 2
        ;;
      --allow-major|--allow-breaking)
        allow_major=1
        shift
        ;;
      *) break ;;
    esac
  done
  [ "$#" -eq 2 ] || { usage >&2; exit 2; }
  base=$1
  head=$2
  git rev-parse --verify "$base^{commit}" >/dev/null 2>&1 || die "invalid range base: $base"
  git rev-parse --verify "$head^{commit}" >/dev/null 2>&1 || die "invalid range head: $head"
  commits=$(git rev-list --reverse "$base..$head")
  previous_version=
  seen_versions=
  if [ -n "$migration_base" ]; then
    migration_commit=$(git rev-parse --verify "$migration_base^{commit}" 2>/dev/null) \
      || die "invalid migration base: $migration_base"
    first_commit=$(printf '%s\n' "$commits" | sed -n '1p')
    [ "$first_commit" = "$migration_commit" ] \
      || die "migration base must be the first commit in the checked range"
    git merge-base --is-ancestor "$base" "$migration_commit" \
      || die "migration base is not descended from range base"
    git merge-base --is-ancestor "$migration_commit" "$head" \
      || die "migration base is not an ancestor of range head"
  fi
  for commit in $commits; do
    if [ -n "$migration_base" ] && [ "$commit" = "$migration_commit" ]; then
      previous_version=$(version_at_commit "$commit") \
        || die "migration commit must contain a readable CMake project version"
      valid_version "$previous_version" || die "migration version is not SemVer: $previous_version"
      seen_versions="$previous_version"
      continue
    fi
    parent=$(git rev-parse "$commit^" 2>/dev/null) \
      || die "commit $commit has no parent; specify it outside the range or as migration base"
    [ -n "$previous_version" ] || previous_version=$(version_at_commit "$parent") \
      || die "commit $commit has no previous CMake version; specify a migration base"
    message=$(git show -s --format=%B "$commit")
    validate_message "$message"
    current_version=$(version_at_commit "$commit") \
      || die "commit $commit must contain CMakeLists.txt with one project VERSION"
    valid_version "$current_version" || die "commit $commit has a non-SemVer CMake version"
    if printf '%s\n' "$seen_versions" | grep -Fxq "$SUBJECT_VERSION"; then
      die "duplicate version in range: $SUBJECT_VERSION"
    fi
    [ "$current_version" = "$SUBJECT_VERSION" ] \
      || die "commit $commit CMake version ${current_version} does not match subject ${SUBJECT_VERSION}"
    [ "$current_version" != "$previous_version" ] \
      || die "commit $commit does not change the CMake product version"
    paths=$(git diff-tree --no-commit-id --name-only -r "$commit")
    printf '%s\n' "$paths" | grep -Fxq CMakeLists.txt \
      || die "commit $commit must change CMakeLists.txt"
    non_cmake=$(printf '%s\n' "$paths" | grep -Fvx CMakeLists.txt || true)
    [ -n "$non_cmake" ] || die "commit $commit is version-only"
    check_bump "$previous_version" "$current_version" "$SUBJECT_TYPE" "$SUBJECT_BREAKING"
    echo "version-policy: $commit ${SUBJECT_TYPE} ${previous_version} -> ${current_version}"
    seen_versions="$seen_versions
$current_version"
    previous_version=$current_version
  done
  echo "version-policy: range ${base}..${head} is valid"
}

case "$mode" in
  current)
    [ "$#" -eq 0 ] || { usage >&2; exit 2; }
    check_current
    ;;
  message)
    check_message_mode "$@"
    ;;
  staged)
    check_staged "$@"
    ;;
  range)
    check_range "$@"
    ;;
  -h|--help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
