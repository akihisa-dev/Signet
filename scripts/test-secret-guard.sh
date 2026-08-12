#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
guard="$script_dir/secret-guard.sh"
temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/signet-secret-guard-test.XXXXXX")
trap 'rm -rf "$temporary_root"' EXIT HUP INT TERM

fail() {
  echo "secret-guard self-test: $*" >&2
  exit 1
}

new_fixture() {
  fixture=$(mktemp -d "$temporary_root/repo.XXXXXX")
  cd "$fixture"
  git init -q
  git config user.name "Signet secret guard test"
  git config user.email "secret-guard@example.invalid"
  printf '%s\n' 'fixture baseline' > README.md
  git add README.md
  git commit -q -m 'fixture baseline'
  base=$(git rev-parse HEAD)
}

write_worktree() {
  printf '%s\n' "$1" > fixture.txt
}

expect_failure() {
  if "$guard" "$@" >/dev/null 2>&1; then
    fail "expected rejection"
  fi
}

expect_success() {
  "$guard" "$@" >/dev/null 2>&1 || fail "expected acceptance"
}

# All secret-shaped values are assembled only in this temporary fixture.
github_prefix=ghp
github_token="${github_prefix}_AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDD"
openai_prefix=sk
openai_token="${openai_prefix}-proj-AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDD"
aws_prefix=AKIA
aws_access_key="${aws_prefix}ABCDEFGHIJKLMNOP"
private_key_name=PRIVATE
private_key_header=$(printf '%s%s%s%s%s' '-----' 'BEGIN ' 'OPENSSH ' "$private_key_name" ' KEY-----')
password_name=password
password_value=LongTemporaryValue123
path_root=/
local_path="${path_root}Users/example/private-document.txt"
environment_name=OPENAI_API_KEY

new_fixture
write_worktree "$github_token"
expect_failure worktree

write_worktree "$openai_token"
expect_failure worktree

write_worktree "$aws_access_key"
expect_failure worktree

write_worktree "$private_key_header"
expect_failure worktree

write_worktree "${password_name}=${password_value}"
expect_failure worktree

write_worktree "${environment_name}=${openai_token}"
expect_failure worktree

write_worktree "$local_path"
expect_failure worktree

# Placeholder values and references are intentionally accepted, while binary,
# build, license, and checksum paths are not scanned.
write_worktree 'password=<placeholder>'
expect_success worktree
mkdir -p build/generated licenses/runtime
printf '%s\n' "$github_token" > build/generated/secret.txt
printf '%s\n' "$openai_token" > licenses/runtime/license.txt
printf '%s\n' "$aws_access_key" > checksums.sha256
printf 'binary\000%s\n' "$private_key_header" > binary.dat
expect_success worktree

new_fixture
printf '%s\n' "$github_token" > staged.txt
git add staged.txt
expect_failure staged
git reset -q -- staged.txt
printf '%s\n' 'staged safe content' > staged.txt
git add staged.txt
expect_success staged

new_fixture
range_base=$base
printf '%s\n' "$openai_token" > range.txt
git add range.txt
git commit -q -m 'fixture secret-shaped change'
expect_failure range "$range_base" HEAD

new_fixture
range_base=$base
printf '%s\n' 'range safe content' > range.txt
git add range.txt
git commit -q -m 'fixture safe change'
expect_success range "$range_base" HEAD

new_fixture
range_base=$base
mkdir -p build/generated licenses/runtime
printf '%s\n' "$github_token" > build/generated/range-secret.txt
printf '%s\n' "$openai_token" > licenses/runtime/range-secret.txt
printf '%s\n' "$aws_access_key" > checksums.sha256
git add -f build/generated/range-secret.txt licenses/runtime/range-secret.txt checksums.sha256
git commit -q -m 'fixture excluded generated change'
expect_success range "$range_base" HEAD

echo "secret-guard self-test: passed token, key, password, path, staged/range/worktree, placeholder, binary, build, license, and checksum fixtures"
