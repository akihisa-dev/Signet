---
name: signet-manage-version
description: SignetのCMake project version、version header、配布設定、コミット種別、GitHub Releasesの整合を確認し、次のMAJOR・MINOR・PATCHを根拠付きで決めて更新する。version判断と検証に使い、commit、tag、Release公開は対応Skillへ渡す。
---

# Signet Version Management

## 次版を決める

1. `AGENTS.md`、開発・リリース正本、`CMakeLists.txt`、配布設定、既存のversion検査を確認する。Signetのversion正本は`CMakeLists.txt`の`project(VERSION ...)`であり、生成version headerはその値を表示へ伝播するconsumerとして確認する。
2. 製品世代を壊す変更は、ユーザーがMAJORを明示し、かつbreaking changeである場合だけ採用する。根拠をcommit本文とrelease記録へ残す。
3. `feat`はMINOR、それ以外の固定された10 type（`fix`、`docs`、`test`、`refactor`、`perf`、`build`、`ci`、`chore`、`style`、`revert`）はPATCHとする。複数の影響がある場合はMAJOR > MINOR > PATCHの最大impactを採用する。
4. version-only commit、同一versionの複数commit、対象変更なしのversion更新を作らない。独立目的はcommit単位へ分け、対象検証が成功した直後、commit直前にversionを決定・更新する。次目的の編集開始前に前目的のcommitを完了させる。stage-onlyではversionを更新せず、commit許可時にだけ判定・更新する。
5. 現在の未commit bootstrap状態は`0.1.0`を維持し、新規規則は次に明示許可されたcommitから適用する。各commitでversionを順次更新し、全作業後の事後分割を前提にしない。

## 更新して検証する

1. versionの正本と、CMake configure、app bundle、install、アプリ表示、checker、GitHub Release metadataの表示を同じ更新単位で同期する。SBOMはこのversion規則の対象外とする。
2. 機械検査の入口は`scripts/version-policy.sh`、`scripts/verify.sh version`、`scripts/verify.sh version-self-test`を前提とする。存在しない入口を実装済みと扱わず、書込み前に現行状態を確認する。
3. `MAJOR.MINOR.PATCH`、既存tag、配布成果物名、schema versionを確認し、保存形式の互換性を自動判定だけに任せない。
4. CMake configure/build、CTest、必要なbundle検査、`scripts/verify.sh secret`、`scripts/verify.sh secret-self-test`、`git diff --check`を実行する。数字の不一致は手計算で上書きせず正本を調べ直す。
5. 選択したtype、更新前後、最大impact、MAJOR明示の有無、検証、未実施のtag・push・公開をcommitまたはrelease担当へ渡す。
