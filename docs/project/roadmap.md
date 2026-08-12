# 実装ロードマップ

このロードマップは、幾何演算の正しさを先に固定し、その上へ編集操作と製品機能を積み上げる順序を定める。
書き出し形式、視覚スタイルなどの未決定事項は、該当仕様が決まるまで実装対象へ含めない。

## 基盤整備

状態：完了。

- C++23、Qt 6.11、CGAL 6.2、CMake、NinjaによるmacOS arm64専用構成。
- 通常ビルドとAddressSanitizer、UndefinedBehaviorSanitizerの検証構成。
- AGPL-3.0-or-laterのライセンスと、コントリビューション、行動規範、脆弱性報告の文書。
- macOS 26 arm64 runnerで通常構成とSanitizer構成を検証するGitHub Actions。
- Signet向けにローカライズしたエージェント規則とSkill群。

## 幾何カーネル

状態：限定範囲を実装済み。

履歴付き平面分割へ円、線分、3点円弧を挿入し、交点で分割された頂点、辺、面と元曲線の対応を保持する。
円弧と線分の境界DTO、外周、穴、元曲線の履歴インデックス、評価スナップショット内だけで有効な`FaceId`を返す。
交差円、接円、交差線分、円弧で閉じた領域、無効入力を自動テストしている。

閉じたCircle、Rectangle、GoldenRectangle、full-circleの任意組合せについて、`ArrangementModel::evaluateBoolean`がN-operand exact arrangement membershipから結合、交差、差、排他的論理和を構成できる。
交差、非交差、接線、包含、同一円、空結果、差の入力順、xor、境界閉鎖、hitをテスト済みである。
Boolean入力グラフはprimitive葉へ展開し、深さ3、shared subtree、transform、Undo/Redoの再評価を確認している。open Arc、Split、RegionSelectionはBoolean operand非対応である。

同一円、穴、巨大座標、微小座標を含む回帰テストを実施済みであり、面集合と元曲線履歴を確認している。

完了条件は、必須回帰ケースで位相と元曲線履歴が再現可能であり、表示用近似を判定へ使用していないことである。

## 文書モデルと編集履歴

状態：実装済み（保存を除く）。

安定IDを持つCircle、Rectangle、GoldenRectangle、Arc、Boolean演算のノードモデルと文書内schema versionを実装している。これは保存形式の実装を意味しない。
現時点では、既存ノードだけを演算入力にできるため、新しい演算から過去のノードへ向かう非巡回構造になる。
図形追加、Booleanノード追加、変換にはUndoとRedoを適用でき、Undo後に編集を分岐しても取り消したノードのIDを再利用しない。

対称、分割、領域選択も操作ノードとして同じ非破壊モデルへ接続している。
UndoとRedoを操作ノードへ適用し、Undo後の分岐でも取り消したノードIDを再利用しない。
RegionSelectionとRegionFilterの組合せは、入力検証後に2ノードを一つの履歴操作として追加する。ノード削除は参照検査または依存カスケードを一括で検証し、成功時だけ一つの履歴操作として確定する。
保存形式は未実装であり、入力上限、破損データの拒否、schema migration、決定的な出力順序は仕様決定後に追加する。

完了条件は、すべての中核操作を破壊せずに再編集でき、UndoとRedo後もノード参照と評価結果が一致することである。

## エディタ操作

状態：文書履歴と全primitiveのCanvas操作を実装済み。

`DocumentEvaluator`は文書からNodeId付きの全primitiveスナップショットを生成し、Boolean入力をprimitive葉へ展開してN-operand exact arrangement membershipで再評価し、`DocumentEvaluationSnapshot.booleans`へ格納する。表示DTO roundtripは行わない。`SymmetryNode` / `SymmetryAxis`による任意軸反射は各primitiveとfull-circle、二重反射に対応するが、Boolean結果のSymmetryは未対応である。
元primitiveのtransform変更とUndo/Redoは再評価結果へ反映される。
`MainWindow`は`DocumentHistory`を所有し、CanvasとObjects一覧の選択を同期する。
Canvasではsnapshotを入力に全primitiveを描画し、screen-space hitとNodeIdによる選択、drag中の一時プレビュー、pointer releaseでの確定、Escape・focus喪失・mouse ungrabによる中断、middle buttonまたはSpace併用のpan、wheel zoom、History経由のkeyboard移動を扱う。
選択中のCircleの中心、半径線、半径数値表示はpreview、cancel、commit、Undoに追従する。
Undo/RedoはMainWindowの編集アクションからHistoryへ接続している。
offscreen UIテストで座標変換、選択、確定、中断、pan、zoom、focus、keyboard、Objects一覧同期を確認している。
Circle、Rectangle、Arc、Golden Rectangleのpointer配置、Splitの軸プレビューと確定、Split cellのRegionKeyによる選択、region delete、Duplicate、参照中ノードの削除拒否も実装済みである。

