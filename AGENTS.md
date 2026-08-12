# AGENTS.md

この文書は、SignetリポジトリでAIエージェントが常に守る判断規則を定義する。
製品仕様、設計、開発手順の詳細はここへ重複させず、`docs/INDEX.md` から必要な正本だけを確認する。

## 優先順位

- ユーザーが求める結果と明示範囲を最優先にする。
- 品質、幾何学的正確性、ユーザーデータ保護を満たしたうえで、読み込み量、ツール呼び出し、所要時間を抑える。
- 確認済みの事実、推論、提案、未決定事項を区別する。提案を決定済み仕様として実装しない。
- 現行文書と実装は現在状態の根拠として扱い、ユーザーが変更を指示した場合は変更後の仕様と実装を一致させる。

## 対話

- 丁寧な日本語で、結論から説明する。
- 複数案で結果が大きく変わる場合は、比較軸、推奨案、欠点、決定に必要な情報を先に示す。
- 複数段階のツール作業では、開始前に対象と最初の確認を短く伝える。
- 「提案」「相談」「調査」「レビュー」は読み取り専用とし、明示的な変更依頼なしに実装しない。

## 情報源と文書

- 最初から全文書を読まず、対象コード、`rg`、`docs/INDEX.md` の該当ルートから始める。
- 製品方針は `docs/project/overview.md`、機能とUIは `docs/features/editor.md`、設計と技術判断は `docs/engineering/` を正本とする。
- 外部リポジトリは一次情報として確認するが、その製品固有の仕様、名称、技術選定をSignetへ自動移植しない。
- GitHub URLが提示された場合は、回答または変更前に対象を直接確認する。確認できない場合は理由を明示する。

## 指示と権限

- 質問、説明、調査、相談、レビュー、診断、計画、提案では必要な資料を確認して報告し、編集や外部変更を行わない。
- 変更、実装、修正、更新、整理が明示された場合は、指定範囲のローカル編集と非破壊的な検証まで進める。
- コミット、push、Issue操作、Pull Request、タグ、Releaseは、それぞれ明示された場合だけ行う。
- ユーザーの変更、無関係な差分、別の作業者の変更を保持する。

### 変更・commit・version・公開の正本

- stage-only、commit、branch push、tag作成、tag push、Draft Release作成、Release公開は別操作であり、一つの許可を次の操作へ拡張しない。stage-onlyではversionを変更しない。
- commit許可を受けた時点で変更の最大impactを判定し、対象変更とversion更新を同じcommitへ含める。versionだけのcommit、同一versionの複数commit、対象変更なしのversion更新は禁止する。
- version正本は`CMakeLists.txt`の`project(VERSION ...)`とする。bundle、表示、checkerはこの値へ一致させる。version規則の入口は`scripts/version-policy.sh`、`scripts/verify.sh version`、`scripts/verify.sh version-self-test`を前提とする。
- 現在の未commit bootstrap状態は`0.1.0`を維持し、新規規則は次に明示許可されたcommitから適用する。最初のcommitを分割する場合も各commitでversionを順次更新する。
- tag名は`MAJOR.MINOR.PATCH`（`v`なし）とし、version一致、未使用、commit済み、clean、local/remote reachability、local release verify、秘密情報検査を満たすまで作成しない。tag作成、tag push、Draft、Publishはそれぞれ独立して扱う。
- CIのActionsはlocal release verifyの代替ではない。公開済みassetは上書きせず、内容を変える公開は新しいPATCHとして扱う。
- commit、diff、ログ、文書、fixtureへ実credential、token、password、秘密鍵、個人情報その他の秘密を入れない。値を出力・保存・引用せず、検査には架空値を使い、疑いがあれば内容を再掲せず停止して報告する。

詳細な形式、version判定、公開・release手順は、[開発手順](docs/development.md)、[commit Skill](.agents/skills/signet-commit/SKILL.md)、[version Skill](.agents/skills/signet-manage-version/SKILL.md)、[GitHub公開 Skill](.agents/skills/signet-publish-github/SKILL.md)、[release Skill](.agents/skills/signet-release/SKILL.md)を正本とする。

### 作業を止めて確認する条件

次のいずれかに該当し、ユーザーが具体的に決定していない場合は作業を止める。

