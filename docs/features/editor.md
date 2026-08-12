# エディタ要件

## 基本フロー

1. 円、円弧、黄金長方形と派生図形を配置する。
2. 移動、拡大縮小、回転、複製、反転、対称配置を行う。
3. 図形を重ね、結合、交差、切り抜き、分割を行う。
4. 平面分割で生じた領域から残す領域を選ぶ。
5. 元図形を編集し、下流の構成結果を再評価する。

現行実装では、Canvasを主領域として評価スナップショットのCircle、Rectangle、GoldenRectangle、Arc（full-circleを含む）を表示し、NodeIdで選択してdragで移動できる。上部のコマンドバーは意味別glyphのicon-only図形配置群と編集群を分け、右側の`Shapes`ドックは初期幅300（最小240）の図形一覧、件数、診断件数、選択概要を提供する。Canvas内には評価集計を表示せず、形状と直接操作オーバーレイだけを表示する。hit判定はscreen-spaceの許容幅を使う。
操作中は一時状態を表示し、pointer releaseで`DocumentHistory`へ確定し、Escape、focus喪失、mouse ungrabでは中断して文書上の位置を変更しない。
middle buttonまたはSpace併用のpan、wheelによるanchor付きzoom、矢印キーによる移動を利用できる。
keyboard移動とUndo/RedoはHistoryを経由し、Shapes一覧とCanvasの選択を同期する。
選択中のCircleには中心、半径線、半径数値を表示し、preview・cancel・commit・Undoに追従する。
offscreen UIテストで選択、focus、座標変換、確定、中断、pan、zoom、keyboard操作、Shapes一覧同期、コマンドバーの排他モード、意味glyphのtooltip/accessibility、Shapesドックの実幅（280〜320、最小240）、ドック切替、ステータス表示とエラー後のtool hint復帰を確認している。狭幅表示ではShapesドックを明示的に閉じ、Canvas実サイズ720×520以上を確保できる。高DPIでは論理座標とcosmetic penを使い、Canvasの選択・preview・split・snap色はpaletteのHighlight/Link/Text/Midから派生する。暗色paletteのDPR2 renderが背景と描画を分離することもoffscreenで確認している。glyphの視覚的な分かりやすさ、高コントラスト完全保証、実GUIでの見た目は未確認である。アニメーションは追加しない。
Circle、Rectangle、Arc、Golden Rectangleの配置をpointer操作で確定できる。Splitは閉じた選択対象に軸を指定して確定し、評価された全cellを保持する。open Arcや不正な候補は確定しない。
Split cellは`RegionKey`で追跡され、領域を選択してDeleteすると、元のSplitを変更せずRegionSelectionとRegionFilterを一つのHistory操作として追加する。ノードのDuplicateとDeleteはSelectionから実行でき、参照中のノードは削除を拒否する。

## 補助操作

- グリッドとスナップ（純粋APIに加え、Canvasの移動・配置・リサイズへ接続済み。grid、document center、geometry候補、zoom換算した8 logical pxの許容距離、Option/Altによる一時無効化、snap guideを実装済み）
- 中心線、中心、半径の表示（選択中Circleの中心・半径線・数値を実装済み）
- 整列と等間隔配置（純粋APIは実装済み。UI接続、複数選択、toolbar/shortcut、Historyへのcommitは未実装・未決定）
- 回転角度の補助
- 黄金比に基づく寸法補助
- 左右・上下対称（`SymmetryNode` / `SymmetryAxis`による任意軸反射、二重反射を含む）
- Flip Horizontal / Vertical（選択boundsの中心軸による`SymmetryNode`、メニュー／toolbar、`Cmd+Option+H`／`Cmd+Option+V`、Undo/Redoを実装済み）

## データ上の要件

- 結果パスだけでなく元図形と操作関係を保持する。
- Booleanは閉じたCircle、Rectangle、GoldenRectangle、full-circleの任意組合せをprimitive葉へ展開してN-operand exact arrangement membershipで再評価できる。表示DTO roundtripは行わない。
- Undo/Redoで操作結果を再現でき、Undo後の分岐でもノードIDを再利用しない。
- 保存・再読込による再現は保存形式の仕様決定後に実装する。
- 不正値、空結果、接線、重複、ほぼ一致する境界を定義済みの結果または明示的なエラーとして扱う。

Booleanは結合、交差、切り抜き、排他的論理和を面分類で評価し、外周と穴を持つ閉じた境界DTOとして返す。
DTOのdouble値は表示用近似であり、面の`FaceId`と`RegionHit::face_id`は評価スナップショット内だけで有効である。
Booleanの評価結果は`DocumentEvaluationSnapshot.booleans`にNodeId付きで格納され、元primitiveのtransform変更とUndo/Redoの後に文書から再評価される。
交差、非交差、接線、包含、同一円、空結果、切り抜きの入力順、xor、境界閉鎖、hitをテストしている。
この評価は幾何モジュールと文書スナップショットまで実装済みだが、Canvasと`MainWindow`のBoolean操作・結果表示へは接続していない。
open ArcはBooleanとSplitのoperand非対応で、Boolean結果のSymmetryも非対応である。Boolean操作の配置方式、複数選択方式、alignment UI既定値、色、線、透明度、レイヤー、保存、書き出しは未実装または未決定である。`AI`メニューの`AIロゴ…`からmodeless生成ダイアログを開き、文章入力または任意のPNG/JPEG参照画像（両方も可）、明示同意、生成プレビュー、構成ノード一覧、stale revisionの拒否、明示的なApplyを提供する。文章と画像の両方がない場合は生成を開始しない。ApplyはPlanCompilerを介して編集可能なDAGを一つのUndo単位で追加する。provider接続、外部送信、生成成功の実動作はこのUIテストでは確認していない。
