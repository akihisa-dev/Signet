---
name: signet-debug-packaging
description: SignetのCMake・CPackまたは現行配布設定によるmacOS arm64アプリ、bundle、resources、署名・公証境界、配布成果物の構成を診断・修正・再検証する。GitHub Release操作はrelease、workflow失敗はchange-ciへ渡す。
---

# Signet Packaging Debug

## 診断範囲を決める

1. 調査・原因説明だけでは成果物を作り直したり削除したりしない。修正またはビルド実行が明示された場合だけ書き込む。
2. `git status --short`、CMakeLists、CMakePresets、CPack設定、macOS bundle設定、workflow、期待する成果物を確認する。
3. configure、compile、test、install、bundle、resource、署名・公証、checksum、archiveを別段階として切り分ける。
4. 最初の失敗と直前の生成物を確認し、ログ末尾だけで原因を断定しない。既存成果物の成功を現行buildの成功とみなさない。

## 成果物を確認する

1. macOS arm64のapp bundle、実行ファイル、Info.plist、Qt runtime、CGAL関連resource、license notice、versionを確認する。
2. source、test、設定、debug symbol、不要な依存が配布対象へ混入していないことを、現行の成果物契約に照合する。
3. 署名、公証、installer、自動更新、ストア配布は明示された範囲だけ扱い、未署名を署名済みと表現しない。

## 修正と再検証

1. 欠落を隠すためにinstall rule、ignore、check、必須resourceを緩めない。定義と検査を同じ変更単位で更新する。
2. `cmake --build <build> --target install`、現行のbundle検査、CTestを実行し、内容と終了codeを確認する。
3. 配布説明、LICENSE、依存notice、versionを変更したら正本文書とrelease案内を同期する。
4. `git diff --check`で生成物、一時ログ、ローカル絶対パスを変更へ含めないことを確認し、原因、成果物、未確認事項を報告する。
