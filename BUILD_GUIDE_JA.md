# FlyWithLua-Mac: ビルド＆デプロイガイド

このガイドでは、FlyWithLua-MacをUniversal Binary (arm64 + x86_64) としてビルドし、X-Plane 12へデプロイする手順を説明します。

## 事前準備

- **macOS 12.0+** (Swift Concurrencyを利用するため、Monterey以降を推奨)
- **Xcode 13.0+**
- **Homebrew** (ツールの管理用)
- **XcodeGen**: Homebrew経由でインストールしてください：
  ```bash
  brew install xcodegen
  ```

## ステップ 1: 依存関係の準備

自動化スクリプトを実行して、必要なライブラリを取得およびビルドします。

1. **LuaJIT (Universal Binary)**:
   ```bash
   chmod +x scripts/build_luajit.sh
   ./scripts/build_luajit.sh
   ```
   *これによりLuaJIT v2.1がクローンされ、`lib/LuaJIT/lib/libluajit.a` に統合ライブラリが作成されます。*

2. **X-Plane SDK 4.0**:
   ```bash
   chmod +x scripts/setup_sdk.sh
   ./scripts/setup_sdk.sh
   ```
   *これにより最新のSDKがダウンロードされ、`include/` ディレクトリにヘッダーが整理されます。*

## ステップ 2: Xcodeプロジェクトの生成

`project.yml` の設定から `.xcodeproj` ファイルを生成します。
```bash
xcodegen generate
```

## ステップ 3: ビルド＆デプロイ

Xcodeから直接ビルドするか、提供されているデプロイスクリプトを使用できます。

### デプロイスクリプトを使用する場合
```bash
chmod +x scripts/deploy.sh
./scripts/deploy.sh
```
*このスクリプトはUniversal Binaryをビルドし、`dist/` ディレクトリにパッケージングします。*

### Xcodeでの手動ビルド
1. `FlyWithLua-Mac.xcodeproj` を開きます。
2. **FlyWithLua-Mac** ターゲットを選択し、デスティネーションとして **Any Mac (Apple Silicon, Intel)** を選択します。
3. ビルドを実行します (**Cmd + B**)。
4. 生成された `FlyWithLua.xpl` バンドルは、ビルド出力ディレクトリに保存されます。

## X-Planeへのデプロイ

`FlyWithLua.xpl` バンドルをX-Planeのプラグインディレクトリにコピーします：
`X-Plane 12/Resources/plugins/FlyWithLua/mac.xpl`

*(注: Macでは、プラグインは `FlyWithLua.xpl` という名前のバンドルフォルダ、またはフォルダ内の `mac.xpl` という名前に変更された単一ファイルのいずれかになります。本プロジェクトではバンドルを生成します。)*

## トラブルシューティング

- **アーキテクチャの不一致**: 生成されたバイナリに対して `lipo -info` を実行し、`arm64` と `x86_64` の両方が含まれていることを確認してください。
- **コード署名**: X-Planeがプラグインのロードに失敗する場合は、コード署名を確認してください。`project.yml` はデフォルトでアドホック署名を行うように設定されています。
