# エディタ要件

## 基本フロー

1. 円、円弧、黄金長方形と派生図形を配置する。
2. 移動、拡大縮小、回転、複製、反転、対称配置を行う。
3. 図形を重ね、結合、交差、切り抜き、分割を行う。
4. 平面分割で生じた領域から残す領域を選ぶ。
5. 元図形を編集し、下流の構成結果を再評価する。

現行実装では、Canvasは評価スナップショットを入力としてCircle、Rectangle、GoldenRectangle、Arc（full-circleを含む）を表示し、NodeIdで選択してdragで移動できる。hit判定はscreen-spaceの許容幅を使う。
操作中は一時状態を表示し、pointer releaseで`DocumentHistory`へ確定し、Escape、focus喪失、mouse ungrabでは中断して文書上の位置を変更しない。
middle buttonまたはSpace併用のpan、wheelによるanchor付きzoom、矢印キーによる移動を利用できる。
keyboard移動とUndo/RedoはHistoryを経由し、Objects一覧とCanvasの選択を同期する。
選択中のCircleには中心、半径線、半径数値を表示し、preview・cancel・commit・Undoに追従する。
offscreen UIテストで選択、focus、座標変換、確定、中断、pan、zoom、keyboard操作、Objects一覧同期を確認している。
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
open ArcはBooleanとSplitのoperand非対応で、Boolean結果のSymmetryも非対応である。Boolean操作の配置方式、複数選択方式、alignment UI既定値、色、線、透明度、レイヤー、保存、書き出し、AI機能は未実装または未決定である。
