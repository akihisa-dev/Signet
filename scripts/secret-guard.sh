#!/bin/sh

# Secret Guard intentionally uses only POSIX shell, Git, and standard text
# utilities. It reports paths and rule names, never matching content.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

usage() {
  cat <<'EOF'
Usage: scripts/secret-guard.sh <worktree|staged|range> ...

  worktree              Scan tracked and non-ignored untracked text files.
  staged                Scan the staged index contents only.
  range <base> <head>   Scan added lines and commit messages in base..head.

Generated build/app artifacts, binary files, third-party license trees, and
checksum files are excluded by path. Findings report only a relative path and
rule name; secret values are never printed.
EOF
}

die() {
  echo "secret-guard: $*" >&2
  exit 2
}

repo_root=$(git rev-parse --show-toplevel 2>/dev/null) \
  || die "must run inside a Git worktree"
cd "$repo_root"

temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/signet-secret-guard.XXXXXX")
trap 'rm -rf "$temporary_root"' EXIT HUP INT TERM

findings=0

is_excluded_path() {
  case "$1" in
    build|build/*|build-*|build-*/*|dist|dist/*|out|out/*|.cache|.cache/*)
      return 0
      ;;
    *.app|*.app/*|*.dSYM|*.dSYM/*|compile_commands.json)
      return 0
      ;;
    licenses|licenses/*|third_party|third_party/*|third-party|third-party/*)
      return 0
      ;;
    vendor|vendor/*|external|external/*|THIRD_PARTY_NOTICES.md|LICENSE)
      return 0
      ;;
    checksums.sha256|*.sha256)
      return 0
      ;;
  esac
  return 1
}

report_finding() {
  findings=1
  printf '%s\n' "secret-guard: rejected $1 ($2)" >&2
}

scan_assignments() {
  scan_file=$1
  awk '
    function skip_prefix(value, character) {
      while (length(value) > 0) {
        character = substr(value, 1, 1)
        if (character == " " || character == "\t" || character == "\"" ||
            character == sprintf("%c", 39)) {
          value = substr(value, 2)
        } else {
          break
        }
      }
      return value
    }

    function suspicious_value(value, lowered) {
      if (length(value) < 8 ||
          value !~ /^[A-Za-z0-9][A-Za-z0-9_\.\/+=:-]*$/) {
        return 0
      }
      lowered = tolower(value)
      if (lowered ~ /^(example|dummy|fake|test|testing|placeholder|changeme|change_me|your|replace|redacted|masked|none|null|value|secret|token|password|foo|bar|baz|invalid)([-_.]|$)/ ||
          lowered ~ /^\$\{?[A-Za-z_][A-Za-z0-9_]*\}?$/ ||
          lowered ~ /^<.*>$/) {
        return 0
      }
      return 1
    }

    function line_has_assignment(line, lowered_line, key, start, offset,
                                 position, before, rest, character, value,
                                 value_length) {
      lowered_line = tolower(line)
      for (key in secret_keys) {
        start = 1
        while (start <= length(line)) {
          offset = index(substr(lowered_line, start), key)
          if (offset == 0) {
            break
          }
          position = start + offset - 1
          before = position > 1 ? substr(lowered_line, position - 1, 1) : ""
          if (before !~ /[A-Za-z0-9_]/) {
            rest = substr(line, position + length(key))
            rest = skip_prefix(rest)
            if (substr(rest, 1, 1) == ":" || substr(rest, 1, 1) == "=") {
              rest = skip_prefix(substr(rest, 2))
              value = rest
              value_length = 0
              while (value_length < length(value)) {
                character = substr(value, value_length + 1, 1)
                if (character == " " || character == "\t" ||
                    character == "\"" || character == sprintf("%c", 39) ||
                    character == "," || character == ";" || character == "}") {
                  break
                }
                value_length++
              }
              value = substr(value, 1, value_length)
              if (suspicious_value(value)) {
                return 1
              }
            }
          }
          start = position + length(key)
        }
      }
      return 0
    }

    BEGIN {
      secret_keys["password"] = 1
      secret_keys["passwd"] = 1
      secret_keys["pwd"] = 1
      secret_keys["secret"] = 1
      secret_keys["token"] = 1
      secret_keys["api_key"] = 1
      secret_keys["api-key"] = 1
      secret_keys["apikey"] = 1
      secret_keys["access_token"] = 1
      secret_keys["access-token"] = 1
      secret_keys["auth_token"] = 1
      secret_keys["auth-token"] = 1
      secret_keys["private_key"] = 1
      secret_keys["private-key"] = 1
      secret_keys["client_secret"] = 1
      secret_keys["client-secret"] = 1
      secret_keys["github_token"] = 1
      secret_keys["openai_api_key"] = 1
      secret_keys["aws_access_key_id"] = 1
      secret_keys["aws_secret_access_key"] = 1
    }

    {
      if (line_has_assignment($0)) {
        found = 1
      }
    }

    END {
      exit found ? 0 : 1
    }
  ' "$scan_file"
}

scan_file_rules() {
  scan_label=$1
  scan_file=$2

  if LC_ALL=C grep -Eq \
    '(^|[^[:alnum:]])-----BEGIN[[:space:]]+(RSA[[:space:]]+|EC[[:space:]]+|OPENSSH[[:space:]]+|DSA[[:space:]]+|PGP[[:space:]]+)?PRIVATE[[:space:]]+KEY-----' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" private-key-header
  fi
  if LC_ALL=C grep -Eq '(^|[^[:alnum:]])gh[pousr]_[A-Za-z0-9_]{20,}([^A-Za-z0-9_]|$)' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" github-token
  fi
  if LC_ALL=C grep -Eq '(^|[^[:alnum:]])sk-(proj-)?[A-Za-z0-9_-]{20,}([^A-Za-z0-9_-]|$)' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" openai-token
  fi
  if LC_ALL=C grep -Eq '(^|[^[:alnum:]])(AKIA|ASIA)[A-Z0-9]{16}([^A-Za-z0-9]|$)' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" aws-access-key
  fi
  if LC_ALL=C grep -Eq '(^|[^[:alnum:]])glpat-[A-Za-z0-9_-]{20,}([^A-Za-z0-9_-]|$)' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" gitlab-token
  fi
  if LC_ALL=C grep -Eq '(^|[^[:alnum:]])npm_[A-Za-z0-9]{20,}([^A-Za-z0-9]|$)' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" npm-token
  fi
  if LC_ALL=C grep -Eq '(^|[^[:alnum:]])xox[baprs]-[A-Za-z0-9-]{10,}([^A-Za-z0-9-]|$)' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" slack-token
  fi
  if LC_ALL=C grep -Eq '(^|[^[:alnum:]])AIza[0-9A-Za-z_-]{30,}([^A-Za-z0-9_-]|$)' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" google-api-key
  fi
  if LC_ALL=C grep -Eq '(^|[^[:alnum:]])(sk|rk)_live_[0-9A-Za-z]{16,}([^A-Za-z0-9]|$)' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" stripe-token
  fi
  if LC_ALL=C grep -Eq '(^|[^[:alnum:]])ya29\.[0-9A-Za-z_-]{20,}([^A-Za-z0-9_-]|$)' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" google-oauth-token
  fi
  if LC_ALL=C grep -Eq \
    '(^|[^[:alnum:]_])/(Users|home|private/var|var/folders|private/tmp|tmp)/[A-Za-z0-9._/-]+' \
    "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" local-absolute-path
  fi
  if scan_assignments "$scan_file" >/dev/null 2>&1; then
    report_finding "$scan_label" secret-assignment
  fi
}

scan_text_file() {
  scan_label=$1
  scan_file=$2
  [ -r "$scan_file" ] || die "cannot read scan input"
  if LC_ALL=C grep -Iq . "$scan_file" >/dev/null 2>&1; then
    scan_file_rules "$scan_label" "$scan_file"
  else
    scan_status=$?
    [ "$scan_status" -eq 1 ] || die "cannot inspect scan input"
  fi
}

scan_worktree() {
  path_list="$temporary_root/worktree-paths"
  git ls-files --cached --others --exclude-standard > "$path_list" \
    || die "cannot enumerate worktree files"
  while IFS= read -r scan_path || [ -n "$scan_path" ]; do
    [ -n "$scan_path" ] || continue
    is_excluded_path "$scan_path" && continue
    [ -L "$scan_path" ] && continue
    [ -f "$scan_path" ] || die "worktree file disappeared: $scan_path"
    scan_text_file "$scan_path" "$scan_path"
  done < "$path_list"
  echo "secret-guard: worktree scan passed"
}

scan_staged() {
  path_list="$temporary_root/staged-paths"
  git diff --cached --name-only --diff-filter=ACMR > "$path_list" \
    || die "cannot enumerate staged files"
  while IFS= read -r scan_path || [ -n "$scan_path" ]; do
    [ -n "$scan_path" ] || continue
    is_excluded_path "$scan_path" && continue
    staged_file="$temporary_root/staged-input"
    git cat-file blob ":$scan_path" > "$staged_file" 2>/dev/null \
      || die "cannot read staged file"
    scan_text_file "$scan_path" "$staged_file"
  done < "$path_list"
  echo "secret-guard: staged scan passed"
}

range_current_path=
range_additions_file=

flush_range_additions() {
  if [ -n "$range_current_path" ] &&
     ! is_excluded_path "$range_current_path" &&
     [ -s "$range_additions_file" ]; then
    scan_text_file "$range_current_path" "$range_additions_file"
  fi
  : > "$range_additions_file"
}

scan_range() {
  [ "$#" -eq 2 ] || { usage >&2; exit 2; }
  range_base=$1
  range_head=$2
  git rev-parse --verify "$range_base^{commit}" >/dev/null 2>&1 \
    || die "invalid range base"
  git rev-parse --verify "$range_head^{commit}" >/dev/null 2>&1 \
    || die "invalid range head"

  diff_file="$temporary_root/range-diff"
  git diff --no-ext-diff --no-color --no-renames --unified=0 \
    "$range_base" "$range_head" -- > "$diff_file" \
    || die "cannot read range diff"
  range_additions_file="$temporary_root/range-additions"
  : > "$range_additions_file"
  range_current_path=
  while IFS= read -r diff_line || [ -n "$diff_line" ]; do
    case "$diff_line" in
      '+++ b/'*)
        flush_range_additions
        range_current_path=${diff_line#+++ b/}
        ;;
      '+++ /dev/null')
        flush_range_additions
        range_current_path=
        ;;
      +*)
        [ -n "$range_current_path" ] || continue
        printf '%s\n' "${diff_line#+}" >> "$range_additions_file"
        ;;
    esac
  done < "$diff_file"
  flush_range_additions

  commit_list="$temporary_root/range-commits"
  git rev-list --reverse "$range_base..$range_head" > "$commit_list" \
    || die "cannot enumerate range commits"
  while IFS= read -r range_commit || [ -n "$range_commit" ]; do
    [ -n "$range_commit" ] || continue
    commit_file="$temporary_root/commit-message"
    git show -s --format=%B "$range_commit" > "$commit_file" 2>/dev/null \
      || die "cannot read range commit message"
    scan_text_file "commit-message" "$commit_file"
  done < "$commit_list"
  echo "secret-guard: range scan passed"
}

[ "$#" -gt 0 ] || { usage >&2; exit 2; }
case "$1" in
  worktree|current)
    [ "$#" -eq 1 ] || { usage >&2; exit 2; }
    scan_worktree
    ;;
  staged)
    [ "$#" -eq 1 ] || { usage >&2; exit 2; }
    scan_staged
    ;;
  range)
    shift
    scan_range "$@"
    ;;
  -h|--help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

[ "$findings" -eq 0 ] || exit 1
