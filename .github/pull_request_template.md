## 変更結果

-

## 変更理由

-

## 検証

- [ ] `cmake --preset dev`
- [ ] `cmake --build --preset dev --parallel`
- [ ] `ctest --preset dev --output-on-failure`
- [ ] `scripts/verify.sh format`（またはclang-format未導入の理由を記載）
- [ ] `scripts/verify.sh lint`（またはclang-tidy未導入の理由を記載）
- [ ] 必要な場合はSanitizer構成でも確認した
- [ ] GitHub Actionsが成功した

## 確認事項

- [ ] 秘密情報、個人情報、非公開ファイルを含めていない
- [ ] 振る舞いを変えた場合は正本文書を更新した
- [ ] 幾何変更には境界条件の回帰テストを追加した
- [ ] QtまたはCGAL固有型を文書モデルへ漏らしていない
- [ ] 追加したコード、依存、素材のライセンスを確認した
