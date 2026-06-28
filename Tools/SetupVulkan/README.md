# SetupVulkan — Vulkan バックエンド用セットアップキット

aqEngine の **Vulkan レンダリングバックエンド**をビルド／実行するための環境を、
PC を変えても再現できるようにするためのフォルダです。

対象バージョン: **Vulkan SDK 1.4.350.0**（LunarG）

---

## 使い方（新しい PC でのセットアップ）

1. `SetupVulkan.bat` をダブルクリックする。
   - SDK 未導入なら **インストーラを自動ダウンロード**して導入します。
   - 既に導入済みなら**検証のみ**行います。
   - 管理者昇格（UAC）と環境変数 `VULKAN_SDK` の設定が行われます。
2. 終了後、**新しいコマンドプロンプト / Visual Studio を開き直す**（`VULKAN_SDK` を反映するため）。
3. `DirectX/aqEngine/aq.h` を `#define ENGINE_GRAPHICS_VULKAN` に変更してリビルド。

> インストーラ exe（`vulkansdk-windows-X64-1.4.350.0.exe`, 約300MB）は
> **GitHub の 100MB ファイル上限を超えるため git にはコミットしていません**（`.gitignore` 除外）。
> このフォルダに置いておけばオフラインで使え、無ければ bat が再ダウンロードします。
> 手動取得 URL:
> `https://sdk.lunarg.com/sdk/download/1.4.350.0/windows/vulkansdk-windows-X64-1.4.350.0.exe`

---

## ビルドに必要なもの（このリポジトリ側は設定済み）

| 項目 | 供給元 | 備考 |
|---|---|---|
| Vulkan ヘッダ / `vulkan-1.lib` | Vulkan SDK | vcxproj が `$(VULKAN_SDK)\Include` / `\Lib` を参照 |
| `dxcompiler.dll`（実行時 HLSL→SPIR-V） | Vulkan SDK `Bin` | Game の **post-build が出力フォルダへ自動コピー** |
| VMA / SPIRV-Reflect | リポジトリ同梱 | `DirectX/ThirdParty/{vma,spirv_reflect}` |
| 検証レイヤ（任意・Debug） | Vulkan SDK | コードが自動有効化、`vk_debug.log` に出力 |

## 実行（ビルド済み exe を別 PC で動かす場合）

Vulkan SDK は**ビルド時のみ**必要です。実行には以下があれば動きます。

- `vulkan-1.dll` … GPU ドライバ同梱（通常どの PC にもある）
- `dxcompiler.dll` … exe の隣（`x64\Debug` 等）に必要 → post-build が配置済み
- `Game/Assets/Shader/*.fx` … 実行時にコンパイルするため必要

---

## トラブルシュート

- **ビルドで `vulkan/vulkan.h` が見つからない** → SDK 未導入か、VS を開き直していない（`VULKAN_SDK` 未反映）。bat を実行し VS を再起動。
- **実行時に dxcompiler.dll が無い** → リビルドすれば post-build が再コピー。手動なら `%VULKAN_SDK%\Bin\dxcompiler.dll` を exe の隣へ。
- **サイレントインストールに失敗** → bat が通常インストーラを起動するので画面に従う。
