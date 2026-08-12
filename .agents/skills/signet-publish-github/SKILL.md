---
name: signet-publish-github
description: Signetの検証・commit済み変更を、明示された範囲だけGitHubへpushし、必要ならDraft Pull Requestを作成・更新する。対象branch、SHA、秘密検査を照合し、tagとGitHub Release公開はreleaseへ渡す。
---

# Signet GitHub Publication

## 公開権限と対象を固定する

1. `git status --short`、branch、upstream、remote URL、未pushcommit、対象SHAを確認し、既存差分を公開対象へ混ぜない。
2. branch push、branch作成、Pull Request作成・更新のうち明示された操作だけを行う。mainへの直接push、force push、履歴変更を推測しない。tag push、Draft Release作成、Release公開は別許可であり、このSkillのpush許可へ含めない。
3. commit未作成の修正、version未確認、CTest・秘密情報検査未実施、remote側の新しいcommitがある場合は公開せず報告する。version更新は対象変更と同じcommitに含まれ、version-only commitや同一versionの複数commitがないことも確認する。
4. tag、配布物、Draft Release、Release公開は`signet-release`へ渡す。

## 差分とremoteを確認する

1. remote branch、local HEAD、remote SHA、共通祖先、送信rangeを確認し、意図しないmerge・巻き戻しが入らないことを確かめる。
2. `scripts/secret-guard.sh range <base> <head>`、`git diff --check`、version・license・成果物の検査を送信rangeへ実行し、remoteのcheck結果をlocal証拠の代わりにしない。
3. 接続失敗、認証拒否、権限不足、対象不存在を分け、token値を出力・保存・引用しない。
4. CIのActions結果をlocal検証の代替とせず、実credential、token、password、秘密鍵、個人情報、非公開ファイルを送信対象・ログ・説明へ含めない。検査fixtureは架空値だけを使う。

## 公開後を確認する

1. 明示された通常pushを対象remote・branchへ行い、GitHub側SHAがlocal HEADと一致することを確認する。
2. PR作成時は同じhead・baseのOpen PRを先に確認し、ready指定がなければDraftとする。
3. 公開先、commit SHA、PR番号・状態、検査、未実施操作、並行変更を分けて報告する。