Canvasのsnapは移動、配置、リサイズへ接続済みで、grid、document center、geometry候補、zoom換算した8 logical pxの許容距離、Option/Altによる一時無効化、snap guideを扱う。Flip Horizontal / Verticalは選択boundsの中心軸による`SymmetryNode`として一つのHistory操作へ確定し、メニュー／toolbar、`Cmd+Option+H`／`Cmd+Option+V`、Undo/Redoに対応する。

Booleanの評価は幾何モジュールと文書スナップショットまで実装済みだが、Canvasと`MainWindow`のBoolean操作・結果表示へは接続していない。
open ArcはBooleanとSplitのoperandにできず、Boolean結果のSymmetryは未対応である。Boolean操作の配置方式と複数選択方式、alignment UI既定値は未決定である。

保存、出力、色、線、透明度は未実装または未決定である。snapの追加UI設定とalignment UIの既定値は未決定である。

整列6種とhorizontal/verticalの等間隔配置は、stable identityとboundsをcallerから受け取り、selection boundsまたは明示的なanchorを基準にtranslationだけを返す純粋APIとして実装済みである。distributionは外側を固定し、identityでtie-breakし、negative gapを許容し、3項目未満を拒否する。UI接続、複数選択の操作契約、toolbar/shortcut、Historyへのcommitは未実装・未決定である。

完了条件は、pointer操作の確定と中断、keyboard操作、focus、zoom、pan、高DPIで同じ文書結果を得られ、操作中断が文書を変更しないことである。

## AIロゴ生成MVP

状態：限定範囲を実装済み（実provider接続と実GUI操作は未確認）。

`AI`メニューの`AIロゴ…`からmodelessダイアログを開き、自然言語のプロンプトまたは任意のPNG/JPEG参照画像（両方も可）を受け取る。両方がない場合は拒否する。外部送信前に明示的な同意を求め、同意がない場合は生成を開始しない。ユーザーがインストールしてログインしたCodex CLIを任意providerとして使い、Signetは認証情報を読み取らない。providerの失敗や未設定はエディタの編集を妨げない。

Codex CLI呼び出しは非同期で、read-only sandboxとephemeral実行を指定し、入力と一時ファイルはリクエスト終了時に削除する。生成結果は`LogoConstructionPlan`として厳格に検証し、コピーした文書でプレビューする。文書revisionが変わった結果は適用せず、明示的なApplyだけがDAGへ一つのUndo単位で追加する。プロンプトと画像はDocument、設定、ログへ保存しない。

providerの実ネットワーク送信、Codex CLIの実環境、実GUIでの生成成功は未確認である。配布・サブスクリプション条件と正式なAI結果の永続化形式は未決定である。

## 製品化

状態：配布bundleのinstall検証を実装済み（署名・公証を除く）。

保存形式と書き出し形式が決定した後に、ファイル操作、破損入力への防御、互換移行を実装する。
色、線、透明度、レイヤーの仕様が決定した後に、描画と編集UIを実装する。

配布段階では、Release構成からQt runtime、platform plugin、CGAL経由のGMP/MPFRなどの非system dylibをQt 6.11の公式deploy APIで同梱し、Mach-O依存、arm64、Info.plist、ライセンス通知をinstall後に検証する。現行のruntime inventoryはMach-O pathsを記録し、manifestの全pathがbundleと完全一致し、runtime licenseとchecksumを検査済みである。Homebrew、workspace、build dependencyへのabsolute依存とRPATHは0件で、qcocoa、CMake正本のproject version、minOS 26を確認している。QtPdf、QtSvg、QtVirtualKeyboardのpluginは同梱していない。Qt official notice、SBOM、runtime inventory、適用されるライセンス通知はbundleへ同梱している。現行のinstall成果物は未署名・未公証である。
Qt Core/GUI/WidgetsはLGPL-3.0-onlyを選択し、改変なしGNU LGPL v3本文、SHA-256、GNU URL、Qt公式licensing URLをQt noticeへ記録する。Qt-GPL exceptionはこのruntimeでは選択しない。SBOMはinventory evidenceであり、モジュール固有通知を含む法的完全性を断定するものではない。
GitHub Releaseの公開は、ローカル実装やCI整備とは別の明示操作として扱う。
