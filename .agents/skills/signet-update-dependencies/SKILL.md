---
name: signet-update-dependencies
description: SignetのC++23・Qt 6・CGAL 6.2を含むCMake依存、toolchain、外部Action参照を候補確認から小さく更新し、API互換性、ライセンス、ビルド、CTest、clang-tidy、sanitizer、macOS arm64配布を整合させる。設計変更やRelease公開は別Skillへ渡す。
---

# Signet Dependency Update

## 現状と候補を確認する

1. 相談・outdated確認だけでは変更しない。追加、削除、version更新、脆弱性対応が明示された場合だけ編集する。
2. `git status --short`、CMakeLists、CMakePresets、toolchain、lock・固定情報、依存notice、license、SBOM相当の現行資源を確認する。
3. Qt、CGAL、CMake module、テスト・解析ツール、間接依存、GitHub Actionsを分ける。Qt 6とCGAL 6.2の指定を無断で別majorへ変えない。
4. 公式release notes、migration guide、ライセンス、現行APIの一次情報を確認し、最新版という理由だけで採用しない。

## 小さく更新する

1. 依存を小さな互換単位で更新し、manifest・toolchain・固定情報の差分を都度確認する。一括更新を既定にしない。
2. API、型、CMake設定、幾何結果、bundle内容が変わる場合は互換対応と回帰テストを同じ単位に含める。
3. AGPL-3.0-or-laterと互換しない依存、未検証のバイナリ、外部送信を追加しない。自動更新botや強制overrideを勝手に導入しない。
4. 検証が壊れたら今回の依存差分だけを原因分析し、既存変更を巻き戻さない。

## 検証する

1. ライセンス・notice・SBOM相当の一覧を更新し、未知ライセンスは公式情報で確認する。
2. CMake configure/build、CTest、clang-tidy、必要なsanitizer、macOS arm64 bundle検査を影響に応じて実行する。
3. Action参照変更はworkflow検査を追加し、network・remote runnerだけで証明される事項は未確認と報告する。
4. 全差分、`git diff --check`、生成物、秘密、内部URL、ローカル絶対パスを確認する。
