---
name: signet-maintain-docs
description: Signetの正本文書、AGENTS.md、README、CONTRIBUTING、SECURITY、LICENSE、リリース案内、文書索引を現行のC++23・Qt 6・CGAL 6.2実装と運用へ同期する。文書の作成・更新・整理に使い、実装や公開操作は対応Skillへ委ねる。
---

# Signet Documentation Maintenance

## 正本を選ぶ

1. `git status --short` と `docs/INDEX.md` を確認し、無関係な文書差分を保護する。
2. AI行動規則は `AGENTS.md`、製品目的は `docs/project/overview.md`、技術判断は `docs/engineering/`、機能とUIは `docs/features/editor.md`、開発運用は開発文書へ置く。
3. 同じ判断を複数文書へ複製せず、未決定事項を確定仕様として書かない。仕様、設計、運用、履歴を分離する。
4. 現行コード、CMake、CTest、workflow、配布設定と照合できないコマンド、version、OS、成果物を書かない。
5. commit・version・公開の正本は`AGENTS.md`と`docs/development.md`に置き、同じ規則を必要なSkillと貢献・release案内へ同期する。現行workflowにないDraft自動化や未実装checkerを実装済みと書かない。

## 更新する

1. C++23、Qt 6、CGAL 6.2、CMake、CTest、clang-tidy、sanitizer、macOS arm64、AGPLの記述は確認済みの事実だけを反映する。
2. 幾何の正本、座標・許容誤差、保存schema、Undo/Redo、入力境界は該当する技術文書へ一度だけ記録し、実装と表示を同期する。
3. 外部送信、アカウント、同期、AI、対応OS、配布方式、ライセンスを依頼なしに追加・確定しない。
4. 相対リンクを使い、移動・追加・削除では `docs/INDEX.md` の導線を更新する。作業ログや一時調査を正本へ蓄積しない。
5. 正本化する規則から参照元固有語、特定の別製品の用語、未採用の配布方式名を除く。Signetの現行C++23・Qt 6・CGAL 6.2、CMake、CTest、macOS arm64の事実だけを書く。

## 検証する

1. `rg` で旧用語、旧パス、旧コマンド、参考製品名、リンク切れ候補を確認する。
2. 文書索引検査が存在する場合は現行CMake／スクリプトの手順で実行し、未追跡状態などの制約を報告する。
3. `git diff --check` と全差分を確認し、ローカル絶対パス、秘密、未確認の動作を含めない。
4. 更新した正本、照合した実装、検証、未確認事項を報告する。
5. 文書変更後は、15 Skillのfrontmatterと相対参照、Markdownリンク、旧曖昧規則、参照元固有語、秘密・ローカル絶対パス、`git diff --check`を確認する。既存の並行差分は結果から分けて報告する。
