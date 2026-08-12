# 開発手順

## 必要環境

- macOS 26
- Apple Silicon
- Apple Clang
- CMake 3.31以降
- Ninja
- Qt 6.11
- CGAL 6.2

```sh
brew install cmake ninja qt cgal
```

## ビルドとテスト

日常の開発確認は、既存のCMake presetとCTestをまとめた検証入口から実行できます。

```sh
scripts/verify.sh dev
```

Finderから個別に実行する場合は、リポジトリ直下の`Signet-Build.command`をビルド用、
`Signet-Test.command`をテスト用としてダブルクリックできます。テスト用は既存の`build/dev`
を対象にするため、先にビルド用を実行してください。どちらも処理結果を表示し、Finderの
ターミナルでは確認後にEnterキーを押すまで画面を保持します。

個別の構成・解析・bundle検査は次の入口を使います。

```sh
scripts/verify.sh sanitize
scripts/verify.sh format
scripts/verify.sh lint
scripts/verify.sh bundle
```

上記をすべて順に実行する場合は`scripts/verify.sh all`です。

各コマンドを直接実行する場合は次のとおりです。

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev --output-on-failure
```

Sanitizerを有効にする場合は次を使う。

```sh
cmake --preset sanitize
cmake --build --preset sanitize --parallel
ctest --preset sanitize --output-on-failure
```

現在のCTestには次の7件が登録されています。

| CTest | 主な範囲 |
|---|---|
| `document` | schema、操作ノード、RegionSelection/RegionFilterのatomic commit、Duplicate、参照削除policy、Undo/Redo |
| `geometry` | Arrangement、Boolean、Split、provenance、接線・包含・穴・空結果 |
| `snap` | grid、candidate、angleの純粋API |
| `alignment` | 6種整列とhorizontal/vertical distribution |
| `document_evaluator` | primitive／Boolean／Symmetry／Split／RegionSelection／RegionFilterのsnapshot再評価 |
| `ui` | placement、選択、drag、Split、region selection/delete、Duplicate/Delete、Undo/Redo、focus、pan、zoom |
| `editor_interaction` | 座標変換とselection transformの純粋API |

`scripts/verify.sh format`は`.clang-format`によるC++形式を確認します。`scripts/verify.sh lint`は`.clang-tidy`を使うclang-tidy専用configure/build経路を使用します。どちらもローカルツールを自動インストールせず、未導入なら`brew install llvm`の案内とともに終了します。CIではllvmを導入して両方をgateします。

CIも同じverify入口を使い、通常ビルドから分離したclang-tidy解析を実行します。

### Secret Guard

秘密らしい値、秘密鍵ヘッダー、代表的なサービスtoken、AWS access key、明白なpassword assignment、ローカル絶対パスを、値を出力せずに検査します。通常のworktree検査とfixture self-testは次の入口から実行します。

```sh
scripts/verify.sh secret
scripts/verify.sh secret-self-test
```

commit前はindexだけを検査し、pushやPull Requestの差分はbaseからheadまでの追加行とcommit本文を検査します。

```sh
scripts/secret-guard.sh staged
scripts/secret-guard.sh range <base> <head>
```

`build/`、`build-*`、`dist/`、`out/`、app bundle、binary、third-party license tree、`*.sha256`は検査対象外です。fixtureは一時ディレクトリ内で値を分割して組み立て、リポジトリへ保存しません。hookの自動installは行いません。

## 変更、commit、version、公開

### 許可の境界

次の操作は独立しており、一つの許可を後続操作へ拡張しません。

- stage-only（指定差分の検証とstageだけ）
- commit（version判定・更新とcommitまで）
- branch push
- tag作成
- tag push
- Draft Release作成
- Release公開

stage-onlyではversionを更新しません。commit許可を受けた時点でversionを判定し、対象変更と同じcommitへ含めます。versionだけのcommit、同一versionを複数commitで使うこと、対象変更なしのversion更新は禁止です。独立した目的を分割する場合は、各commitでversionを順次更新します。

### commit形式とversion判定

commit typeは次の11種類に固定します。

`feat`、`fix`、`docs`、`test`、`refactor`、`perf`、`build`、`ci`、`chore`、`style`、`revert`

影響が最大の規則を採用します。MAJORはユーザーが明示し、かつbreaking changeである場合だけ、`feat`はMINOR、それ以外の10 typeはPATCHです。複数の影響が混在する場合はMAJOR > MINOR > PATCHの順で判定します。互換性が壊れる可能性だけでMAJORを推測せず、明示がなければ確認します。

件名はscopeを持たせず、次の形式にします。

```text
<type>[!]: <version> 日本語の説明
```

`!`は明示されたbreaking changeだけに使います。本文には次の5項目を必ず含めます。`Scope:`の値は英語小文字の名詞とし、件名のscopeとは別物です。

```text
Scope: release
目的: 何を解決するか
内容: 何を変更したか
確認: 何を検証したか、未実施ならその理由
影響: ユーザー、互換性、配布、未確認事項への影響
```

実credential、token、password、秘密鍵、個人情報、非公開ファイルその他の秘密をcommit、diff、ログ、文書、fixtureへ含めません。値を出力・保存・引用せず、fixtureは架空値だけを使います。秘密が疑われる場合は内容を再掲せず、検査を止めて報告します。

versionの正本は`CMakeLists.txt`の`project(VERSION ...)`です。CMake configure、app bundle、install、アプリ表示、checkerの値は正本へ一致させます。bundle checkerへ渡す`SIGNET_EXPECTED_VERSION`は必須で、CMakeの正本から指定します。SBOMはこのversion規則の対象に含めません。機械checkerの実装入口は、別途存在を確認したうえで`scripts/version-policy.sh`、`scripts/verify.sh version`、`scripts/verify.sh version-self-test`を使います。未実装の入口を実装済みとは記載しません。

現在の未commit bootstrap状態では`0.1.0`を維持します。新規規則は次に明示許可されたcommitから適用します。最初のcommitを複数へ分割する場合は、最初のcommitを`0.1.1`、次を`0.1.2`とするよう、各commitで順次更新します。

### tagとRelease

tagは`MAJOR.MINOR.PATCH`（`v`なし）で、versionと完全一致する未使用tagだけを対象commitへ作成します。commit済み、clean worktree、local/remote reachability、local release verify、秘密情報検査が揃わない場合はtagを作成しません。tag作成、tag push、Draft Release作成、Release公開は別操作です。

Actionsの成功はlocal release verifyの代替になりません。公開済みassetは上書きせず、差し替えが必要な場合は新しいPATCHを作成します。現在のworkflowにはDraft Release作成の自動化はないため、release時はこの文書と[Release checklist](../.github/RELEASE_CHECKLIST.md)をコピーして使い、各操作を個別に確認します。

## Release bundle

Release packaging uses Qt 6.11's official CMake deployment API. The install
step runs the Qt deployment tool, bundles Qt Core/Gui/Widgets, the macOS
platform plugin, and recursively discovered non-system dylibs such as GMP and
MPFR, then installs the applicable notices under the app's Resources folder.
The checked-in runtime inventory is a dual contract. `manifest.tsv` maps
component rows and license files, and each row's `bundle files` field records
the exact non-system Mach-O paths expected in the app. `checksums.sha256`
detects changes to the runtime inventory and is checked both before and after
install.

```sh
cmake --preset release
cmake --build --preset release
stage="$(mktemp -d)"
cmake --install build/release --prefix "$stage"
expected_version="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p' CMakeLists.txt)"
cmake \
  -DSIGNET_EXPECTED_VERSION="$expected_version" \
  -DSIGNET_BUNDLE="$stage/Signet.app" \
  -DSIGNET_SOURCE_DIR="$PWD" \
  -DSIGNET_BUILD_DIR="$PWD/build/release" \
  -P cmake/verify_macos_bundle.cmake
