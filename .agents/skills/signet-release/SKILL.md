---
name: signet-release
description: Signetのリリース準備、version確認、CMake配布ビルド、CTest、macOS arm64 bundle、checksum、Git tag、GitHub ReleasesのDraft・Publishを現行手順に従って安全に進める。各操作の明示許可を分け、通常のbranch公開はpublish-githubへ渡す。
---

# Signet Release

## 範囲を分ける

1. 準備確認、local build、tag作成、branch push、tag push、Draft作成、Publishを別操作として扱い、一つの許可を後続操作へ広げない。
2. version正本、CMake preset、CTest、配布設定、workflow、release checklistを実行時の現行HEADから確認する。version正本は`CMakeLists.txt`の`project(VERSION ...)`であり、bundle・表示・checkerと一致させる。
3. clean worktree、`MAJOR.MINOR.PATCH`、対象commit、macOS arm64成果物、license notice、checksum、必要な検証の完了を確認する。

## 検証と公開

1. CMake configure/build、CTest、必要なclang-tidy・sanitizer、macOS arm64 bundle検査、`scripts/verify.sh secret`、`scripts/verify.sh secret-self-test`、version検査を行い、未実施や失敗があればtagを作らない。Actionsの成功はlocal release verifyの代替ではない。
2. tag作成は明示時だけ、`MAJOR.MINOR.PATCH`（`v`なし）でversionと完全一致する未使用tagを対象commitへ作る。commit済み、clean worktree、local・remote reachabilityを確認し、既存tagを移動・削除・上書きしない。
3. branch push、tag push、Draft Release作成、Publishを個別に確認する。Draft workflowが現行にない場合は自動化済みと表現せず、[Release checklist](../../../.github/RELEASE_CHECKLIST.md)をコピーして手順を記録する。
4. GitHub Releasesの公開済みassetを無断で差し替え・上書きしない。差し替えが必要な場合は新しいPATCHを作成する。
5. 署名、公証、自動更新、ストア配布、認証情報の利用は明示範囲だけ扱い、未署名・未公証を済みと表現しない。秘密の値を出力・保存・引用しない。
6. version、local・remote branch/tag、成果物、checksum、検査、Draft・Publish状態、未実施操作、統合事項を報告する。
