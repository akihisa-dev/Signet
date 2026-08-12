# Contributing to Signet

Signetは、幾何演算の正しさ、アクセシビリティ、文書、創作操作を改善するコントリビューションを歓迎します。

Pull Requestを作る前に、次を確認してください。

1. 解決する問題とユーザーに見える結果を説明する。
2. 幾何演算を決定的にし、接線、重複、ほぼ一致する入力をテストする。
3. `cmake --preset dev`、`cmake --build --preset dev`、`ctest --preset dev`を実行する。
4. 可能なら `scripts/verify.sh format` と `scripts/verify.sh lint` も実行する。clang-format／clang-tidyがない場合は導入せず、CI gateまたは未実施理由を記載する。
5. `scripts/verify.sh secret` と `scripts/verify.sh secret-self-test`を実行し、必要なら`staged`または`range`も確認する。秘密値は出力・保存・引用しない。
6. UI変更をキーボード操作とmacOSアクセシビリティに対応させる。
7. 明示的な製品判断なしに、テレメトリ、ホステッドサービス、独自ライセンス素材を追加しない。
8. Qt・CGAL固有型を文書モデルや保存形式へ露出させない。

commitを依頼または作成する場合は、[開発手順のcommit・version・公開規則](docs/development.md#変更commitversion公開)に従ってください。typeは11種類に固定し、件名へscopeを入れず、`<type>[!]: <version> 日本語の説明`とします。本文には`Scope:`（英語小文字の名詞）、`目的:`、`内容:`、`確認:`、`影響:`を必ず含めます。対象変更とversion更新は同じcommitへ含め、versionだけのcommitは作りません。

実credential、token、password、秘密鍵、個人情報、非公開ファイルその他の秘密をcommit、diff、ログ、文書、fixtureへ含めないでください。検査fixtureには架空値だけを使い、疑いがある値を出力・保存・引用しないでください。

Keep pull requests focused. By contributing, you agree that your work is provided under the repository license.