```

The verification is read-only and does not launch the GUI. The Release install
and verification succeed for the explicitly supplied CMake project version with minimum OS 26. Every non-system
Mach-O is arm64 and present exactly once in the manifest, and all manifest
paths match the bundled paths exactly. The checker compares the `bundle files`
path inventory with its Mach-O inventory; it does not compare component-row
counts with Mach-O counts or license-file counts. Thus `Manifest rows checked`
and `Mach-O files checked` are different metrics, while the listed path sets
must agree. Homebrew, workspace, and build
dependency absolute paths and RPATHs are absent, `libqcocoa` is present, and
the Info/version/minimum-OS metadata remain expected. The runtime license and
checksum checks also succeed. QtPdf, QtSvg, and QtVirtualKeyboard plugins are
not bundled. The bundle includes the runtime inventory, Qt official notice,
SBOM, and applicable license files. It remains unsigned and unnotarized;
signing, notarization, and publication are separate release operations.

The bundle includes the unmodified GNU LGPL v3 text at
`licenses/runtime/Qt-6.11.1/LGPL-3.0.txt` with its recorded SHA-256. The
GNU source URL and [Qt licensing page](https://doc.qt.io/qt-6.11/licensing.html)
are recorded in the Qt notice. Qt Core, GUI, and Widgets select LGPL-3.0-only;
the Qt-GPL exception is not selected for this runtime. Module-specific
additional notices remain version-dependent and no legal-completeness claim is
made.

When dependency versions change, repeat the temporary Release install, update
`licenses/runtime/manifest.tsv` and the exact upstream files without editing
their contents, regenerate `licenses/runtime/checksums.sha256` (all runtime
files except that checksum file), and rerun the checksum and bundle verifier.

## 検証順序

1. 変更対象のテストを実行する。
2. `cmake --build --preset dev`で警告を含めてビルドする。
3. `ctest --preset dev`を実行する。
4. メモリまたは未定義動作へ影響する変更はSanitizer構成でも実行する。
5. `git diff --check`と全差分を確認する。

GUI起動と実画面確認は、その作業で明示された場合だけ実施する。
