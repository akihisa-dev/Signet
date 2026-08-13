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
`Signet-Test.command`を開発用アプリ起動用としてダブルクリックできます。ビルド用は成果物の生成だけを行い、
開発用アプリ起動用はCTestを実行せず、
`dev` presetのconfigureとincremental buildを自分で行った後、生成した開発アプリを起動します。
そのため、ビルド用を先に実行する必要はありません。configure/buildに失敗した場合は日本語の案内を
表示して終了します。アプリはbundle内の実行ファイルを前景で起動するため、標準出力・標準エラーを
確認でき、起動直後の終了も終了コードで検知できます。アプリを終了するとターミナルも終了し、
Enterキーの入力待ちは発生しません。CTestを実行する場合は、引き続き
`scripts/verify.sh dev`または下記のコマンドを使用してください。

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

現在のCTestには次の10件が登録されています。

| CTest | 主な範囲 |
|---|---|
| `document` | schema、操作ノード、RegionSelection/RegionFilterのatomic commit、Duplicate、参照削除policy、Undo/Redo |
| `geometry` | Arrangement、Boolean、Split、provenance、接線・包含・穴・空結果 |
| `snap` | grid、candidate、angleの純粋API |
| `alignment` | 6種整列とhorizontal/vertical distribution |
| `document_evaluator` | primitive／Boolean／Symmetry／Split／RegionSelection／RegionFilterのsnapshot再評価 |
| `ai_plan` | LogoConstructionPlan v1のJSON解析・厳格な上限・参照検証・コンパイル／atomic apply |
| `ai_provider` | Codex CLI providerの入力検証、非同期要求、キャンセル、出力制限、失敗分類 |
| `ui` | placement、選択、drag、Split、region selection/delete、Duplicate/Delete、Undo/Redo、focus、pan、zoom |
| `ai_ui` | AIダイアログの同意、入力検証、preview、stale revision、Apply、cancel、provider状態 |
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

AIロゴ生成は自然言語または参照画像（両方も可）を入力にできますが、両方なしは拒否します。実環境確認には、ユーザーがインストールしてログインしたCodex CLIが必要です。providerは外部送信前に同意を要求し、構造化計画の指示をstdinへ前置したうえでread-only／ephemeralで非同期実行します。`--output-schema`によるparserの安全なsubset（図形種別・数値・必須フィールド）と、parserによるcross-node意味検証を組み合わせます。実Codex、ネットワーク、認証、GUIの手動確認は通常の`dev`検証には含まれず、配布・サブスクリプション条件と正式な結果保存は未決定です。

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

### commitのタイミング

変更・構築タスクでは、独立目的ごとに次の状態を順番に完了し、必要な検証後にローカルcommitします。

`planned -> editing one purpose -> verified -> versioned -> staged -> committed -> post-commit checked -> next purpose`

- `planned`で目的、対象、検証方法を決め、`editing one purpose`ではその目的だけを実装・編集します。次の独立目的の調査・計画はできますが、実装・編集は前目的のcommit後まで開始しません。
- `verified`で前目的の対象検証を完了し、その直後かつcommit直前の`versioned`でversionを決定・更新します。`staged`では検証済みの一目的とversionだけを明示パスでstageし、`committed`後にSHA、index、残差分を`post-commit checked`で確認します。
- 失敗した場合は次の状態や次の目的へ進みません。複数目的をworktreeへ蓄積し、最後にpatch-stageして事後的に複数commitへ分ける運用は禁止です。
- 各目的は対象検証後に最新のstatus、diff、indexを確認し、対象だけを明示パスでstageしてcommitし、commit後にhashとstatusを確認します。検証失敗、規則不明、競合、秘密情報、Gitの不適切な状態ではcommitせず停止します。ユーザーがcommit禁止・未コミット・監査のみ・計画のみを指定した場合もcommitしません。stage-onlyの明示依頼はstage後に停止し、versionを更新しません。複数目的を蓄積して後から分割する運用は禁止します。
- 並行agentが独立範囲を編集しても、同一branchのcommit順序はメイン担当が管理し、前commit後に次担当の変更を統合します。commit境界をまたぐversion正本を複数担当が同時編集しません。

正しい流れは次のとおりです。

```text
feature A edit -> feature A test -> feature A version -> feature A commit -> post-commit check
-> feature B edit -> feature B test -> feature B version -> feature B commit
```

次の流れは誤りです。

```text
feature A/B/Cをすべて編集 -> 最後にpatch-stageして3commitへ事後分割
```

stage-onlyではversionを更新しません。対象検証直後にversionを判定し、対象変更と同じcommitへ含めます。versionだけのcommit、同一versionを複数commitで使うこと、対象変更なしのversion更新は禁止です。独立した目的を分割する場合は、各commitでversionを順次更新します。

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

checkerだけで編集時系列や、各変更が本当に一目的で進められたことを完全に証明することはできません。`staged` modeは、stage対象外の非ignored tracked/untracked pathが3件以上残っている場合を「次目的が残っている可能性が高い」状態として警告し、専用overrideと理由がなければ停止します。意図的な既存差分などで継続する場合は、`--allow-dirty-next-purpose --override-reason "理由"`、または対応する環境変数へ理由を指定します。少数の残差分は誤検知を避けるため自動判定しません。`range` modeは各commitのCMake version更新、直前commitからの単調増加、commit件名versionとの対応、bump規則を検査しますが、事後分割を完全には見抜けません。


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
