---
name: signet-change-ci
description: SignetのGitHub Actions、CMake configure/build、CTest、clang-tidy、sanitizer、macOS arm64検証、権限、concurrency、秘密情報検査を安全に追加・修正・調査する。Actionのversionだけは依存更新、GitHub上の失敗はCIデバッグ、タグとReleasesはリリースSkillへ渡す。
---

# Signet CI Change

## 境界を限定する

1. 調査・check確認だけでは編集しない。workflow、検査、hook、所有規則の変更が明示された場合だけ変更する。
2. `git status --short`、`.github/workflows/`、CMake設定、CTest、`SECURITY.md`、`CONTRIBUTING.md`を確認する。
3. trigger、権限、concurrency、runner、toolchain、cache、検査script、secret guardを分けて扱う。
4. workflow上の失敗はrun、job、step、logの順に一次情報を確認し、通信失敗を認証失敗と断定しない。

## 安全なworkflowを作る

1. `permissions`を明示し、readを基本とする。writeは必要なjobとresourceへ限定し、理由を記録する。
2. forkの未信頼コードとwrite token、`pull_request_target`、shellへの未検証入力展開を安全設計なしに組み合わせない。
3. CMake configure、ビルド、CTest、clang-tidy、sanitizerを独立した失敗単位にし、macOS arm64のrunner・toolchain条件を明示する。
4. cache key、同時実行、checkoutのcredential保持、外部Actionの固定versionを現行要件と一次資料へ照合する。
5. 自動push、秘密追加、repository設定変更、GitHub Release公開をworkflow変更の許可へ拡大しない。

## 検証する

1. YAML構文とworkflow参照先を検査し、実在するCMake preset、target、CTest、clang-tidy設定、sanitizer構成だけを呼ぶ。
2. 対象のCMake buildとCTest、必要なclang-tidy・sanitizerを実行し、remote runnerだけの結果は未確認とする。
3. Secret Guardを変更した場合は`scripts/verify.sh secret-self-test`、`scripts/verify.sh secret`、必要なrange検査を架空fixtureで確認し、実credentialを置かない。
4. `git diff --check`と差分を確認し、不要なwrite権限、秘密、内部URL、ローカル絶対パスがないことを確かめる。
