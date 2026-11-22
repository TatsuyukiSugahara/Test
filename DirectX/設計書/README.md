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
| [System登録契約設計](System登録契約設計.md) | `SystemManager` の登録ルール(循環・未登録依存・確定後登録・確定前更新)を Release でも強制する。登録失敗を初期化失敗として伝える経路、型照合の完全一致化。フェーズ P1〜P2(2026-09-30 完了) |
| [Systemアクセス宣言設計](Systemアクセス宣言設計.md) | System ごとに Component と共有資源(カメラ・サウンド等)の読み書きを宣言させ、競合する System 間に依存経路があるかを起動時に検証する。既存の競合候補 7 件と解消案、除外指定、Debug 限定の宣言漏れ検出。フェーズ P1〜P3(2026-09-30 設計、**保留**。問題が出たときに再検討) |

## エディタ

| 資料 | 内容 |
| --- | --- |
| [エディタ全体設計](エディタ全体設計.md) | エディタ用ビルドとランタイム用ビルドの分離、ImGui Docking / Multi-Viewport、2 コンテキスト、既存 Reflect / Level / Prefab の拡張、Level の編集モデルを正本にした Play モードと Undo/Redo、コンテンツブラウザ。Edit/Play の一次資料。フェーズ E0a〜E12。**E0 完了、E1a は詳細設計まで。冒頭の「到達点と再開手順」が現状** |

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

## 全般

| 資料 | 内容 |
| --- | --- |
| [エンジン不足機能メモ](エンジン不足機能メモ.md) | 「エンジンに足りないもの」の一次メモ(2026-11-09)。構成からの推測が中心で未確認。既知の課題は README、持ち越しは各設計書へ誘導 |

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
  読み書きしていた件は、[Core/AppHost.cpp](../aqEngine/Core/AppHost.cpp)(旧 Application.cpp)で依存を宣言して解消。
- (解決済 2026-11-19)**Animation の型消去の `deserializePtr` が疎な JSON を扱えない。**
  `AnimationComponent::DeserializeFrom`([AnimationComponentSystem.cpp](../aqEngine/Component/AnimationComponentSystem.cpp))が
  先頭で `slots_.clear()` するので、`clips` の無い JSON(`{}` や `playSpeed` だけ)を渡すとクリップが消える。
  エディタのフィールド単位の編集・Undo で値を戻すときに表面化する。E6b で `clips` があるときだけ作り直す形に直した
  ([エディタ全体設計](エディタ全体設計.md) 6.4 節)。`Tools/ReflectionTest --selftest` の T2 で確かめる。
- (解決済 2026-11-19)**Terrain を型消去の経路で読んで書き出すと、`resolution` / `heightScale` / `terrainSize` が既定値に戻る。**
  [TerrainComponent.h](../aqEngine/Component/TerrainComponent.h) の `Reflect` は `resolution` を `chunk_` から書き出して `authorResolution_` へ読み込み、
  `heightScale` / `terrainSize` は `HeightmapChunk::SetHeightScale` / `SetTerrainSize` に渡すが、この 2 つはハイトマップ未読込だと値を捨てる。
  `deserializePtr` は `OnDeserialized` を呼ばないので、Prefab エディタで Terrain を読んで保存し直すと 3 つの値が失われる。
  E6b で 3 つの値をコンポーネント側のオーサリング値で往復させる形に直した(同 6.4 節)。`Tools/ReflectionTest --selftest` の T2 で確かめる。
- ~~(2026-11-19 発見)Level / Prefab エディタで参照ノードを読んで保存し直すと、override が壊れる。~~
  (2026-11-20、E4a で解消。`overridesRaw`/`loadedJson` を保持し、変わったキーだけを書き戻す形にした。
  [エディタ全体設計](エディタ全体設計.md) 7.8 節「2. override の保持」、ReflectionTest T6 で確認)。
- ~~(2026-11-19 発見)Level エディタが、インライン subLevel の中の `subLevels` を捨てる。~~
  (2026-11-20、E4b で解消。`LevelEditModel::SubLevelEdit` が `subLevels` を再帰で持つ形にした。
  ReflectionTest T6(LevelEditModel 往復・入れ子の subLevel)で確認)。
