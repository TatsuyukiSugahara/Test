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
| [レンダーパイプライン設計](レンダーパイプライン設計.md) | パス列方式。`IRenderPass` / `PipelineBuilder` / `PassResources`、プラットフォーム別構成、ゲーム独自パスの挿入、フェーズ計画 P1〜P5(2026-09-16 設計、実装未着手) |

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

## UI

| 資料 | 内容 |
| --- | --- |
| [UIアニメーション階層統合設計](UIアニメーション階層統合設計.md) | `Clip → ClipTrack → PropTrack → Keyframe` の 4 階層を 3 階層へ畳む設計。同時実行の受け皿を Clip 側へ移す、条件付きクリップの常時評価、グループ起動の 32bit ハッシュ化、フェーズ計画 P0〜P3(2026-09-16 設計、実装未着手) |

## 使いやすさ

| 資料 | 内容 |
| --- | --- |
| [使いやすさ課題メモ](使いやすさ課題.md) | 「学生でも使いやすい」を目標にした課題整理と着手順。エンジンのアセット自立化、起動ボイラープレート、System 依存配線、静かな失敗、入門ドキュメント |
| [使いやすさ改善設計](使いやすさ改善設計.md) | 上の課題への対応設計(一次資料)。コンテンツルート 2 系統化、パス解決の一元化、フェーズ計画 P1-A〜P4。**P1〜P4 完了。冒頭の到達点表と §6 持ち越し一覧が現状** |

## メモリ

| 資料 | 内容 |
| --- | --- |
| [メモリ管理課題メモ](メモリ管理課題.md) | アロケータ / 予算 / コールスタックの課題整理と対応順序。GPU 未計上、予算の実測化、確保失敗の伝播、再帰の深度ガード、将来の予約 + 遅延コミット化 |

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
| [Mac移植設計](Mac移植設計.md) | macOS/Metal 移植の設計。方針決定、Platform/入力/サウンド/Vulkan(MoltenVK) の責務表、フェーズ計画とチェックリスト |
| [Mac移植調査](Mac移植調査.md) | 同・調査メモ(一次資料)。Windows 依存の棚卸し、MoltenVK/DXC の対応状況、道A/道B 比較 |
| [Android移植設計](Android移植設計.md) | Android(NDK/Vulkan)移植の設計。VS の Android ワークロードを使わない理由、ビルド/プラットフォーム層/ライフサイクル/タッチ入力/アセット配置の責務表、フェーズ計画 |
| [iOS移植設計](iOS移植設計.md) | iOS(UIKit/Metal)移植の設計。iOS SDK での事前実測(195 TU のうち失敗 4 本、シミュレータの Metal 機能値)、メインループの所有権、BC 圧縮テクスチャ、タッチ入力、サンドボックスとファイル IO、フェーズ計画 |

## 既知の課題

2026-09-16 のエンジン設計レビュー(ECS / Resource / レンダリング / マルチプラットフォーム抽象 / コア)で
見つかった項目。同日に直したものは「解決済」、残りは未対応。

- (解決済 2026-09-16)**コンポーネント 16 個の Entity へ `AddComponent<T>` すると壊れる。**
  `Archetype::AddType` が上限で無言 no-op を返し、同じチャンクへの自己 Move で位置が範囲外になった後に
  存在しない領域へ placement new していた。`AddType` を bool 戻りにし、
  [EntityManager.h](../aqEngine/ECS/EntityManager.h) の `AddComponentByLocation` で assert + false に。
- (解決済 2026-09-16)**静的 `.tkm` / `.obj` がパス解決を通らず生の `fopen`** だった件と、
  **FBX の 4 経路が候補全滅時に何も言わない**件を [Resource.cpp](../aqEngine/Resource/Resource.cpp) で修正
  (使いやすさ改善設計.md §5)。
- (解決済 2026-09-16)**Vulkan の `ImmediateSubmit` がレンダースレッドの提出と排他していなかった。**
  `VkQueue` / `VkCommandPool` は外部同期必須。直列描画(`AQ_RENDER_PIPELINED` 無効)では重ならないため
  顕在化していなかった。`queueMutex_` で提出区間を排他。
- (解決済 2026-09-16)`SpawnSystem` と `HierarcicalTransformSystem` が同一 wave で `parentHandle` を
  読み書きしていた件は、[Core/Application.cpp](../aqEngine/Core/Application.cpp) で依存を宣言して解消。
- **D3D11 でインスタンス描画が無言で消える。** `IRenderContextImpl::DrawIndexedInstanced` /
  `IASetVertexBufferSlot` の no-op 既定を D3D11 だけ override しておらず、
  [Renderer.cpp](../aqEngine/Rendering/Renderer.cpp) も機能ゲート無しで `instancedItems` を積む。
  草(`InstancedGrass.fx`)は D3D11 では描かれないはず。Windows 機で目視確認のうえ、D3D11 に実装する。
- **GPU 生成のスレッド境界が設計書と違う。** 規約は「GPU 生成はメインの `ResourceManager::Update`」だが、
  シェーダ生成は `ShaderLoader::Loading`(ワーカー)、メッシュ VB/IB は `RenderSystem::Update`(ワーカー)で
  行っている。4 バックエンドとも生成 API はスレッドセーフなので即バグではないが、規約を
  「生成はどのスレッドでも可、キューとコンテキスト操作だけレンダースレッド」に改めるか実装を寄せるかを決める。