- 対応プラットフォーム、配布方式、ライセンス、文書形式など、製品の成立条件を変える判断。
- UIフレームワーク、描画基盤、幾何演算エンジン、永続化形式など、交換コストが高い技術選定。
- 複数案でユーザー体験、互換性、性能、保守性、セキュリティ、OSS参加障壁が大きく変わる。
- データ消失、Git履歴の書換え、秘密情報、権限変更、外部送信、公開、費用発生を伴う。

確認時は、確認済み事実、候補ごとの差、推奨案と理由、決定後の影響だけを示す。

## Signet固有の不変条件

- アプリ基盤はC++23、Qt 6、CGAL 6.2とし、採用済み技術を提案段階へ戻さない。変更には新しい要件、比較根拠、ユーザー決定を必要とする。
- QtはデスクトップUIと表示を担当し、幾何演算の正しさをQPainterPathのBoolean演算へ依存させない。
- CGAL型は`src/geometry/`内へ隔離し、保存形式、UI、公開インターフェースの正本へ漏らさない。
- 編集データの正本は、再編集可能なパラメトリック幾何モデルとする。画面上のピクセル、ラスタ画像、描画API固有オブジェクトを正本にしない。
- 円、円弧、黄金長方形、派生図形、変換、対称、Boolean演算の意味を文書化し、表示と保存で同じモデルを使う。
- 座標系、単位、精度、許容誤差、縮退形状、塗り規則、演算結果の安定順序を暗黙にしない。
- Undo/Redoは操作結果を再現できる設計にし、保存形式にはschema versionと移行方針を持たせる。
- 入力ファイルを信頼せず、解析上限、パス境界、外部参照、破損データを安全に扱う。
- テレメトリ、アカウント、クラウド同期、AI送信、ネットワーク必須機能を、明示された製品判断なしに追加しない。
- OSSライセンスと互換しない依存、素材、フォント、コードを追加しない。
- 比較対象のアプリ名や参考リポジトリ固有の用語をSignetの仕様、UI文言、コード、テストへ残さない。

## 検証と完了条件

- リスクに応じ、対象テスト、型チェック、lint、ビルド、文書整合、差分確認から必要なものを実行する。
- 幾何演算では正常系だけでなく、接線、重複、ほぼ一致する辺、空結果、自己交差、巨大・微小座標、対称性、順序安定性を検証する。
- UI操作ではpointer確定と中断、keyboard、focus、zoom、pan、drag、狭幅、高DPI、reduced motionを影響範囲に応じて確認する。
- 実アプリの起動、GUI操作、スクリーンショットはユーザーが明示した場合だけ行う。
- ツール成功表示だけで完了とせず、差分と重要な生成結果を確認する。
- 完了報告には変更結果、更新した正本、検証結果、未確認事項、主要な参照対象を含める。

## 実在する開発入口

- 日常のconfigure、build、CTestは `scripts/verify.sh dev`、Sanitizerは `scripts/verify.sh sanitize` を使う。
- C++形式は `scripts/verify.sh format`、clang-tidyは `scripts/verify.sh lint`、Release bundle検査は `scripts/verify.sh bundle` を使う。
- `format` と `lint` はllvmを自動インストールせず、未導入時は `brew install llvm` の案内とともに停止する。詳細な依存、CTest一覧、bundle手順は `docs/development.md` を正本とする。
- Git hooksの有効化、依存のinstall/update、GUI起動、署名、公証、公開操作は、明示的な依頼なしに行わない。

## Skillルーティング

- ローカル変更前は `signet-guard-task` を使う。
- 技術選定、描画基盤、プラットフォーム判断は `signet-select-technology` を使い、提案と採用を分離する。
- 幾何モデル、Boolean演算、スナップ、座標変換は `signet-change-geometry` を使う。
- エディタUI、Canvas操作、pointer・keyboard操作は `signet-change-editor-ui` を使う。
- テスト追加、失敗、flaky、coverageは `signet-test-code` を使う。
- 正本文書、README、OSS運用文書は `signet-maintain-docs` を使う。
- ステージまたはコミットは `signet-commit`、GitHubへのpushまたはPull Requestは `signet-publish-github` を使う。
- Skillは必要な段階でだけ読み、専門Skillの安全条件を共通手順で置き換えない。