- ~~(2026-11-19 発見)Level の Unload の後に、生成が走ってエンティティが残ることがある。~~
  (2026-11-20、E4b で解消。同期ロードの遅延生成コマンドは実行前に `IsLoaded` を確かめ、非同期ロードは
  `Unload` が対象ジョブに飛ばす印を付ける形にした。ReflectionTest T8 (b)〜(d) で確認)。
- ~~(2026-11-19 発見)Level の自動リロード(`PollFileChanges`)が GPU の完了を待たずに破棄する。~~
  (2026-11-20、E4b で解消。`ReloadAll` と同じく `WaitForRenderIdle` + `FlushPendingCommands` を入れた)。
- (2026-11-20 発見)**Level エディタでインラインの subLevel を削除([x])すると、その中のノードを選択していた場合
  `selected_` が解放済みのノードを指したまま残り、同じフレームの Inspector 描画で使われる。** E4b の実装中に発見(実装自体は変えていない)。
  [LevelEditor.cpp](../aqEngine/Level/LevelEditor.cpp) の `DrawSubLevels` / `DebugRender` で、削除予約の実行時に `selected_` が削除対象の子孫なら
  クリアする対応が要る。
- ~~(2026-11-21 発見)`LevelEditModel::SaveFile`(Editor の Save と Level エディタの Save)で保存すると、`//` コメントが消える。~~
  (2026-11-22 解消。ファイル先頭のコメント(ルートの値より前の `//` 行)を読み込みで控え、保存で先頭に書き戻すようにした。
  Prefab エディタの保存も同じ。あわせて `JsonSerializer::WriteFile` をバイナリで開き、Windows でも LF で書くようにした
  (テキストモードだと CRLF になり、LF で管理しているアセットの改行が保存のたびに全部変わっていた)。ReflectionTest T12 で確認)
- (2026-11-22、上の解消の残り)**ルートの値の中に書いた `//` コメントは保存で消える。また、保存ではキーの並べ替え・既定値の書き足し・
  インデントの変更でファイル全体が書き直される。** 値は保たれる(11.4 節の意味の比較では一致)。今のアセットのコメントはすべて先頭にあるので
  実害は無いが、値の中にコメントを書くと消える。保持するには、パーサーがコメントを位置と一緒に残す仕組みが要る。
- (2026-11-21 発見)**Scene Hierarchy(`SceneHierarchySystem`)のコンポーネント削除・エンティティ破棄が、GPU の完了を待たずに実行される。**
  削除・破棄は遅延コマンドで予約され、次のフレームの `EntityContext::Update` の Flush で `WaitForRenderIdle` なしに走る。
  GPU リソースを自分で持つコンポーネント(D3D12 のバッファはデストラクタで即時 `Release`)だと、GPU が前フレームで使っている間に解放されうる。
  パスのフィールドを変えたときのリソースの差し替えも同じ問題を持ちうる。エディタ設計 E7 の Codex レビューで発見(コードは変えていない)。
  Editor の Play 中のパネルは構造の変更を出さない形にして避けた(ゲーム用のデバッグ UI には残る)。
- (2026-09-30 発見)**依存の経路が無いまま同じ Component やカメラを触る System の組が 7 件ある。**
  うち同じ wave で実際に並列に走っていた `ActorStateMachineSystem` ↔ `SpeedCharacterSystem`(`TransformComponent` を両方が書く)と
  `CoinSystem` ↔ `AutoCameraSystem`(`TransformComponent` の書きと読み)の 2 組は、2026-11-11 に
  [GameModule.cpp](../Game/Application/GameModule.cpp) で依存を宣言して直列にした(ActorStateMachine を SpeedCharacter の後、AutoCamera を Coin の後)。
  残る 5 件は wave が違うので偶然順番に走っているだけ。依存の追加で level がずれたときや、
  wave の待ち合わせをやめたときに表面化し得る。一覧と対策案は [Systemアクセス宣言設計](Systemアクセス宣言設計.md) §1.3(保留中)。
