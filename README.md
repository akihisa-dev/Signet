# Signet

Signetは、円、円弧、黄金長方形などの幾何学図形を組み合わせ、家紋、印章、紋章、宗教記号、魔術記号、組織ロゴを制作するmacOS向けOSSデザインアプリです。

Signet is an open-source macOS design application for constructing seals, crests, insignia, and fictional symbols from precise, reusable geometry.

## 技術構成 / Technology

- C++23
- Qt 6.11
- CGAL 6.2
- CMake + Ninja
- macOS 26、Apple Silicon `arm64`

編集データの正本は、元図形と操作を保持する非破壊の依存グラフです。Qtの描画結果やCGAL内部型は保存形式の正本にしません。

The canonical document is a non-destructive dependency graph of source geometry and operations. Rendered pixels and library-specific geometry objects are derived data.

## 開発 / Development

```sh
brew install cmake ninja qt cgal
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

構成ごとの検証をまとめて実行する入口は次のとおりです。

```sh
scripts/verify.sh dev
scripts/verify.sh sanitize
scripts/verify.sh format
scripts/verify.sh lint
scripts/verify.sh bundle
```

実行ファイルは`build/dev/Signet.app`へ生成されます。

Release bundleを一時stageへinstallし、依存と同梱ライセンスを検査するには次を実行します。

```sh
cmake --preset release
cmake --build --preset release
stage="$(mktemp -d)"
cmake --install build/release --prefix "$stage"
expected_version="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p' CMakeLists.txt)"
cmake -DSIGNET_BUNDLE="$stage/Signet.app" \
  -DSIGNET_EXPECTED_VERSION="$expected_version" \
  -DSIGNET_SOURCE_DIR="$PWD" \
  -DSIGNET_BUILD_DIR="$PWD/build/release" \
  -P cmake/verify_macos_bundle.cmake
