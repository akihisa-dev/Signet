# 技術決定

## 決定

SignetはC++23、Qt 6.11、CGAL 6.2で実装する。ビルドにはCMakeとNinjaを使い、macOS 26のApple Silicon `arm64`のみを生成する。

## 根拠

CGALの2D Arrangementは、線分と円弧の交差から頂点、辺、面を構築できる。`Arrangement_with_history_2`は、分割後の辺と元曲線の対応を保持する。現行実装はArrangementの面を全operandの内外で分類し、閉じたprimitive任意組合せのN-operand Boolean結果を選択する。

Qtは、macOS `arm64`に対応するデスクトップUI、入力、ショートカット、アクセシビリティ、Undo/Redo、アプリバンドル生成を提供する。Qtの描画機能は表示に使い、幾何演算の正しさはCGAL側で管理する。

## 依存境界

- `src/core`: ライブラリ非依存の文書モデルと操作グラフ。
- `src/geometry`: CGAL型、精度方針、平面分割、集合演算。
- `src/ui`: Qtの画面、入力、表示。
- `tests`: 文書モデルと幾何演算の回帰検証。

CGAL型とQt型を保存形式の正本へ含めない。

## ライセンス

SignetはAGPL-3.0-or-laterを採用する。使用するCGALの2D ArrangementはGPL-3.0-or-later、Qtの対象モジュールはLGPL-3.0/GPL-3.0系であり、配布時は依存ライセンスとソース提供条件を確認する。
