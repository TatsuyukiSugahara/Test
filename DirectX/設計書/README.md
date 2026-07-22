# aqEngine 設計ドキュメント

aqEngine の概要設計、バックエンド詳細、データ仕様、移植記録を集約しています。
実装コードから設計資料を分離し、技術レビュー時に分野別で追える構成にしています。

## 推奨読順

1. [ゲームアプリケーションコア設計](06_ゲームアプリケーションコア設計.md)
2. [レンダリング設計](01_レンダリング設計.md)
3. [ECS設計](04_ECS設計.md)
4. [マルチスレッド設計](05_マルチスレッド設計.md)
5. [リソース管理設計](07_リソース管理設計.md)
6. 興味のある分野の詳細設計

## 概要設計

| 資料 | 対象 |
| --- | --- |
| [01 レンダリング設計](01_レンダリング設計.md) | 描画抽象、フレーム構築、Deferred、Shadow、PostProcess、カリング |
| [02 HID設計](02_HID設計.md) | キーボード、マウス、パッド抽象、ActionMap |
| [03 サウンド設計](03_サウンド設計.md) | SoundEngine、XAudio2、3D音響、デコーダ、ECS統合 |
| [04 ECS設計](04_ECS設計.md) | Archetype、Chunk、System並列、Prefab |
| [05 マルチスレッド設計](05_マルチスレッド設計.md) | ThreadPool、RenderThread、非同期ロード、Profiler |
| [06 ゲームアプリケーションコア設計](06_ゲームアプリケーションコア設計.md) | Engine、Application、Platform、GameFlow |
| [07 リソース管理設計](07_リソース管理設計.md) | 非同期ロード、共有キャッシュ、解放、メモリ予算 |

## グラフィックス

| 資料 | 内容 |
| --- | --- |
| [D3D12バックエンド設計](D3D12Backend設計.md) | Device、CommandList、Descriptor、PSO、同期、実装フェーズ |
| [Vulkanバックエンド設計](VulkanBackend設計.md) | Vulkan 1.3、Dynamic Rendering、Descriptor、同期、実装フェーズ |
| [カリング設計](カリング設計.md) | Frustum、Hi-Z、Cluster、GPU駆動カリング、間接描画 |

## ECS / ゲーム構築

| 資料 | 内容 |
| --- | --- |
| [Prefab設計](Prefab設計.md) | JSON Prefab、Reflection、override、遅延生成、エディタ |
| [Level設計](Level設計.md) | Level階層、非同期ロード、ストリーミング、エディタ |

## パーティクル / アセット

| 資料 | 内容 |
| --- | --- |
| [FBX・ParticleSystem導入設計](FBX・ParticleSystem導入設計.md) | ufbx、Unityエクスポータ、CPU simulation、描画実装 |
| [.particleフォーマット仕様 v1](particleフォーマット仕様v1.md) | Unityから受け渡す独自JSON形式の正本 |

## サウンド

| 資料 | 内容 |
| --- | --- |
| [Sound設計](Sound設計.md) | Backend / Voice、寿命、リアルタイム契約、3D音響、ストリーミング |
| [Audio Authoring設計](AudioAuthoring設計.md) | Event、Bank、Bus、Snapshot、ECS連携、編集UI |
| [サウンド残作業](サウンド残作業.md) | 現在の残作業と次の実装候補 |

## プラットフォーム / Xbox

| 資料 | 内容 |
| --- | --- |
| [Xbox移植設計](Xbox移植設計.md) | UWP / GDK方針、リソース制限、抽象化、移植計画 |
| [Xbox UWP移植変更まとめ](Xbox_UWP移植_変更まとめ.md) | Xbox One実機描画までの変更、調査結果、配置手順、制約 |

## 文書管理方針

- aqEngine 固有の Markdown 設計資料はこのフォルダへ集約します。
- ソースコードへのリンクはリポジトリルートから追跡可能な相対リンクにします。
- 実装状況や制約が変わった場合は、概要設計と該当する詳細設計を同時に更新します。
- `ThirdParty` 配下の README は外部ライブラリの原文であるため集約対象外です。