```

Release installとverifyは成功しています。検査済みbundleではruntime manifestの全pathがbundleのpathと完全一致し、全Mach-Oがarm64です。Homebrew、workspace、build dependencyへのabsolute依存とRPATHは0件で、qcocoa、CMake正本のproject version、minimum OS 26を確認しています。runtime licenseとchecksumの検査も成功しています。Qt runtime、macOS platform plugin、GMP/MPFRなどの非system dylib、runtime inventory、Qtのofficial noticeとSBOM、ライセンス通知はbundleへ同梱されています。QtPdf、QtSvg、QtVirtualKeyboardのpluginは同梱していません。この成果物は未署名・未公証です。署名、公証、公開は別の明示操作です。Qt Core/GUI/WidgetsはLGPL-3.0-onlyを選択し、改変なしGNU LGPL-3本文、SHA-256、GNU URL、Qt公式licensing URLをQt noticeへ記録しています。Qt-GPL exceptionはこのruntimeでは選択していません。SBOMと公式licensing URLは本文の代替ではなく、法的完全性を断定しません。

現在のdev構成とsanitize構成には、それぞれ7件のCTest（document、geometry、snap、alignment、document_evaluator、ui、editor_interaction）が登録されています。

現行実装では、`DocumentEvaluator`がCircle、Rectangle、GoldenRectangle、ArcをNodeId付きスナップショットへ評価します。RectangleとGoldenRectangleは線分、Arcは円弧、sweepが±360度のArcはfull-circleとして扱います。`SymmetryNode` / `SymmetryAxis`は任意軸反射、二重反射、各primitiveとfull-circleに対応します。
円、線分、3点円弧は履歴付きArrangementへ入力でき、元曲線の履歴インデックスを境界DTOへ保持できます。
BooleanはCircle、Rectangle、GoldenRectangle、full-circleの閉じたprimitiveを任意に組み合わせ、`ArrangementModel::evaluateBoolean`が外周と穴を持つ閉じた領域DTOとして評価し、結合、交差、差、排他的論理和を返します。
境界DTOのdouble値は表示用近似であり、幾何の正本ではありません。
`FaceId`と`RegionHit::face_id`は、その評価スナップショット内でだけ有効で、永続的な識別子ではありません。
`DocumentEvaluator`はBooleanの入力グラフをprimitive葉へ展開し、N-operandのexact arrangement membershipで評価して、結果を`DocumentEvaluationSnapshot.booleans`へNodeId付きで格納します。表示DTOを経由した再評価は行いません。
深さ3、shared subtree、transform変更、Undo/Redoを含む再評価に対応します。
交差、非交差、接線、包含、同一円、空結果、subtractの入力順、xor、境界閉鎖、hitをテストしています。
`MainWindow`は`DocumentHistory`を所有し、Canvasのdrag確定、keyboard移動、Undo/Redoを履歴経由で文書へ反映します。
CanvasではNodeIdによる選択、drag中のプレビューとpointer releaseでの確定、Escape・focus喪失・mouse ungrabによる中断、pan、zoomを扱い、Objects一覧と選択を同期します。
Canvasは評価snapshotを入力に全primitiveを表示し、screen-space hit、keyboard move、Undo/Redoを扱います。
選択中のCircleには中心、半径線、半径数値を表示し、preview・cancel・commit・Undoに追従します。
対称、分割、領域選択は入力を壊さない操作ノードとして文書モデルに保持され、ノードIDの非再利用も実装済みです。
Circle、Rectangle、Arc、Golden Rectangleの配置は各ツールのpointer操作で行えます。Splitは閉じたprimitive、対称、またはBoolean結果を入力に、指定軸で全material cellを保持する非破壊ノードとして評価します。open Arcや不正な軸は拒否されます。
Splitの領域は安定した`RegionKey`で選択でき、Deleteは選択領域を直接破壊せず、RegionSelectionとRegionFilterを一つの履歴操作として追加します。ノードのDuplicateとDeleteも実装済みで、参照中のノードは既定の削除ポリシーで拒否されます。

snapはgrid、candidates、angleの純粋APIで、callerがspacing、origin、threshold、incrementを明示します。tie-breakは決定的です。Canvasのsnapは移動、配置、リサイズへ接続済みで、grid、document center、geometry候補、zoom換算した8 logical pxの許容距離、Option/Altによる一時無効化、snap guideを扱います。

Flip Horizontal / Verticalは選択boundsの中心軸を使う`SymmetryNode`として一つのHistory操作へ確定します。メニュー／toolbarと`Cmd+Option+H`／`Cmd+Option+V`から実行でき、Undo/Redoに対応します。

整列6種とhorizontal/verticalの等間隔配置も純粋APIとして実装済みです。callerがstable identityとboundsを渡し、selection boundsまたは明示的なanchorを基準にtranslationだけを受け取ります。等間隔配置は外側を固定し、identityでtie-breakし、negative gapを許容します。3項目未満は拒否します。UI接続、複数選択、toolbar/shortcut、Historyへのcommitは未実装・未決定です。

Boolean評価は幾何モジュールと文書スナップショットまで実装済みですが、Canvasと`MainWindow`のBoolean操作・結果表示へは接続していません。
open ArcはBooleanとSplitのoperandにできず、Boolean結果に対するSymmetryも未対応です。
Boolean操作の配置方式と複数選択、alignment UIの既定値とCanvas接続は未決定です。
layer、colorも未決定です。

保存形式、書き出し、色、線、透明度、レイヤー、AI機能、一般の曲線入力によるBoolean演算は、未実装または未決定です。

The application bundle is generated at `build/dev/Signet.app`. The current implementation builds history-preserving arrangements for circles, segments, and three-point arcs, including source-curve history indices in boundary DTOs. `ArrangementModel::evaluateBoolean` returns selected closed regions with outer boundaries and holes. Display DTO doubles are approximations, and `FaceId` values are valid only within their evaluation snapshot. `DocumentEvaluator` expands nested Boolean inputs to primitive leaves and evaluates N-operand exact arrangement membership without a display-DTO round trip.

Boolean evaluation supports union, intersection, subtraction, and exclusive-or for arbitrary combinations of closed Circle, Rectangle, GoldenRectangle, and full-circle primitives, with nested inputs, shared subtrees, transforms, and Undo/Redo re-evaluation covered. Open arcs are not Boolean or Split operands, and Symmetry of Boolean results is unsupported. Boolean editing and result display are not connected to Canvas or `MainWindow`. Circle, Rectangle, Arc, and Golden Rectangle placement, Split, RegionSelection, region filtering, duplicate, and delete are implemented as non-destructive document operations; region deletion commits its selection and filter pair in one history entry. Canvas snap is connected to move, placement, and resize with grid, center, and geometry candidates, zoom-scaled 8 logical-pixel tolerance, Option/Alt temporary disable, and snap guides. Flip Horizontal / Vertical is a bounds-center-axis `SymmetryNode` operation with menu/toolbar, `Cmd+Option+H` / `Cmd+Option+V`, and Undo/Redo support. Boolean placement, multi-selection, and alignment UI defaults remain undecided.

Persistence, export, color and stroke styling, layers, AI features, and Boolean operations for general curve inputs are not implemented or decided.

## 文書 / Documentation

開発・設計文書の入口は[docs/INDEX.md](docs/INDEX.md)です。

Start with [docs/INDEX.md](docs/INDEX.md) for product, engineering, and development documentation.

## ライセンス / License

SignetはGNU Affero General Public License v3.0以降で提供します。詳細は[LICENSE](LICENSE)と[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。

Signet is distributed under the GNU Affero General Public License v3.0 or later. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## コントリビューション / Contributing

[CONTRIBUTING.md](CONTRIBUTING.md)、[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)、[SECURITY.md](SECURITY.md)を確認してください。

Read [CONTRIBUTING.md](CONTRIBUTING.md), [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md), and [SECURITY.md](SECURITY.md) before contributing.