- (2026-11-11 発見)**影の「cascade」は名前だけで、中身はディレクショナルライトごとのスライス。**
  `ShadowCBData::lightViewProj[MaxShadowCascades]` / `cascadeCount` を
  [HardShadowRenderer.cpp](../aqEngine/Rendering/Shadow/HardShadowRenderer.cpp) の `FillShadowCBData` はライト数で埋めており、
  `cascadeSplits` は x に farPlane を入れるだけで使っていない。CSM を足すときは名前の整理から([エンジン不足機能メモ](エンジン不足機能メモ.md) §3)。
- (2026-11-11 発見)**Metal の `CreateShader` は読み込みに失敗しても null を返さない。**
  [MetalGraphicsDeviceImpl.mm](../aqEngine/Graphics/Metal/MetalGraphicsDeviceImpl.mm) の `CreateShader` が `MetalShader::Load` の戻り値を見ずに
  オブジェクトを返す(D3D11 / D3D12 / Vulkan は `Load` が false なら nullptr)。そのため「シェーダが作れなければ機能を外して続行」という
  作りが Metal では発動せず、空のシェーダで描画が黙って失敗する。SSR のシェーダ名を一時的に壊して確かめた
  ([SSR設計](SSR設計.md) チェックポイント。Vulkan は SSR を外して続行、Metal は SSR パスが残り映り込みだけ消える)。
  他のバックエンドに揃えるなら `if (!shader->Load(...)) return nullptr;` にするが、既存の全シェーダの失敗時の挙動が変わるので要判断。
- (2026-11-11 発見)**モーションブラーが分割画面でも動いている。**
  [MotionBlurPass.cpp](../aqEngine/Rendering/Pipeline/Passes/MotionBlurPass.cpp) は `view.IsSingleView()` で「分割画面では無効」のつもりでいるが、
  [RenderPipeline.cpp](../aqEngine/Rendering/Pipeline/RenderPipeline.cpp) の `BuildViews` は後段(ポストプロセス・UI)に分割画面でも `viewCount = 1` の全画面 view を渡すため、
  実際には分割画面でもビュー 0 のカメラで画面全体にブラーが掛かる。分割画面の判定は `PassViewInfo::frameViewCount`(被写界深度 P1 で追加)で行える
  ([被写界深度設計](被写界深度設計.md) §0.1)。直すと分割画面の絵が変わるので、本件は記録のみ。
- (解決済 2026-11-16)**D3D11 でインスタンス描画が無言で消える。** `IRenderContextImpl::DrawIndexedInstanced` /
  `IASetVertexBufferSlot` の no-op 既定を D3D11 だけ override しておらず、入力レイアウトも全要素を slot0 の per-vertex にしていた。
  Windows 機で確かめると、草・花だけでなく**道路・路肩線・中央線・コイン**(どれも `InstancedSimple`)まで AquaDash のステージから消えていた。
  [D3D11RenderContextImpl.cpp](../aqEngine/Graphics/D3D11/D3D11RenderContextImpl.cpp) に 2 つを実装し、
  [D3D11Shader.cpp](../aqEngine/Graphics/D3D11/D3D11Shader.cpp) で `I_` 始まりのセマンティクスを slot1 の per-instance にした(D3D12 と同じ規則)。
  D3D11 のステージが D3D12 と同じ絵になることを確認。
- **GPU 生成のスレッド境界が設計書と違う。** 規約は「GPU 生成はメインの `ResourceManager::Update`」だが、
  シェーダ生成は `ShaderLoader::Loading`(ワーカー)、メッシュ VB/IB は `RenderSystem::Update`(ワーカー)で
  行っている。4 バックエンドとも生成 API はスレッドセーフなので即バグではないが、規約を
  「生成はどのスレッドでも可、キューとコンテキスト操作だけレンダースレッド」に改めるか実装を寄せるかを決める。
- (解決済 2026-11-09)**ImGui の接続が Bridge の外に漏れている**件は、`IGraphicsDeviceImpl` /
  `IPlatform` の no-op 既定フック(`ImGuiInit` / `ImGuiShutdown` / `ImGuiNewFrame` / `ImGuiRender`、VSync、`GetBackendName`)へ移して解消
  ([エンジン課題解消設計](エンジン課題解消設計.md) P1-A / P1-B)。`ENGINE_GRAPHICS_*` と Platform 入力バックエンドの `#if` は
  AppHost / ImGuiRenderCommand から 0 件。フォント読込・iOS の ini パス・モバイルの UI スケールは AppHost に残してある(設計書 §2.2)。
  D3D11 / D3D12 / Win32 / UWP / Android の確認は実機待ち。
