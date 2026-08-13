---
name: signet-manage-version
description: SignetのCMake project version、version header、配布設定、コミット種別、GitHub Releasesの整合を確認し、次のMAJOR・MINOR・PATCHを根拠付きで決めて更新する。対象変更とversion更新を同じcommitへ整合させ、stage-onlyではversionを変更しない。commit・push・tag・Release・PR・Issueは各操作の明示許可と対応Skillへ渡す。
---

# Signet Version Management

## 次版を決める

1. `AGENTS.md`、開発・リリース正本、`CMakeLists.txt`、配布設定、既存のversion検査を確認する。Signetのversion正本は`CMakeLists.txt`の`project(VERSION ...)`であり、生成version headerはその値を表示へ伝播するconsumerとして確認する。
2. 製品世代を壊す変更は、ユーザーがMAJORを明示し、かつbreaking changeである場合だけ採用する。根拠をcommit本文とrelease記録へ残す。
3. `feat`はMINOR、それ以外の固定された10 type（`fix`、`docs`、`test`、`refactor`、`perf`、`build`、`ci`、`chore`、`style`、`revert`）はPATCHとする。複数の影響がある場合はMAJOR > MINOR > PATCHの最大impactを採用する。
4. version-only commit、同一versionの複数commit、対象変更なしのversion更新を作らない。明示的にcommitが許可された変更・構築タスクでは、一目的の対象検証直後に最新のstatus・diff・indexを確認し、versionを決定・更新して対象変更と同じcommitへ含め、commit後にhash・statusを確認してから次目的へ進む。commit禁止・未コミット・監査のみ・計画のみ、stage-only、検証失敗、規則不明、競合、秘密情報、Gitの不適切な状態ではversionを更新またはcommitせず停止する。stage-onlyではversionを変更せず、複数目的を蓄積して後から分割しない。
5. version更新後のcommitを越えてpush、tag、Release、PR、Issue、その他の外部公開を行わず、それぞれの明示許可と対応Skillへ渡す。

## 更新して検証する

1. versionの正本と、CMake configure、app bundle、install、アプリ表示、checker、GitHub Release metadataの表示を同じ更新単位で同期する。SBOMはこのversion規則の対象外とする。
2. 機械検査の入口は`scripts/version-policy.sh`、`scripts/verify.sh version`、`scripts/verify.sh version-self-test`を前提とする。存在しない入口を実装済みと扱わず、書込み前に現行状態を確認する。
3. `MAJOR.MINOR.PATCH`、既存tag、配布成果物名、schema versionを確認し、保存形式の互換性を自動判定だけに任せない。
4. CMake configure/build、CTest、必要なbundle検査、`scripts/verify.sh secret`、`scripts/verify.sh secret-self-test`、`git diff --check`を実行する。数字の不一致は手計算で上書きせず正本を調べ直す。
5. 選択したtype、更新前後、最大impact、MAJOR明示の有無、検証、未実施のtag・push・公開をcommitまたはrelease担当へ渡す。
