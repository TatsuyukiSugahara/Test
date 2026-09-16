---
name: windows-regression-pending
description: Windows 回帰は 2026-09-10 に一度実施済み。未実施で残るのは UWP と、P4b(2026-09-11)以降の変更
metadata: 
  node_type: memory
  type: project
  originSessionId: 67329cf3-7e0e-4cab-b8c5-681ec8644ca3
  modified: 2026-09-11T02:14:02.140Z
---

Mac 移植で入ったプラットフォーム非依存の変更について、**2026-09-10 に Windows 回帰確認を
一度実施した**(`DirectX.sln` / MSBuild v145 で D3D11 / D3D12 / Vulkan の Debug をビルド、
0 エラー・警告 54 件 = マージ前と同数。3 構成ともタイトルまで起動し終了コード 0)。
このとき **D3D12 の終了時クラッシュを発見・修正**した(移植の回帰ではなく既存不具合。設計書 §8-19)。

**まだ Windows で見ていないもの:**

1. **UWP(`DebugXbox`)** — 2026-09-10 の PC に Windows Store 向けツールセットが無く
   MSB8020 で止まったため未確認。特に §8-10(Xbox で地形が「読めない」→「読める」に変わる)
2. **P4b(2026-09-11 / コミット `3e74793`)で入った共有ファイルの変更**
   — `Core/Application.cpp` のみ。Mac 経路は `#elif defined(AQ_PLATFORM_MAC)` に閉じているが、
   **ImGui のフォント読み込みを Windows / Mac で共用する形に整理した**ので、
   Windows のフォント探索が壊れていないかは要確認(グリフ範囲表とロード手順を共通化し、
   探索先だけ分岐させた)。UWP が拾う `#else` の `DisplaySize`/`DeltaTime` 手当ては残してある
3. **Metal バックエンド追加(2026-09-11 / P0〜P6)で入った共有ファイルの変更**
   — `aq.h`(`ENGINE_GRAPHICS_METAL` の検証)/ `Engine.cpp` / `Core/Application.cpp` /
   `Rendering/ImGuiRenderCommand.cpp` / ルート `CMakeLists.txt`。
   Metal 経路は `#elif defined(ENGINE_GRAPHICS_METAL)` に閉じているが、次の 2 つは
   **Windows の表示に直接効く**:
   - **ImGui のフォント読み込みを Win/Mac で共用する形に整理した**(グリフ範囲表と
     ロード手順を共通化し、探索先だけ分岐)。Windows のフォントが従来どおり出るか
   - **FPS オーバーレイのバックエンド名に `Vulkan` / `Metal` を追加した**。
     Windows の Vulkan 構成が `?` から `Vulkan` に変わる(意図した変更)
4. 実操作(キー/パッド)での通しプレイ、`GameTimer` の FPS 制限

**How to apply:** 次に Windows を触るときは **最初に** D3D11 / D3D12 / Vulkan / UWP(DebugXbox)を
ビルドして AquaDash を起動し、ImGui のフォントが以前どおり出ることを含めて回帰を見る。
関連: [[mac-port-phase-status]] / [[mac-imgui-own-backend]]
