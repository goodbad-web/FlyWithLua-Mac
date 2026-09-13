# AGENT.md

このリポジトリで作業する Codex 向けの運用メモです。

## 優先事項
- correctness, safety, readability, maintainability を優先
- 既存の設計・データフロー・命名に合わせる
- 依頼外の機能追加や不要なリファクタはしない
- 変更は最小差分で、buildable を維持する

## 調査方針
- 修正前に原因・責務・影響範囲を確認する
- 変更対象だけでなく、呼び出し元・状態の持ち主・更新経路も必要に応じて見る
- 不確実なまま断定しない

## このプロジェクトの前提
- XcodeGen の `project.yml` が source of truth
- `FlyWithLua-Mac.xcodeproj` は生成物として扱う
- Swift / SwiftUI / C++ / Lua ブリッジが混在するので、型変更や state 変更は影響範囲を必ず確認する
- UI 修正は `src/GUI/` を中心に、state lifetime と更新経路を崩さない

## 実装ルール
- View は軽く保つ
- state の重複を避ける
- 必要以上に public interface を変えない
- 大規模な構造変更より、まず最小の安全な修正を探す
- 生成ファイルを直接直すより、元定義を直して再生成する

## 確認
- 変更後は可能ならビルド確認まで行う
- UI 変更は見た目だけでなく accessibility と hit area も見る
- 影響が読めない場合は、短く要点を整理してから追加調査する

