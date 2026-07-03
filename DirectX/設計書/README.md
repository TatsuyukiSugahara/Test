# aqEngine 機能別設計書(レビュー用)

> 対象コミット: `700699e` / 最終更新: 2026-07-03

ハイブリッドゲームエンジン(DirectX 11 / DirectX 12 / Vulkan / Xbox / UWP 対応)の機能別設計書。
各ファイルが何をしているかをレビューできる粒度でまとめている。

> 運用ルール: 設計に影響するコミットでは、該当設計書の対象コミットを更新すること。

| # | ドキュメント | 対象 |
|---|---|---|
| 01 | [レンダリング設計](01_レンダリング設計.md) | `Graphics/`(API 抽象・Bridge)、`Rendering/`(フレーム/コマンド/ディファード/シャドウ/ポストプロセス/カリング) |
| 02 | [HID設計](02_HID設計.md) | `HID/`(キーボード/マウス/パッド抽象・ActionMap) |
| 03 | [サウンド設計](03_サウンド設計.md) | `Sound/`(SoundEngine・XAudio2 バックエンド・3D・デコーダ・ECS 統合) |
| 04 | [ECS設計](04_ECS設計.md) | `ECS/`・`Component/`(アーキタイプ ECS・System 並列・Prefab) |
| 05 | [マルチスレッド設計](05_マルチスレッド設計.md) | `Util/`(ThreadPool/JobSystem/Profiler)、`RenderThread`、非同期ロード |
| 06 | [ゲームアプリケーションコア設計](06_ゲームアプリケーションコア設計.md) | `Engine`・`Core/`・`Platform/`・`Game/Application/` |
| 07 | [リソース管理設計](07_リソース管理設計.md) | `Resource/`(非同期ロード・共有キャッシュ・ローダ) |

初見のレビュアーは 06 → 01 → 04 → 05 → 07 → 02 → 03 の順を推奨。

## ハイブリッド構成の共通思想

全システムが **Bridge パターン + `#define` バックエンド選択**で API/プラットフォーム差を吸収する:

| 系統 | 抽象 IF | バックエンド | 選択 |
|---|---|---|---|
| グラフィックス | `IGraphicsDeviceImpl` / `IRenderContextImpl` | D3D11 / D3D12 / Vulkan | `ENGINE_GRAPHICS_*`(aq.h) |
| サウンド | `ISoundBackend` / `ISoundVoice` | XAudio2 / (Oboe) | `SoundBackend.h` |
| パッド入力 | `IPadBackend` | XInput / WinRT | `PadBackend.h` |
| プラットフォーム | `IPlatform` | Win32 / UWP | エントリで注入 |

上位レイヤー(`aq::rendering` / `aq::sound` コア / ECS / ゲーム)は API・プラットフォームを知らず、
差し替えても呼び出し側は無改修 —— これが移植コストを抑える設計の核。

## 既存の詳細設計書(サブシステム内)

- [カリング設計](../aqEngine/Rendering/カリング設計.md)
- [D3D12Backend設計](../aqEngine/Graphics/D3D12/D3D12Backend設計.md) / [VulkanBackend設計](../aqEngine/Graphics/Vulkan/VulkanBackend設計.md)
- [Sound設計](../aqEngine/Sound/Sound設計.md) / [AudioAuthoring設計](../aqEngine/Sound/AudioAuthoring設計.md)
- [Prefab設計](../aqEngine/ECS/Prefab設計.md)
- [Xbox移植設計](../aqEngine/Platform/Xbox移植設計.md)

## 既知の課題

- [x] `ComponentArray.h`: `begin_()` / `eng()` の綴り(→ `begin()` / `end()`。挙動影響なし)→ タスクB-1で対応済み(`144ae8a`)
- [x] `MAX_COMPONENT_SIZE` は実態が「個数」(→ `MAX_COMPONENT_COUNT` へ改名)→ タスクB-2で対応済み(`144ae8a`)
- [ ] `ThreadPool` / `JobSystem` の併存(→ ThreadPool に一本化、JobSystem 凍結中)
- [x] リソース管理の unload / 未使用解放 API を追加(`8f16111`)。`UnloadUnused()` / `Unload<T>()` でシーン切替時にメモリ解放可。世代カウンタは shared_ptr 所有のため不採用。→ [07 §3](07_リソース管理設計.md)
- [x] `PlatformBudget` の予算チェックを実装(`700699e`)。`maxSingleFileBytes` はロード時に強制(超過拒否)、`memoryBudgetBytes` は `MemoryManager::IsOverMemoryBudget()` で観測(非強制)。→ [07 §6](07_リソース管理設計.md)
