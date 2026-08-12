---
name: signet-audit-skills
description: Signetの`.agents/skills/`にあるSkill集合を責務、発火条件、frontmatter、相互関係、補助資源、参照切れ、重複、指示量、代表依頼のroutingから監査する。不要Skillや不足領域を根拠付きで提案・適用し、監査だけでは変更しない。
---

# Signet Skill Audit

## モードと対象を固定する

1. `git status --short`を確認し、監査のみ、監査と適用、候補指定の適用を分ける。
2. 対象はrepository-ownedの`.agents/skills/`と明示されたSkill rootに限定し、外部参照を「全Skill」と呼ばない。
3. 監査と適用では、問題、根拠、影響、適用対象を先に報告し、`delete`、外部書き込み、履歴変更、秘密・権限変更は明示範囲だけ行う。
4. 本Skill自身を一覧へ一度だけ含め、自己監査を起点に全体監査を再帰開始しない。

## 証拠を集める

1. 各Skillのfrontmatter `name`・`description`、本文、相対参照、補助資源、ディレクトリ名を機械的に収集し、名前の重複と参照切れを分類する。
2. `rg --files .agents/skills`、frontmatter境界の確認、相対リンクの存在確認、文字数・行数の計測を同じHEADで再実行できる形にする。
3. descriptionを発火判定の一次根拠とし、本文だけの入口条件、隣接Skillとの責務衝突、禁止操作の欠落を問題候補にする。
4. AGENTS.md、docs/INDEX.md、現行CMake・CTest・workflowと照合し、架空のパス、コマンド、仕様を確定事実にしない。

## 反証して判断する

1. `keep`、`revise`、`rename`、`merge`、`split`、`deprecate`、`delete`、`create`、`hold`を、根拠、反対証拠、代替、失われる知識とともに使う。
2. 行数、語の類似、参照数だけで重複や不要を確定しない。実際の誤発火、停止、安全条件の衝突、二重更新の証拠を求める。
3. 高影響候補は独立した読み取りpassを二回行い、一致しない、根拠不足、移行不能なら`hold`にする。
4. 代表依頼に正例、対象外、隣接境界、入口、検証、commit、公開の段階を含め、期待Skillと誤発火を記録する。

## 適用と検証

1. 適用前に対象Skillを再読し、現行差分へ小さなパッチを当てる。既存の固有知識、参照、補助資源を無断で削除しない。
2. 全Skillのfrontmatter、名前とディレクトリ、相対参照、主要routing、AGENTS.mdに列挙されたSkillを再検証する。
3. `git diff --check`と全差分を確認し、未承認の追加・削除・外部書き込みを行わない。
4. 監査範囲、保証水準、問題、変更候補、実施内容、未確認、並行変更を分けて報告する。
