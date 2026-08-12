# 幾何モデル

## 正本

正本は、安定IDを持つ図形ノードと操作ノードの有向非巡回グラフである。評価済みパス、描画用ポリゴン、画面座標はキャッシュとして扱う。

文書モデルはCircle、Rectangle、GoldenRectangle、Arcをprimitiveとして扱い、変換、対称、Boolean演算、分割、領域選択は図形を直接破壊せず、入力ノードを参照する操作ノードとして保持する。EvaluatorはRectangleとGoldenRectangleを4本のSegmentInput、ArcをArcInput、±360度のArcをCircleInputとして表示用スナップショットへ変換する。
現行のArrangement入力は円、線分、3点円弧である。黄金長方形は評価時に長辺を短辺×数学的な黄金比から導出する。

## 平面分割と評価DTO

円、線分、3点円弧を履歴付きCGAL Arrangementへ挿入し、交点で分割された頂点、辺、面を構築する。
各分割要素と元曲線の対応を保持し、境界DTOの`source_curve_indices`へ入力曲線の履歴インデックスとして公開する。
境界DTOは円弧と線分を表現し、領域は`outer_boundary`と`holes`を持つ閉じたループとして返す。
DTOの座標と半径は`double`による表示用近似であり、CGALの幾何正本でも保存形式でもない。
`FaceId`は評価スナップショット内だけで有効な面識別子であり、永続性を持たない。
`RegionHit::face_id`も面を返す同じスナップショット内でだけ設定され、辺または頂点のhitには設定されない。
SplitのcellはCanvasで表示・hit対象になり、元曲線と生成chordのprovenanceから座標やFaceIdに依存しない`RegionKey`へ変換される。RegionSelectionは同じSplit snapshotへkeyを再解決し、解決不能なkeyを含む場合は部分評価せず診断を返す。

BooleanはCircle、Rectangle、GoldenRectangle、full-circleの閉じたprimitiveを任意個数・任意組合せで入力できる。各面について全operandの内外を分類し、`ArrangementModel::evaluateBoolean`がN-operandのexact arrangement membershipで選択された有界面だけを領域DTOとして返す。
結合、交差、差、排他的論理和を、交差、非交差、接線、包含、同一円、空結果、差の入力順、xor、境界閉鎖、hitの回帰テストで確認している。
深さ3、shared subtree、transform、Undo/Redo後の再評価を確認している。open Arc、Split、RegionSelectionはBoolean operandにできない。

文書からのBoolean評価は、`DocumentEvaluator`がBoolean入力グラフをprimitive葉へ展開し、N-operandのexact arrangement membershipで再評価して`DocumentEvaluationSnapshot.booleans`へ格納する。表示DTOを再入力するroundtripは行わない。
元primitiveのtransform変更とUndo/Redoは文書を再評価した結果へ反映される。

`SymmetryNode`は`SymmetryAxis`の任意軸に対する反射を表し、反射結果をさらに反射する二重反射も評価する。各primitiveとfull-circleに対応する。Boolean結果に対するSymmetryは未対応である。
CanvasのFlip Horizontal / Verticalは選択boundsの中心軸を`SymmetryAxis`としてこのノードを一つのHistory操作で追加する。Boolean結果のFlipは未対応である。

## Split / RegionSelection / RegionFilter

Splitは閉じたprimitive、Symmetry、またはBoolean結果を指定軸で分割する非破壊操作である。軸との交差が一意に確定しない場合は、tangent、vertex touch、nonintersection、boundary coincident、odd intersections、branch ambiguityなどのstatusを返し、UIはSplit nodeを追加しない。成功時は全material cellを保持し、境界provenanceから安定した`RegionKey`を生成する。

RegionSelectionはSplitの`RegionKey`を一つ以上保持する操作ノードであり、RegionFilterは選択cellをkeepまたはremoveする派生結果である。Canvasの領域DeleteはRegionSelectionとremove-modeのRegionFilterを一つのHistory entryとして追加し、入力Splitを変更しない。`DocumentHistory`は入力検証に失敗した場合も、これらのノードの一部だけを残さない。

ノード削除は`reject_if_referenced`と`cascade_dependents`の明示的なpolicyを持つ。UIは参照中ノードを拒否するpolicyを使い、モデルは依存関係を一括検証して成功時だけ一つのHistory entryへ確定する。

## Snap API

snapはUI状態を持たない純粋APIである。callerがgridのspacing・origin・threshold、candidateのthreshold、angleのincrement・thresholdを明示して呼び出す。入力を変更せず、範囲外や不正値は結果なしで返す。候補のtie-breakは距離、`identity`の小さい順、座標の辞書順で決定的にする。CanvasではこのAPIを移動、配置、リサイズへ接続し、grid、document center、geometry候補、zoom換算した8 logical pxの許容距離、Option/Altによる一時無効化、snap guideを実装している。追加のsnap UI設定は未決定である。

## Alignment / Distribution API

整列はUI状態を持たない純粋APIであり、callerが各項目のstable identityとdocument-coordinateのboundsを渡す。`align`はleft、horizontal center、right、top、vertical center、bottomの6種を受け、selection boundsまたは明示的なanchor coordinateを基準にする。結果は各identityに対するtranslation（`dx`、`dy`）だけで、入力のboundsやDocument/UIを変更しない。結果順はidentity順で、入力順に依存しない。

等間隔配置はhorizontalまたはverticalを指定し、軸方向の座標とidentity（同一座標時）の順で安定化して並べる。両端の項目は固定し、中間項目を等しいgapへ移動する。boundsが重なる場合もnegative gapを許容し、最小3項目未満、不正なbounds、重複identity、無効な軸は結果なしで返す。

## 数値方針

- 保存値とUI入力値の単位、丸め、範囲は保存形式の設計時に固定する。
- 幾何判定と構成にはCGALのexact predicates/exact constructionsを用いる。
- 表示の近似精度はズームに応じて変えてよいが、表示近似を幾何正本へ戻さない。
- 非有限値、非正半径、循環参照を入力境界で拒否する。

## 必須回帰ケース

- 交差する円
- 外接・内接する円
- 同一円
- ほぼ一致する境界
- 包含、穴、空結果
- 巨大・微小座標
- 入力順序を変えた結果の安定性

保存形式と書き出し形式は未決定であり、Arrangementの内部型を保存データへ漏らさない。