- (解決済)**抽象があるのに具象へ依存する箇所。** `IDeferredRenderer`→`DeferredRenderer` の `dynamic_cast` と
  `PostProcessChain` の直書きは、パス列方式への移行で消えていた(2026-11-09 に実コードで確認。
  [エンジン課題解消設計](エンジン課題解消設計.md) §0)。
- (解決済 2026-11-09)**パイプライン構築の失敗が黙って全描画の消失になる**件は、`AppHost::SetRenderPipeline` /
  `SetupStandardRenderers` を `bool` 戻りにして初期化失敗として伝える形で解消(同設計書 P2)。影・空の生成失敗と
  `FullscreenComputeCommand` の無ログ return にも、1 回だけのログを足した。
- (一部解消 2026-11-09)**描画解像度が起動時に固定**されていた件は、Mac の Metal で実行時リサイズに対応した
  ([エンジン課題解消設計](エンジン課題解消設計.md) P4-A)。Vulkan(MoltenVK)と iOS も P4-B で対応済み(2026-11-10)。D3D11 / D3D12 / Win32 / Android / Windows の Vulkan(P4-C)は未対応で、従来どおり固定寸法のまま動く。恒久仕様は [01 §2.11](01_レンダリング設計.md)。
  未対応のバックエンドや、`GetResizePolicy` を付けていないカスタムパスがある列は、リサイズ無効(ログに 1 回出る)になる。
- (解消 2026-11-10)**リサイズの保留中(失敗後の再試行待ち)に `FrameStep` が待ち無しで空回りしていた**件は、
  `UpdateSoundOnly` の後に 2 ms の sleep を入れて解消した(約 600 ms で 13.6 万回 → 約 270 回。再試行の間隔は約 200 ms のまま。Metal で確認)。
- (解決済 2026-11-10)**非アクティブなノードにある Exit クリップを待つと、Exit 待機が必ず 2.0 秒のタイムアウトまで続く。**
  `UIAnimationSystem::IsAnimationGroupPlaying` は非アクティブな階層も調べるが、`UIAnimationSystem::Update` はその階層を更新しないため、
  クリップが `active` のまま進まない。ボタンの callback で自身を非表示にし、同じノードに Exit クリップを付けた構成で観測した。
  Click 側(`IsTriggerPlaying`)は非アクティブな階層を飛ばして避けた([UIアニメーション階層統合設計](UIアニメーション階層統合設計.md) §20.3)。
  `IsAnimationGroupPlaying` も同じく非アクティブな階層を飛ばすようにして解消(Mac で、非アクティブなボタンの Exit が同じフレームで ExitDone、アクティブなら従来どおり 0.3 秒待つことを確認)。
- (解決済 2026-11-10)**Engine に終了要求の API が無い**件は、`IPlatform::RequestExit()`(純粋仮想)と `Engine::RequestExit(int)` / `GetExitCode()` を足して解消し、
  リサイズの致命失敗は `std::abort()` をやめて終了コード 2 で `Finalize` を通すようにした([エンジン課題解消設計](エンジン課題解消設計.md) P5)。
  Windows / UWP / Android のビルドは未確認。
- (解決済 2026-11-10)**初期化失敗でもプロセスの終了コードが 0 になる**件は、Mac の `MacMain.mm`(Game / Sample)だけが原因だった
  (Win32 の `Main.cpp` は失敗時 1 を返していた)。同じ形に揃えた。Mac の Sample で、`-app=` に存在しないモジュールを渡すと 1、
  ウィンドウを閉じて正常終了すると 0 を確認。iOS / Android / UWP はプロセスの終了コードを使わない起動形態なので対象外。
