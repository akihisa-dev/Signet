---
name: signet-change-editor-ui
description: SignetのQt 6エディタ画面、Canvas、ツール選択、pointer・keyboard操作、focus、drag、zoom、pan、grid・snap表示をモデルと整合させて変更する。実画面の起動確認は明示時だけ行い、幾何計算や保存契約は専用Skillへ委ねる。
---

# Signet Editor UI Change

## 境界を決める

1. 調査・レビューだけでは編集しない。変更時は対象画面、入力、期待状態、モデルへの副作用を先に書き出す。
2. `git status --short`、UI正本、対象Qt 6コード、モデル・コマンド、CTestを確認し、既存差分を保持する。
3. Canvasの座標、ヒット判定、表示閾値、変換はテスト可能なモデル／純粋処理へ置き、Widgetの見た目だけへ埋め込まない。

## 実装する

1. 画面状態、選択状態、一時drag状態、確定コマンド、永続化を分離する。画面ピクセルやQPainterの状態を正本にしない。
2. pointer確定と中断を分け、`pointerup` 相当の確定、cancel・capture喪失時の解除を明確にする。中断で保存、drop、選択確定を起こさない。
3. keyboard、focus移動、escapeによる中断、再操作、zoom・pan、狭い表示領域、高DPI、reduced motionを影響範囲で扱う。
4. grid、snap、中心線、整列、等間隔、角度補助、黄金比補助は指定された機能の範囲だけを表示し、補助線と確定形状を混同しない。
5. Qt 6のsignal、event、モデル通知の所有者を明確にし、不要な再描画、循環通知、破棄済みQObjectへの接続を避ける。

## 検証する

1. 座標・状態遷移はCTestまたは既存のC++テストで、通常確定、cancel、capture喪失、escape、focus変更、再操作を確認する。
2. 操作範囲に応じて狭幅、高DPI、キーボードのみ、zoom・pan、reduced motionの自動または手動確認を選ぶ。実画面確認は依頼された場合だけ行う。
3. `cmake --build`、対象CTest、必要なsanitizer、`git diff --check`を実行し、実画面を確認していない場合は未確認と報告する。
4. UI変更を理由に幾何契約、保存形式、外部送信、未承認の装飾やアニメーションを変更しない。