- **ImGui の接続が Bridge の外に漏れている。** [Core/Application.cpp](../aqEngine/Core/Application.cpp) に
  バックエンド 4 択 / プラットフォーム 5 択の `#if` が計 13 ブロック、
  [ImGuiRenderCommand.cpp](../aqEngine/Rendering/ImGuiRenderCommand.cpp) は `GetImplRaw()` を具象型へキャスト。
  `IGraphicsDeviceImpl` / `IPlatform` に ImGui 用フック(no-op 既定)を足せば消える。
  `GetImplRaw()` のコメント「D3D11 専用」も実態と違う。
- **抽象があるのに具象へ依存する箇所。** `Application` が `IDeferredRenderer` を `DeferredRenderer` へ
  `dynamic_cast` し、失敗時はモーションブラーと Hi-Z がログ無しで無効化される。
  `PostProcessChain` は `IPostProcessPass` を持ちながら 3 パスをメンバに直書き。
- **`TResourceBank::UnloadOne` が参照数を見ずに消す**ため、進行中ロードと競合すると同じパスを二重にデコードする。
- **`EntityManager::GetView` が `iterationMutex_` の外で `chunkList_` を読む。**
- **[Graphics/GPUBuffer.cpp](../aqEngine/Graphics/GPUBuffer.cpp) は D3D11 型を直接使う旧実装**で、
  vcxproj 未登録 + CMake の明示除外により「どこでもコンパイルされない」まま抽象層のディレクトリに残っている。削除候補。
  [Graphics/RenderContext.cpp](../aqEngine/Graphics/RenderContext.cpp) の D3D11 具象リソース実装も `Graphics/D3D11/` へ移す。
- 小さいもの: 描画系コンポーネントがリソース所有と状態機械を抱える(設計書の「データのみ」と不一致)、
  `MaterialDef.h` の実体が `Lighting.h` にある、`DeferredDecalCommand` が深度モードを復元しない、
  ドローのソート / バッチングが無い、`TypeInfo::Create(hash, size)` が `ConstructFn` 無しで残る、
  `PMDLoader` の `ftell` 失敗時に `fp` が漏れる、`SoftwareMixer` の回収リング溢れ時にオーディオスレッドで解放が走る、
  設計書の記述ずれ(`GameTimer` は `steady_clock`、`Engine::Finalize` に `MemoryManager` は含まれない)。
- (解決済 2026-09-14)`FindProjectRoot` の 6 重複は `Resource/AssetPath.{h,cpp}` へ一元化した
  (使いやすさ改善設計.md P1-A)。以下は発見当時の記録。
- (2026-09-12 発見)`FindProjectRoot` が **6 ファイルに重複実装**されている
  (`Resource.cpp` / `VulkanShader.cpp` / `MetalShader.mm` / `MetalRenderContextImpl.mm` / `D3D12Shader.cpp` / `D3D11Shader.cpp`)。
  うち Metal 経路の 2 本は `GetContentRoot()` を見ずカレントディレクトリ依存のままで、
  サウンド(`OpenStream` / `LoadBank`)と一部メッシュ(`.tkm` / `.obj` / `.pmd`)も同様。
  Mac は `chdir` で凌いでいる([iOS移植設計.md](iOS移植設計.md) §7.1)。パス解決の一元化は別途 `<Engine>` で行う。
- (解決済 2026-09-15)Metal バックエンドに GPU の機能クエリが無かった件は、iOS 移植で
  `supportsBCTextureCompression` / `supportsFamily` / `hasUnifiedMemory` /
  read-write texture tier を起動時に実測してログへ出し、機能フラグへ流す形で解消
  ([MetalGraphicsDeviceImpl.mm](../aqEngine/Graphics/Metal/MetalGraphicsDeviceImpl.mm) §機能クエリ)。
  残っていた「生成が nil でも何も言わない」ほうは
  `metal::ReportCreationFailure()` で報告するようにした(使いやすさ改善設計.md P4-A)。
- (解決済 2026-09-09)オフスクリーンパス(512²)がディファード経路でシーンを描けない件
  (512² RTV × 1920×1080 DSV の寸法不一致。2026-09-06 発見)は、P17 で縮小 GBuffer を
  自前所有する独立パス `OffscreenScenePass` を新設して解消(01_レンダリング設計.md §8)。
  AquaDash のミニマップも UI 点列方式から俯瞰 RT ベイク方式へ戻した。
- (解決済 2026-09-09)`BloomRenderer` の名前が実態と合っていない件は、P15 で
  `PostProcessChain` + パス分割(MotionBlur / Bloom / Tonemap)へリファクタして解消
  (01_レンダリング設計.md §2.6 / §7)。
- (解決済 2026-09-06) `aq::math::Quaternion::SetRotation` の代入ミス(`z` に入れるべき値を `y` へ二重代入)を修正。
  利用箇所ゼロのまま壊れていた。ゲーム側の回避実装(CoinSystem の自前 Y 軸回転)も削除し本 API 使用に統一。

## 文書管理方針

- aqEngine 固有の Markdown 設計資料はこのフォルダへ集約します。
- ソースコードへのリンクはリポジトリルートから追跡可能な相対リンクにします。
- 実装状況や制約が変わった場合は、概要設計と該当する詳細設計を同時に更新します。
- `ThirdParty` 配下の README は外部ライブラリの原文であるため集約対象外です。
