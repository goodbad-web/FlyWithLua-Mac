# SKILLS.md

このリポジトリでの作業時に、必要なら優先して使う観点です。

## SwiftUI
- レイアウト・状態・更新経路の修正は `src/GUI/` を中心に見る
- 大きい View は分割を検討するが、まずは最小修正を優先する
- state の所有者をむやみに増やさない

## Core / Bridge
- `src/Core/`, `src/Bridge/`, `src/XPLM+Swift/` の変更は依存先まで確認する
- C++ / Swift 境界の型や所有権を雑に変えない

## Build
- 設定変更は `project.yml` を優先して直す
- 必要なら XcodeGen で再生成する
- 生成物だけを直して済ませない

## Review / Verify
- 変更後は buildable かどうかを確認する
- UI 変更はアクセシビリティと表示崩れも見る

