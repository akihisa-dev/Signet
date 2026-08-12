---
name: signet-commit
description: Signetの変更を対象確認、検証、分割、明示パスのstage、日本語Conventional Commits形式のcommitまで安全に行う。stage-onlyとcommitを分け、version管理を併用し、push・tag・GitHub Releaseは行わない。
---

# Signet Commit

## 手順

1. `git status --short`、unstaged・staged差分、AGENTS.md、version正本、対象テストを確認し、無関係な変更を保護する。
2. stage-onlyは指定差分の検証とstageだけとし、versionを変更しない。commitは明示許可後のversion判定・更新とcommitまでとし、branch push、tag、Draft、Publishへ広げない。
3. 目的とtypeでcommit単位を決め、独立目的を分ける。typeは`feat`、`fix`、`docs`、`test`、`refactor`、`perf`、`build`、`ci`、`chore`、`style`、`revert`の11種類に固定し、`git add .`、`git add -A`、履歴書換え、force操作を使わない。
4. 影響は最大impactで判定する。MAJORはユーザー明示かつbreaking、`feat`はMINOR、それ以外の10 typeはPATCHとする。breakingだけを推測してMAJORにしない。
5. versionだけのcommit、同一versionの複数commit、対象変更なしのversion更新を禁止する。versionを更新するときは`CMakeLists.txt`の`project(VERSION ...)`を正本にし、対象変更、bundle・表示・checkerへの伝播を同じcommitへ含める。
6. 件名はscopeなしの`<type>[!]: <version> 日本語の説明`とする。本文には`Scope:`（英語小文字の名詞）、`目的:`、`内容:`、`確認:`、`影響:`を必ず含める。
7. commit前に対象CTest、必要なclang-tidy・sanitizer・CMake build、`git diff --cached --check`、`scripts/secret-guard.sh staged`、cached差分、秘密・生成物を確認する。実credential、token、password、秘密鍵、個人情報、非公開ファイルその他の秘密をcommit、diff、ログ、文書、fixtureへ含めず、値を出力・保存・引用しない。
8. 現在の未commit bootstrap状態は`0.1.0`を維持し、新規規則は次に明示許可されたcommitから適用する。最初のcommitを分割する場合は、各commitで`0.1.1`、`0.1.2`のように順次更新する。
9. commit後にSHA、残存差分、未commitの無関係変更、未pushであることを確認し、stage-only・branch push・tag作成・tag push・Draft・Publishを混同しない。
