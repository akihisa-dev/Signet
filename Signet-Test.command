#!/bin/sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir" && pwd)
cd "$repo_root" || exit 1

echo "Signetの開発起動を開始します。"
echo "リポジトリ: $repo_root"

status=0
cmake --preset dev || status=$?
if [ "$status" -eq 0 ]; then
  cmake --build --preset dev --parallel || status=$?
fi
if [ "$status" -ne 0 ]; then
  echo "開発起動用のビルドに失敗しました（終了コード: ${status}）。" >&2
  exit "$status"
fi

app_bundle="$repo_root/build/dev/Signet.app"
app_executable="$app_bundle/Contents/MacOS/Signet"

if [ ! -d "$app_bundle" ] || [ ! -x "$app_executable" ]; then
  echo "開発起動用のSignet.appが生成されませんでした。" >&2
  exit 1
fi

echo "Signetを起動します。アプリを終了すると、このターミナルも終了します。"
"$app_executable"
status=$?
if [ "$status" -eq 0 ]; then
  echo "Signetを終了しました。"
  exit 0
fi

echo "Signetの起動または実行に失敗しました（終了コード: ${status}）。" >&2
exit "$status"
