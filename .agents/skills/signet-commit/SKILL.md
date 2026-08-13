---
name: signet-commit
description: Signetの変更を対象確認、検証、分割、明示パスのstage、日本語Conventional Commits形式のcommitまで安全に行う。stage-onlyとcommitを分け、version管理を併用し、push・tag・GitHub Release・PR・Issue・その他の外部公開は行わない。
---

# Signet Commit

## 前提条件

- 現在stageする対象は、直前に実作業と対象検証を完了した一目的だけに限定する。明確な変更・構築タスクでは、対象だけをstageしてローカルcommitし、hash・statusを確認してから次目的へ進む。長期間蓄積した差分、次目的の変更、目的境界が不明な差分がある場合は停止して再計画する。
- 複数目的を後からpatch-stageして事後分割しない。既存差分の目的境界、意図、競合が確認できない場合は対象を混ぜず、追加編集・stage・commitを停止して報告する。

## 手順

1. `git status --short`、unstaged・staged差分、AGENTS.md、version正本、対象テストを確認し、無関係な変更を保護する。
2. stage-onlyは指定差分の検証とstageだけとし、versionを変更せずstage後に停止する。明確な変更・構築タスクでは、対象検証後のversion判定・更新、対象だけのstage、ローカルcommit、hash・status確認まで行う。branch push、tag、Draft Release、Release公開、PR、Issue、その他の外部公開へ許可を広げない。
3. 一目的の実作業を対象検証した直後にversionを決定・更新し、対象だけを明示パスでstageしてcommitする。状態は `planned -> editing one purpose -> verified -> versioned -> staged -> committed -> post-commit checked -> next purpose` とし、typeは`feat`、`fix`、`docs`、`test`、`refactor`、`perf`、`build`、`ci`、`chore`、`style`、`revert`の11種類に固定する。`git add .`、`git add -A`、履歴書換え、force操作を使わない。
4. 影響は最大impactで判定する。MAJORはユーザー明示かつbreaking、`feat`はMINOR、それ以外の10 typeはPATCHとする。breakingだけを推測してMAJORにしない。
5. versionだけのcommit、同一versionの複数commit、対象変更なしのversion更新を禁止する。versionを更新するときは`CMakeLists.txt`の`project(VERSION ...)`を正本にし、対象変更、bundle・表示・checkerへの伝播を同じcommitへ含める。
6. 件名はscopeなしの`<type>[!]: <version> 日本語の説明`とする。本文には`Scope:`（英語小文字の名詞）、`目的:`、`内容:`、`確認:`、`影響:`を必ず含める。
7. commit前に対象CTest、必要なclang-tidy・sanitizer・CMake build、`git diff --cached --check`、`scripts/secret-guard.sh staged`、cached差分、秘密・生成物を確認する。実credential、token、password、秘密鍵、個人情報、非公開ファイルその他の秘密をcommit、diff、ログ、文書、fixtureへ含めず、値を出力・保存・引用しない。
8. 検証失敗、規則不明、競合、秘密情報、Gitの不適切な状態ではcommitせず停止する。commit後のpush、tag、Draft Release、Release公開、PR、Issue、その他の外部公開は、それぞれの明示許可がある場合だけ別Skillで扱う。
9. commit後にSHA、残存差分、未commitの無関係変更、未pushであることを確認し、stage-only・branch push・tag作成・tag push・Draft・Publishを混同しない。
