# Release checklist

このファイルはチェック済み状態の記録ではなく、リリースごとにコピーして使用するテンプレートです。tag作成、tag push、Draft Release作成、Release公開は個別の明示許可を必要とします。現行workflowにはDraft Release作成の自動化はありません。

## 対象

- [ ] リリース対象のcommitと`MAJOR.MINOR.PATCH`を固定した
- [ ] versionが`CMakeLists.txt`の`project(VERSION ...)`と一致し、bundle・表示・checkerへ伝播している
- [ ] commitのtype、impact、件名、本文（`Scope:`、`目的:`、`内容:`、`確認:`、`影響:`）を確認した
- [ ] worktreeとindexに対象外の変更がなく、commit済みである
- [ ] 保存形式またはschemaの互換性への影響を確認した
- [ ] 実credential、token、password、秘密鍵、個人情報、非公開ファイルその他の秘密が差分・成果物・説明にないことを確認した
- [ ] `scripts/verify.sh secret`、`scripts/verify.sh secret-self-test`、対象range検査が成功した

## 検証

- [ ] development構成のconfigure、build、CTestが成功した
- [ ] Sanitizer構成のconfigure、build、CTestが成功した
- [ ] release構成からmacOS arm64のapp bundleを生成した
- [ ] bundle内の実行ファイルがarm64であることを確認した
- [ ] Qt runtime、ライセンス通知、バージョン表示を確認した
- [ ] manifestのcomponent rowsとMach-O path inventoryを別々に確認し、bundle filesのpath setが一致した
- [ ] 署名と公証の実施状況をリリース説明へ正確に記載した

## 公開

- [ ] tag名が`MAJOR.MINOR.PATCH`（`v`なし）で、アプリのversionと一致する
- [ ] tagが未使用で、対象commitがlocal・remoteから到達可能である
- [ ] 配布物のチェックサムを作成した
- [ ] tagを作成した（tag pushとは別に確認した）
- [ ] tagをpushした（Draft Release作成とは別に確認した）
- [ ] Draft Releaseの本文と配布物を確認した（自動化なしの現行手順）
- [ ] Release公開を個別に確認した
- [ ] 公開済みassetを上書きしていない。差し替えが必要な場合は新しいPATCHである
- [ ] 公開前に未決定機能や未検証事項を完了済みと記載していない
