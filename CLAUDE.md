# aqEngine / Game

自作 C++ ゲームエンジン(`DirectX/aqEngine`)とゲーム(`DirectX/Game`)。
Windows(D3D11/12・Vulkan・UWP)、Mac(Metal/Vulkan)、iOS、Android に対応する。

## 基本方針

- 応答・コメント・コミットメッセージ・設計書は**日本語**。
- **設計書が正本**。`DirectX/設計書/`(エンジン)と `DirectX/Game/Design/`(ゲーム)。
  まず `DirectX/設計書/README.md` の読順と、関連する設計書の「到達点」節を読む。
  推測で設計せず、実コードを読んで書く。
- Claude の memory(`~/.claude/`)はリポジトリに入れない。別 PC へ引き継ぐ知見は設計書へ書く。
- 既存ファイルの**改行コードと BOM は保持する**(多くは LF だが、`aqEngine/aq.h` など
  CRLF + BOM のファイルがある)。エディタ任せで変換しない。

## 設計書の運用規約

- 設計書は「ファイル別責務表 + 末尾チェックポイント(`- [ ]` 形式)」を基本形とする。
- タイトル直下に `> 対象コミット: <hash> / 最終更新: <日付>` を置く。設計に影響するコミットで更新する。
  **本文を実コードと突き合わせずに日付・ハッシュだけを進めない。** 未反映の差分があるときは、
  直下に `> ⚠ 鮮度: ...` で未反映の範囲を明記する。
- **同じ内容を 2 つの設計書に書かない。** 一方を一次資料と明記し、他方はリンクで参照する。
- 内容は必ず実コード(ヘッダ / 主要 .cpp)を読んで書く。推測で埋めない。未確認箇所は TODO として残す。
- 発見したバグ・命名の澱みは README の「既知の課題」節に追記する(その場で直さない)。
- Feature Level 等のプラットフォーム固有の具体値は該当プラットフォーム設計書に一元化し、
  他書は条件表記(例: FL11_0 未満)で参照する。
- 置き場所: エンジン機能は `DirectX/設計書/`、ゲーム固有は `DirectX/Game/Design/<プロジェクト>/`。

## スキル(`.claude/skills/`)

| 作業 | スキル |
|---|---|
| C++ を書く・直す・レビューする | `cpp-gamedev`(規約) |
| まとまった機能の開発 | `feature-dev-cycle`(設計 → 実装 → 評価) |
| `.cpp` / `.h` の追加・移動・削除 | `vs-project-files` |
| コミット | `git-commit`(件名は `<Game|Engine|Build|Chore> 内容`) |

## サブエージェントのモデル

- `model` はタスクの難しさに応じて Claude が選ぶ(定型は `sonnet`、軽作業は `haiku`、難所のみ `opus`)。
- **`fable` は使わない**(`fork` も使わない)。

## ビルド・実行の入口

- Windows: `DirectX/DirectX.sln`(Visual Studio)。
- Mac / iOS / Android: CMake(`DirectX/CMakeLists.txt`、`DirectX/Tools/SetupCMake/README.md`)。
  この Mac は Homebrew 無しで、実行前に環境スクリプトの source が要る(手順は memory の `mac-build-environment`)。
- 移植ごとの手順と落とし穴は `DirectX/設計書/` の Mac / iOS / Android 移植設計を参照。