- (解決済 2026-11-10)**`TResourceBank::UnloadOne` が参照数を見ずに消す**ため、進行中ロードと競合すると同じパスを二重にデコードする件は、
  読み込み中(`IsLoading()`)なら外さず false を返すようにした。Mac の Sample で、読み込み中の `Unload` が false を返して続く `Load` が同じオブジェクトを返すこと、
  読み終えた後は true で作り直されることを確認(`Unload<T>` の呼び出し元はリポジトリ内に無い)。
- (解決済 2026-11-10)**`EntityManager::GetView` が `iterationMutex_` の外で `chunkList_` を読む**件は、対象 Chunk のインデックスを
  `GetView` ではなく `ForEach` がリードロックを取った後に求めるようにした(`GetView` にロックを足すと、入れ子の ForEach と同じく
  共有ロックの二重取得が増えるので避けた)。ビューを作ってから ForEach までの間に増えた Chunk も拾うようになる。
  Mac で Sample の画面が変更前後で一致、AquaDash でステージ走行まで動作。`FindFirstComponent` もロック外で読むが、こちらは GetComponent と同じ扱いとして残す。
- (解決済 2026-11-10)**`Graphics/GPUBuffer.cpp` は D3D11 型を直接使う旧実装**で、どこでもコンパイルされないまま残っていた件は、
  `.cpp` と転送だけの `GPUBuffer.h`(include 元なし)を削除し、vcxproj / filters の登録と CMake の除外指定も外した(Mac の Debug / Release ビルドで確認。Windows は未確認)。
  残る作業: [Graphics/RenderContext.cpp](../aqEngine/Graphics/RenderContext.cpp) の D3D11 具象リソース実装も `Graphics/D3D11/` へ移す。
- 小さいもの: 描画系コンポーネントがリソース所有と状態機械を抱える(設計書の「データのみ」と不一致)、
  `MaterialDef.h` の実体が `Lighting.h` にある、~~`DeferredDecalCommand` が深度モードを復元しない~~(解決済 2026-11-10。コマンドの最後と途中で抜ける経路で `ReadWrite` に戻す。直後のライティングが設定し直すので従来も実害は無かった。Sample にデカールを置き、変更前後の画面が一致)、
  ドローのソート / バッチングが無い、`TypeInfo::Create(hash, size)` が `ConstructFn` 無しで残る、
  ~~`PMDLoader` の `ftell` 失敗時に `fp` が漏れる~~(解決済 2026-11-10。サイズがヘッダ未満なら閉じて失敗を返す)、`SoftwareMixer` の回収リング溢れ時にオーディオスレッドで解放が走る、
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
- (解決済 2026-11-16)**D3D12 の compute は SRV が t0〜t2 の 3 枚しか届かない。** ルートシグネチャの SRV テーブルが `CS_SRV_TABLE_SIZE = 3`([D3D12RootSignature.h](../aqEngine/Graphics/D3D12/D3D12RootSignature.h))で、`D3D12RenderContextImpl::CSSetShaderResource` は範囲外のスロットを黙って捨てる。
  発見時(2026-11-14)は「SSR のトレースの t3 が捨てられる」と見ていたが、Windows 機で動かすと**捨てられる前に SSR トレースの compute PSO の生成が失敗**し
  (シェーダが t3 を宣言していてルートシグネチャに無いため E_INVALIDARG)、Debug ではアサートで止まっていた。SSR を有効にした Sample は D3D12 で起動できなかった。
  `CS_SRV_TABLE_SIZE` / `CS_SRV_COUNT` を 4(t0〜t3)に広げて解消。t3 以上を使う compute は今のところ SSR のトレースだけ。Vulkan は t0〜t11、Metal は t0〜t15 で届く。
- (2026-11-16 発見)**D3D12 は `PSSetSampler` を無視し、s0 が常に Linear / Wrap になる。**
  [D3D12RenderContextImpl.cpp](../aqEngine/Graphics/D3D12/D3D12RenderContextImpl.cpp) の `PSSetSampler` が空で、グラフィクスのサンプラは
  ルートシグネチャの静的サンプラ(s0 = Linear / Wrap、s1 = シャドウ比較)に固定されている。他のバックエンドは呼び出し側が渡したサンプラを使う。
  IBL の BRDF LUT を Clamp のつもりで読んでいたため、D3D12 だけ粗さ 0 / 1 の箱の色が違っていた(最大 43/255)。LUT の座標を端の texel 中心までに収めて回避した
  ([PBRFunctions.fx](../aqEngine/Assets/Shader/PBRFunctions.fx) の `EvalIBLSpecular`)。Clamp・Point などを前提にする他の PS も D3D12 では Wrap / Linear で読まれる。
  直すなら、よく使うサンプラを静的サンプラとして足すか、サンプラのディスクリプタテーブルを持つ。
