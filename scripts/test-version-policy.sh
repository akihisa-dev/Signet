#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
policy="$script_dir/version-policy.sh"
temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/signet-version-policy.XXXXXX")
trap 'rm -rf "$temporary_root"' EXIT HUP INT TERM

fail() {
  echo "version-policy self-test: $*" >&2
  exit 1
}

new_fixture() {
  fixture=$(mktemp -d "$temporary_root/repo.XXXXXX")
  cd "$fixture"
  git init -q
  git config user.name "Signet version policy test"
  git config user.email "version-policy@example.invalid"
  mkdir -p src
  printf '%s\n' \
    'cmake_minimum_required(VERSION 3.31)' \
    'project(Sign et VERSION 0.1.0 LANGUAGES CXX)' > CMakeLists.txt
  sed 's/Sign et/Signet/' CMakeLists.txt > CMakeLists.txt.next
  mv CMakeLists.txt.next CMakeLists.txt
  printf '%s\n' 'bootstrap fixture' > src/bootstrap.txt
  git add CMakeLists.txt src/bootstrap.txt
  git commit -q -m 'bootstrap fixture'
  base=$(git rev-parse HEAD)
}

set_version() {
  new_version=$1
  awk -v new_version="$new_version" '{
    if ($0 ~ /^[[:space:]]*project\(/) {
      line=$0
      sub(/VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+/, "VERSION " new_version, line)
      print line
    } else if ($0 ~ /^[[:space:]]*project/) {
      sub(/[0-9]+\.[0-9]+\.[0-9]+/, new_version)
      print
    } else {
      print
    }
  }' CMakeLists.txt > CMakeLists.txt.next
  mv CMakeLists.txt.next CMakeLists.txt
}

commit_change() {
  version=$1
  path=$2
  subject=$3
  body=$4
  set_version "$version"
  mkdir -p "$(dirname "$path")"
  printf '%s\n' "$version fixture" > "$path"
  git add CMakeLists.txt "$path"
  git commit -q -m "$subject" -m "$body"
}

valid_body() {
  printf '%s\n' \
    'Scope: version_policy' \
    '目的: 版更新規則を検証する' \
    '内容: CMake versionを更新する' \
    '確認: self-testで検査する' \
    '影響: fixtureだけに限定する'
}

expect_failure() {
  if "$@" >/dev/null 2>&1; then
    fail "expected failure: $*"
  fi
}

body=$(valid_body)

# Every allowed type is checked: feat is a minor bump and the other ten types
# are patch bumps.  Keeping these commits in one range also verifies the
# chronological version state and duplicate detection input.
new_fixture
commit_change 0.2.0 src/feature.txt 'feat: 0.2.0 図形機能を追加' "$body"
commit_change 0.2.1 src/fix.txt 'fix: 0.2.1 表示の不具合を修正' "$body"
commit_change 0.2.2 docs/guide.txt 'docs: 0.2.2 開発手順を更新' "$body"
commit_change 0.2.3 src/style.txt 'style: 0.2.3 形式を整える' "$body"
commit_change 0.2.4 src/refactor.txt 'refactor: 0.2.4 責務を整理する' "$body"
commit_change 0.2.5 src/perf.txt 'perf: 0.2.5 処理を改善する' "$body"
commit_change 0.2.6 tests/test.txt 'test: 0.2.6 回帰を追加する' "$body"
commit_change 0.2.7 cmake/build.txt 'build: 0.2.7 構成を更新する' "$body"
commit_change 0.2.8 .github/ci.txt 'ci: 0.2.8 検証を更新する' "$body"
commit_change 0.2.9 src/chore.txt 'chore: 0.2.9 雑務を整理する' "$body"
commit_change 0.2.10 src/revert.txt 'revert: 0.2.10 変更を戻す' "$body"
SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" range "$base" HEAD >/dev/null

# Duplicate subject versions are rejected before the CMake/subject mismatch check.
new_fixture
commit_change 0.2.0 src/feature.txt 'feat: 0.2.0 図形機能を追加' "$body"
commit_change 0.2.1 src/fix.txt 'fix: 0.2.1 表示の不具合を修正' "$body"
commit_change 0.2.2 docs/duplicate.txt 'docs: 0.2.1 重複した版を拒否' "$body"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" range "$base" HEAD

# A non-feature commit that keeps the old version is a missing bump.
new_fixture
commit_change 0.1.0 src/missing-bump.txt 'fix: 0.1.0 更新を忘れた変更' "$body"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" range "$base" HEAD

# A commit containing only the CMake version change is forbidden.
new_fixture
set_version 0.1.1
git add CMakeLists.txt
git commit -q -m 'fix: 0.1.1 版だけを変更' -m "$body"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" range "$base" HEAD

# Major changes need both the subject marker and explicit authorization.  A
# breaking marker with a minor bump is also invalid.
new_fixture
commit_change 1.0.0 src/breaking.txt 'feat!: 1.0.0 互換性を変更' "$body"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" range "$base" HEAD
SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" --allow-major range "$base" HEAD >/dev/null
SIGNET_VERSION_POLICY_ROOT="$fixture" SIGNET_ALLOW_MAJOR=1 "$policy" range "$base" HEAD >/dev/null

new_fixture
commit_change 1.0.0 src/unmarked-major.txt 'feat: 1.0.0 互換性を変更' "$body"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" --allow-major range "$base" HEAD

new_fixture
commit_change 0.2.0 src/wrong-breaking-bump.txt 'feat!: 0.2.0 互換性を変更' "$body"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" --allow-major range "$base" HEAD

# Subject syntax, Japanese explanation, no-subject-scope, and each required
# body field are checked explicitly.
new_fixture
message_file="$fixture/bad-message.txt"
printf '%s\n' 'feat: 0.1.1 invalid description' "$body" > "$message_file"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" message "$message_file"
printf '%s\n' 'feat(release): 0.1.1 件名scopeを拒否' "$body" > "$message_file"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" message "$message_file"
printf '%s\n' 'unknown: 0.1.1 未許可typeを拒否' "$body" > "$message_file"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" message "$message_file"
printf '%s\n' 'feat: 01.1.1 不正な版を拒否' "$body" > "$message_file"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" message "$message_file"
printf '%s\n' \
  'fix: 0.1.1 表示を修正' \
  'Scope: version_policy' \
  '目的: 版更新規則を検証する' \
  '内容: CMake versionを更新する' \
  '確認: self-testで検査する' > "$message_file"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" message "$message_file"
printf '%s\n' \
  'fix: 0.1.1 scope形式を検証する' \
  'Scope: VersionPolicy' \
  '目的: 版更新規則を検証する' \
  '内容: CMake versionを更新する' \
  '確認: self-testで検査する' \
  '影響: fixtureだけに限定する' > "$message_file"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" message "$message_file"
printf '%s\n' \
  'fix: 0.1.1 scope形式を検証する' \
  'Scope: version.policy' \
  '目的: 版更新規則を検証する' \
  '内容: CMake versionを更新する' \
  '確認: self-testで検査する' \
  '影響: fixtureだけに限定する' > "$message_file"
expect_failure env SIGNET_VERSION_POLICY_ROOT="$fixture" "$policy" message "$message_file"

echo "version-policy self-test: passed all 11 types, minor/patch/major authorization, duplicate/missing-bump/version-only, subject Japanese/scope, and body fixtures"
