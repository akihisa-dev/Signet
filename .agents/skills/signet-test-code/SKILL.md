---
name: signet-test-code
description: SignetのC++23テストとCTestについて、失敗の最小再現、回帰テスト、幾何境界、Qt状態、sanitizer、flaky原因、coverageの実害を診断・追加・修正する。製品実装の変更は対応Skillと併用し、GitHub上だけの失敗はCI調査へ渡す。
---

# Signet Code Test

## 失敗と境界を限定する

1. 調査・説明だけなら編集しない。テスト追加、修正、失敗解消が明示された場合だけ変更する。
2. `git status --short`、`docs/INDEX.md`、CMakePresets、CTest登録、対象C++、既存fixtureを確認する。
3. 失敗するコマンド、test名、入力、期待値、実際値を最小実行で再現し、製品不具合、古い期待値、環境、寿命、順序、共有状態へ分類する。

## 回帰テストを設計する

1. ユーザー結果、保存モデル、状態遷移、境界での拒否、副作用の有無を優先し、内部呼出回数や行数を目的にしない。
2. 幾何では接線、重複、ほぼ一致、空結果、自己交差、巨大・微小値、対称性、順序、往復保存を影響に応じて選ぶ。
3. Qt操作では確定、中断、focus、keyboard、zoom、pan、狭幅、高DPI、listenerの後始末を分ける。
4. fixtureはテスト専用の一時領域と架空値を使い、実ユーザーデータ、秘密、外部サービス、実ホームを使わない。
5. flaky testはtimeoutを増やして隠さず、時間、順序、乱数、共有状態、未解放資源を分離する。

## 段階的に検証する

1. 対象CTestを `ctest --test-dir <build> --output-on-failure -R <pattern>` で実行し、次に関連test、全CTestの順へ広げる。
2. CMake構成、sanitizer、clang-tidyに影響する変更は該当構成も実行し、失敗時に安全条件を弱めない。
3. coverageは重要分岐の未実行を説明し、数値だけを満たすtestや除外を追加しない。
4. `git diff --check`と全差分を確認し、生成物、debug log、一時データを残さない。
5. 再現条件、原因、変更test、製品コード、実行構成、未確認事項を分けて報告する。
