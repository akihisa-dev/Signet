#!/bin/sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir" && pwd)
cd "$repo_root" || exit 1

echo "Signetのビルドを開始します。"
echo "リポジトリ: $repo_root"

status=0
cmake --preset dev || status=$?
if [ "$status" -eq 0 ]; then
  cmake --build --preset dev --parallel || status=$?
fi

if [ "$status" -eq 0 ]; then
  echo "ビルドが完了しました。"
else
  echo "ビルドに失敗しました（終了コード: ${status}）。" >&2
fi

exit "$status"