- ~~(2026-11-16 発見)Level / Prefab エディタで保存し直すと、参照ノードの override が増える。~~
  (2026-11-20、E4a で解消。129 行目と同じ不具合、重複記録)。
- ~~(2026-11-16 発見)Level エディタの Load in World は前のプレビューを消さずに追加ロードし、in-memory 登録も残し続ける。~~
  (2026-11-20、E4b で解消。`LevelEditModel::ProcessPreviewRequest` が前回のプレビューを Unload・Unregister してから
  置き換える形にした。ReflectionTest T8 (f) で、10 回連打してもプレビューが 1 つだけ残ることを確認)。
- (2026-11-17 発見)**デバッグパネルの `ImGui::Image` で、アルファが 0 の画像が透明になって見えない。**
  ImGui はアルファでブレンドして描くため、G-Buffer0(BaseColor + Metallic)や Bright Extract のようにアルファに別の値(Metallic など)を入れた
  RT は、その値が 0 の部分が見えない。D3D11 / D3D12 / Vulkan で同じで、ImGui の公式バックエンドへ置き換える前(自前バックエンド)からの挙動。
  直すなら、RGB だけを見せる表示(アルファを 1 にする ImDrawCallback か、表示用のコピー)を足す。
- (2026-11-18 発見)**Mac(Metal)の Rendering パネルの Hi-Z タブで、深度ピラミッドの画像が真っ赤に出る。**
  1 チャンネル(R32F)の深度を ImGui がそのまま描くため、R だけが値を持ち G / B が 0 になる。E1b-1(公式 imgui_impl_metal への置き換え)の前のバイナリでも同じで、退行ではない。
  直すなら、表示用の SRV でチャンネルを R→RGB に入れ替える(Metal はテクスチャビューの swizzle)。
  同じく Mac の ImGui フォント(SFNS)に日本語のグリフが無く、日本語のタブ名(ポストエフェクト)が「?」で出る(エディタ全体設計 15 章)。
- ゲーム内 UI のボタンを押したままマウスを ImGui のウィンドウ上へ動かすと、押下表示(`isPressed`・アニメーションの Pressed 条件)が残る。`UIInputSystem::UpdatePointer` は `IsMouseSuppressed()` のとき何もせずに戻り、ホバーの解除も押下の更新もしないため(Game.exe でも起きる。2026-11-19、エディタ全体設計 5.8 節の調査で発見)。エディタのゲームビューの外の扱いは、抑制を使わない形で避けている(同節)。
- (2026-11-20 発見)**`InstancedStaticMesh::RegisterFromData` は同じ名前が登録済みなら既存を返す。** 登録はエンジン全体で 1 つで、エディタの再生の停止でも消えない。AquaDash は路面などのメッシュを固定の名前で登録するので、頂点の違う別のステージを同じ名前で読むと、前のステージのメッシュが使われる(今はステージが 1 つなので出ていない)。直すなら、名前にステージ ID を含めるか、ステージを捨てるときに登録を外す(エディタ全体設計 8.3 / 8.5 節の調査で発見)。
- ~~Editor.exe(D3D12)のウィンドウの大きさを変えると、エディタの UI 全体が左上に寄って崩れる~~(2026-11-20、エディタ全体設計 E3b-1/E3b-2 で解消。D3D12 のレンダーリサイズと、提示面/レンダー解像度を分ける口を実装)。

## 文書管理方針

- aqEngine 固有の Markdown 設計資料はこのフォルダへ集約します。
- ソースコードへのリンクはリポジトリルートから追跡可能な相対リンクにします。
- 実装状況や制約が変わった場合は、概要設計と該当する詳細設計を同時に更新します。
- `ThirdParty` 配下の README は外部ライブラリの原文であるため集約対象外です。
