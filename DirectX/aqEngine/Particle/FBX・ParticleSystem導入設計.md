# FBX 読み込み・ParticleSystem 導入 設計（メモ・第2版）

> 対象コミット: 7c6e081 / 最終更新: 2026-07-07
>
> Particle 側は [particleフォーマット仕様v1.md](particleフォーマット仕様v1.md) が一次資料（フォーマットは同書が正）。
> 本書はそれを **どう実装に落とすか**（データ表現・ランタイム・描画・フェーズ）を決める設計メモ。

> Unity で作成した **FBX モデル**と **ParticleSystem** を本エンジン（aq）で使えるようにする。
> 既存の Sprite 描画（UIBatchRenderer）とモデル描画（StaticMesh / SkeletalMesh → RenderFrame）
> の資産を最大限流用する。本書は方針合意時点のメモであり、実装未着手。

## 0. 決定事項

- **FBX 方式**: 既存の `FbxLoader`（[../Resource/Resource.h](../Resource/Resource.h) 内）の**空スタブを埋めて
  ランタイム直読みする**方針。ユーザー希望「既存の FBX 読み込み処理を使う」に沿う。
  ただし出荷（Xbox/UWP）を考え、パーサライブラリは PC 限定（`#if !defined(AQ_PLATFORM_UWP)`）で
  積み、UWP は従来どおり TKM に焼く二段構えを許容する。
- **Particle 取り込み**: **Unity から独自 JSON（`.particle`）をエクスポート**して読む。
  Unity の `.prefab` YAML 直接パースは採用しない（バージョン差・フォーマット変化に弱いため）。
- **配置**: 新規モジュールは `aqEngine/Particle/`（本ディレクトリ）。FBX 側は既存 `aqEngine/Resource/` に追記。

## 1. 現状資産と再利用方針

| 既存資産 | 場所 | 本件での役割 |
|---|---|---|
| `FbxLoader`（宣言のみ） | [../Resource/Resource.h:249](../Resource/Resource.h#L249) | **空スタブ**（`return true;`）。ここに FBX パースを実装 |
| `MeshResource` / `MeshData` | [../Resource/Resource.h:160](../Resource/Resource.h#L160) | FBX → `VertexData`/`indices`/`MaterialTexturePaths` の受け皿（既存） |
| `SkeletalMeshResource` / `BoneData` | [../Resource/Resource.h:295](../Resource/Resource.h#L295) | スキン FBX の bone / inverseBindPose 受け皿（既存） |
| TKM/TKA ローダー | [../Resource/Resource.cpp](../Resource/Resource.cpp) `MeshLoader`/`SkeletalMeshLoader` | 出荷パス（変換済みバイナリ）。維持 |
| 非同期ロード基盤 | [../Resource/Resource.h](../Resource/Resource.h) `ResourceLoaderBase` / `ResourceManager` | FBX/Particle 双方の Load 経路に流用 |
| 動的頂点バッファ | [../Graphics/StaticMesh.h:70](../Graphics/StaticMesh.h#L70) `InitializeDynamic`/`UpdateVertices` | パーティクルのビルボード VB を毎フレーム更新 |
| forward 透明パス | [../Rendering/RenderFrame.h:145](../Rendering/RenderFrame.h#L145) `forwardItems` | パーティクル（半透明）の描画キュー |
| ブレンド切替 | [../Rendering/SetBlendModeCommand.h](../Rendering/SetBlendModeCommand.h) | Additive / Alpha の切替 |
| スプライトバッチ | [../UI/Rendering/UIBatchRenderer.h](../UI/Rendering/UIBatchRenderer.h) | クアッド生成・アトラス処理の**実装雛形**（UI はスクリーン空間なので流用は雛形まで） |
| JSON | [../Util/SimpleJson.h](../Util/SimpleJson.h) | `.particle` のロード |
| カメラスナップショット | [../Rendering/RenderFrame.h:23](../Rendering/RenderFrame.h#L23) `CameraData` | ビルボード展開の right/up |

> 新規に本当に必要なのは **①FBX パーサ結線**、**②`ParticleSystemData` + JSON ローダー**、
> **③`ParticleEmitterComponent` + `ParticleSystem`（CPU sim）+ ビルボード描画**の3点。
> 生成/描画の実体は既存の Resource / RenderFrame / 動的VB を使う。

## 2. FBX 対応

### 2.1 現状（重要）

- `FbxLoader` は**宣言だけ**存在し、実装 [../Resource/Resource.cpp:95](../Resource/Resource.cpp#L95) は `return true;` の空スタブ。
- FBX SDK / Assimp は**リポジトリに未導入**（[../Resource/Resource.h:22](../Resource/Resource.h#L22) の `#pragma comment` はコメントアウト）。
- `Game/Assets/` に `unityChan.fbx` 等は**置いてあるが未配線**。実ロードは `unityChan.tkm`（TKM）。
- → 「既存の FBX 読み込みを使う」= **この空スタブに実装を入れる**作業になる。受け皿（`MeshResource`/`Reflection`/拡張子振り分け）は既に揃っている。

### 2.2 実装方針

1. **FBX パーサライブラリを 1 つ導入**（自前パースは非現実的）。
   - 推奨: **Assimp**（単一ライブラリ・寛容ライセンス・FBX + glTF 対応）。`ThirdParty/` に追加。
   - 代替: FBX SDK（`libfbxsdk` を復活）。Autodesk ライセンス + UWP ビルド難あり。
2. `FbxLoader::Loading()` を実装:
   - `aiMesh` → `graphics::VertexData` / `indices`
   - `aiMaterial` → `MaterialTexturePaths`（albedo/normal/specular/emissive）
   - スキンあり: `SkeletalMeshResource` 側に同様の loader（`aiBone` → `BoneData::inverseBindPose`、`aiAnimation` → `AnimationClipData`）
3. `.fbx` 拡張子を `Reflection<MeshResource, FbxLoader>()` に振り分け → [StaticMeshComponent::SetModelPath](../Component/BodyComponentSystem.h#L94) からそのまま使用可。
4. プラットフォーム: PC 限定 `#if !defined(AQ_PLATFORM_UWP)` でランタイム直読み。UWP 出荷は TKM 変換を維持。

## 3. ParticleSystem 対応（`.particle` v1 準拠）

### 3.1 パイプライン（仕様 §1）

```
Unity ParticleSystem
   │  Unity/Editor/AqParticleExporter.cs（Tools > aqEngine > Export ParticleSystem）
   ▼
xxx.particle（.particle v1 / SimpleJson でパース）
   │  ParticleLoader：JSON → 焼き込み（カーブ/グラデを LUT 化）
   ▼
ParticleSystemData（イミュータブルなリソース。実行時はカーブ評価ゼロ）
   │  ParticleEmitterComponent が参照
   ▼
ParticleSystem（ecs System）= CPU sim → ビルボード RenderItem → RenderFrame.forwardItems
```

### 3.2 仕様が設計を規定する3つの要点

再考の核。仕様 v1 から確定した実装方針。

1. **ロード時に LUT へ焼き込む（仕様 §4.2/§4.4）。**
   Curve/TwoCurves は 64 サンプル float LUT（multiplier 込み）、Gradient は 64 サンプル RGBA LUT。
   ステップキー（`|tangent| >= 1e18`）と `±1e30`（Infinity 置換）も焼き込み時に処理。
   → **実行時はエルミート評価も JSON も触らない**。ホットループは配列参照＋線形補間のみ。
2. **時間軸・乱数の二系統（仕様 §5）。** カーブ横軸 `t` は項目で異なる：
   - **エミッタ正規化時間** `emitterNormT = (経過 - startDelay) を duration で正規化`
     … `initial.*`（生成時サンプル）/ `gravityModifier`（毎フレーム）/ `rateOverTime`（毎フレーム）
   - **粒子正規化年齢** `age/lifetime`（0〜1）… `*OverLifetime` / `frameOverTime`
   乱数 `r` は **粒子ごと 32bit シード1個**を持ち、項目別に `Hash(seed ^ 項目ID) / UINT32_MAX`
   で導出（項目間の相関を断つ）。項目 ID は `enum class ParticleRandomItem` で列挙する。
3. **ScalarValue は統一型（仕様 §4.1）。** Constant / TwoConstants / Curve / TwoCurves を
   1 つの評価関数 `Evaluate(t, r)` に集約：
   - Constant → `a`
   - TwoConstants → `lerp(a, b, r)`
   - Curve → `SampleLUT(lutMin, t)`（multiplier 焼き込み済み）
   - TwoCurves → `lerp(SampleLUT(lutMin, t), SampleLUT(lutMax, t), r)`
   LUT は `ParticleSystemData` 所有の float プール（`std::vector<float>`）に連結配置し、
   ScalarValue は `{ mode, a, b, lutMinOffset, lutMaxOffset }` の軽量タグ付き構造体にする
   （定数がメモリを食わず、Curve 系はオフセット参照）。

### 3.3 データ層 `ParticleSystemData`（リソース・イミュータブル）

仕様 §6/§7 の構造をそのまま保持（焼き込み後）。

- ルート: `name` / `warnings`（ツール表示用に保持）/ `emitters[]`
- エミッタ: 基本（duration・looping・startDelay・maxParticles・simulationSpace・gravityModifier）
  + `initial` / `emission` / `shape` / `velocityOverLifetime` / `colorOverLifetime` /
  `sizeOverLifetime` / `rotationOverLifetime` / `textureSheetAnimation` / `renderer`
- カーブ/グラデは LUT オフセットで保持（§3.2-1）。`version != 1` はロードエラー、未知キーは無視。

### 3.4 ランタイム層（ECS）

- `ParticleEmitterComponent`（エミッタ 1 つにつき生成。または内部で N エミッタを配列保持）：
  - アセット参照 `RefParticleSystemResource`
  - **SoA プール**（`maxParticles` で確保・swap-remove で圧縮）：
    `position[3] / velocity[3] / age / lifetime / seed(uint32) / initialSize / initialColor(RGBA) / rotation`
  - 再生状態：`playbackTime / emitAccumulator / aliveCount`、バースト発火済みフラグ
  - エミッタの原点は `HierarchicalTransformComponent` から取得（simulationSpace で解釈を変える）
- `ParticleSystem`（`ecs::SystemBase`）`Update()` の **順序（仕様 §5 準拠）**：
  1. `playbackTime += dt`、`emitterNormT` 算出（!looping はクランプ、looping は folding）
  2. **Emission**：`emitAccumulator += rateOverTime.Evaluate(emitterNormT) * dt`、`floor` 個 spawn。
     bursts は時刻交差で発火（**ループ折り返しを跨ぐフレームの取りこぼしに注意**＝仕様 §7.3 の警告）
  3. **Spawn**：shape から pos/dir、`initial.*` を `Evaluate(emitterNormT_spawn, r_item)` で確定し
     `lifetime/initialSize/initialColor/rotation/velocity(=dir*speed)/seed` を格納
  4. **Integrate**（生存粒子ごと、`u = age/lifetime`）：
     `age += dt` → 寿命超過は kill；
     `velocity += gravityDir * 9.81 * gravityModifier.Evaluate(emitterNormT) * dt`；
     velocityOverLifetime（Local/World）加算 → `position += velocity * dt`；
     `size = initialSize * sizeOverLifetime.Evaluate(u)`；
     `color = initialColor * colorGradientLUT(u)`；
     `rotation += rotationOverLifetime.Evaluate(u) * dt`；
     フリップブック frame（§3.5）
  5. **描画データ生成** → `RenderItem` を `RenderFrame.forwardItems` へ push
- `simulationSpace`：Local = 粒子をエミッタローカルで保持し描画時にワールド変換／
  World = ワールドで保持しエミッタ移動に追従しない。gravity/velocity の適用空間もこれに従う。
- **CPU シミュレーション先行**（既存 CPU 駆動設計に一致）。将来 compute/UAV
  （`IsComputeSupported()` ゲート）へ。スレッド境界（[../../設計書/05_マルチスレッド設計.md](../../設計書/05_マルチスレッド設計.md)）：
  sim=ゲームスレッド、GPU リソース生成=メイン、描画=レンダースレッド消費。

### 3.5 描画層（ビルボード / フリップブック / ストレッチ）

- 頂点：`position / uv（フリップブック小矩形）/ color(RGBA)`。`CameraData` の right/up で
  クアッド展開し、`rotation` はビュー軸まわりのロール。動的VBを毎フレーム更新（既存 `UpdateVertices` 流用）。
- `Particle.fx`（VS: 変換 / PS: texture × color）を追加。blend は `SetBlendModeCommand`
  （Alpha / Additive）。**Alpha かつ sortMode=ByDistance のみ視点距離降順ソート**（Additive は不要）。
- フリップブック UV（仕様 §7.9）：`frame = floor(startFrame + frameOverTime(u)*tilesX*tilesY*cycles) % 総数`、
  左上原点・行優先で小矩形 UV を算出。
- StretchedBillboard は velocity 方向＋`lengthScale`/`speedScale` で伸長。Mesh は P6（それまで Billboard 降格）。
- **テクスチャ解決**：`renderer.texture` は Unity の `Assets/` 相対。aq リソースパスへの対応付けは
  未決（§5 TODO）。

## 4. フェーズ計画

- [ ] **P1** Assimp 導入 + `FbxLoader::Loading()` 実装（static mesh）→ .fbx 表示
- [ ] **P2** スキン/アニメの FBX 対応（`SkeletalMeshResource` / bone / anim）
- [x] **P3**（2026-07-07 実装）`.particle` v1 の受け皿：`ParticleTypes`（ScalarValue/LUT/enum）+ `ParticleSystemData` +
  `ParticleLoader`（SimpleJson パース＋LUT 焼き込み）+ `AqParticleExporter.cs` + `Game/Assets/Particle/FX_Explosion.particle`。
  Engine.vcxproj/.filters・Application.cpp 登録済み。マイルストンの実ロード確認は P4 のインスペクタで行う
- [x] **P4** `ParticleEmitterComponent` + `ParticleSystem`（CPU sim・仕様 §5 準拠）+ ビルボード
  forward 描画（Alpha/Additive）。フリップブック/ストレッチ/overLifetime は最小。マイルストン：Sparks/Smoke が出る
  - [x] **P4a**（CPU sim）`ParticleRandom`/`ParticleEmitterComponent`（SoA プール・再生状態）/`ParticleSystem`
    実装・登録済み。emission→spawn→integrate（仕様 §5）、shape/initial/gravity/各 overLifetime/bursts を最小実装
  - [x] **P4b**（描画）`ParticleVertex`(pos/uv/color) + `Particle.fx`(手続き円×頂点カラー) + `RenderFrame.particleItems` +
    `RenderSystem::BuildRenderFrame` での収集 + `Renderer.cpp` 専用パス（`ParticleDrawCommand` が Alpha/Additive を設定）。
    テクスチャは未使用（§7.4 のパス解決待ち）。Playground.level に `FX_Test` を配置して確認
- [ ] **P5** overLifetime（color/size/rotation/velocity）+ shape 各種 + bursts + フリップブック +
  StretchedBillboard + 距離ソート
- [ ] **P6**（任意）Mesh renderer + GPU compute シミュレーション

## 5. 未確認・TODO

- [ ] Assimp の UWP/Xbox ビルド可否（現状は PC 限定前提）。
- [ ] TKM/TKA と FBX 直読みの座標系・スケール差（Unity は左手Y-up・cm 単位）の吸収方針。
- [ ] **テクスチャ/メッシュのパス解決規約**（仕様 §7.10）：Unity `Assets/xxx` → aq リソースパスの
  対応付け（エクスポータでリライトするか、マニフェストを持つか）。
- [ ] LUT サンプル数 64 の妥当性（急峻カーブ）。ステップは別処理なので当面 64 で進める。
- [ ] Gradient LUT のフォーマット（RGBA8 かハーフ float か：メモリ vs バンディング）。
- [ ] バーストのループ折り返し発火の取りこぼし対策（仕様 §7.3）。
- [ ] `ParticleRandomItem`（項目 ID）列挙の確定（TwoConstants/TwoCurves を持つ全項目）。

## 6. 新規ファイル（P3/P4）

いずれも追加時は [vs-project-files] 規約に従い vcxproj / GameSources.props / filters へ登録（フィルタ配置は要確認）。

| ファイル | 役割 |
|---|---|
| `aqEngine/Particle/ParticleTypes.h` | ScalarValue / Curve LUT / Gradient LUT / `ParticleRandomItem` / 各モジュール構造体 |
| `aqEngine/Resource/ParticleSystemData.h/.cpp` | イミュータブルなリソース（焼き込み済み）＋アクセサ |
| `aqEngine/Resource/ParticleLoader.h/.cpp` | `ResourceLoaderBase` 実装。SimpleJson パース＋LUT 焼き込み。bank/reflection 登録 |
| `aqEngine/Particle/ParticleRandom.h` | シード→ハッシュ、`Hash(seed ^ 項目ID)` ヘルパ |
| `aqEngine/Component/ParticleComponentSystem.h/.cpp` | ECS コンポーネント（SoA プール・再生状態）＋更新システム（`ecs::SystemBase`）を同居 |
| `Game/Assets/Shader/Particle.fx`（配置は要確認） | パーティクル VS/PS |
| `Unity/Editor/AqParticleExporter.cs` | Unity 側エクスポータ |
| `Game/Assets/Particle/FX_Explosion.particle` | ローダーテスト用サンプル（.particle アセット） |


## 7. 引き継ぎメモ（別PC / 次セッション用）

> 現在ブランチ: `feature/particle`。P3 + **P4（CPU sim + 描画）** まで実装・コミット済み。次は **P5**。
> リファクタ済み: Loader/Data は `aqEngine/Resource/` へ移動し `aq::res` に統一。Component/System は `Component/ParticleComponentSystem` に集約。

### 7.1 実装済み（P3・実在ファイル）

- `aqEngine/Particle/ParticleTypes.h` … 焼き込み済みデータ型。`ScalarValue::Evaluate(lutBase, t, r)`（4モード統一）、
  `SampleLut`、`ColorValue`、各モジュール構造体、`EmitterData`（`curveLutPool` を所有）、`RandomItem` enum
- `aqEngine/Resource/ParticleSystemData.h` … `aq::res::ResourceBase` 派生のイミュータブルリソース。`GetEmitter(i)` 等
- `aqEngine/Resource/ParticleLoader.h/.cpp` … `Loading()` で JSON パース＋Hermite→64サンプル LUT 焼き込み。`version==1` 検証
- `Game/Assets/Particle/FX_Explosion.particle` … Sparks+Smoke 2エミッタ
- `Unity/Editor/AqParticleExporter.cs` … `Tools > aqEngine > Export ParticleSystem`
- 登録: `Engine.vcxproj`/`.filters`（`ParticleTypes.h`→Particle フィルタ、他→Resource フィルタ）、
  `Game/Application/Application.cpp`（`RegisterBank` + `Reflection`）
- 使い方: `aq::res::ResourceManager::Get().Load<aq::res::ParticleSystemData>("path/xxx.particle")`

### 7.2 ビルド上の注意（別PCでも同じはず）

- **デスクトップ（Debug/x64）構成は既存の依存 `DirectXTex\DirectXTex.h` 未配置だとリンク前に失敗する**
  （この環境は `packages/directxtex_uwp.*` のみ）。P3 コードとは無関係。UWP/Xbox 構成、または
  desktop 版 DirectXTex を配置した環境でフルビルドすること。
- 新規 C++ ファイルは他の engine ファイルに合わせ **UTF-8 BOM 付き**（`.particle` は仕様 §2 で BOM なし）。
- `ParticleTypes.h` 単体は cl.exe で構文コンパイル通過確認済み（LUT/Evaluate ロジック）。

### 7.3a 実装済み（P4a・CPU sim・実在ファイル）

- `aqEngine/Particle/ParticleRandom.h` … `Hash(uint32)`（fmix32）＋ `RandomUnit(seed, item/salt)`（仕様 §5）
- `aqEngine/Component/ParticleComponentSystem.h/.cpp` … `aq::ecs::ParticleEmitterComponent`（`IComponent`）と
  `aq::ecs::ParticleSystem`（`SystemBase`）を同居（`AnimationComponentSystem` と同じ流儀）。
  エミッタごとに `ParticleEmitterRuntime`（SoA: position/velocity/age/lifetime/seed/initialSize/initialColor/
  rotation/size/color + aliveCount/playbackTime/emitAccumulator/spawnCounter、swap-remove 圧縮）。
  `SetAsset(path)` で `.particle` を非同期ロード → `IsCompleted` 後に runtimes 構築。`ParticleSystem::Update` は
  `Foreach<ParticleEmitterComponent, HierarchicalTransformComponent>` で `htc->transform.position` を原点に `Simulate`。
- 更新順序（§3.4／仕様 §5）: emission（rateOverTime）→ bursts → spawn（`Evaluate(emitterNormT, r)`）→
  integrate（`u=age/lifetime`：gravity/velocity/size/color/rotation overLifetime）。
- 登録: コンポーネント→`ECS/ComponentRegistry.cpp`（`ParticleEmitter`）、システム→`aqEngine/Core/Application.cpp::Register`
  （`AddDependency<ParticleSystem, HierarcicalTransformSystem>` / `AddDependency<RenderSystem, ParticleSystem>`）。
  `Engine.vcxproj`/`.filters` に追加（`ParticleRandom.h`→Particle、`ParticleComponentSystem`→Component フィルタ）。
- `Vector3`/`Vector4` にスカラー倍・成分積・複合代入演算子を追加（`Math/Vector.h`）。
- **未着手（P4a 範囲外）**: 描画。simulationSpace は World 前提で保持（Local はエミッタ回転無視）。
  velocityOverLifetime.space も未考慮。フリップブック frame は未計算。

### 7.3b 実装済み（P4b・描画・実在ファイル）

- `Rendering/RenderFrame.h` … `ParticleVertex`（pos/uv/color）と `ParticleRenderItem`（動的VB/静的IB/vs/ps/indexCount/additive）、
  `RenderFrame::particleItems` を追加。
- `Game/Assets/Shader/Particle.fx` … `VSMain`（`b0` の view/proj で変換、world=Identity）/`PSMain`（uv 距離の柔らかい円 × 頂点カラー）。
  テクスチャ非依存なので Unity のパス解決を待たず表示できる。
- `Component/ParticleComponentSystem.{h,cpp}` … `FillParticleItems(out, camRight, camUp)` を追加。エミッタごとに動的VB/静的IB を
  遅延生成（maxParticles ぶん）、生存粒子をカメラ right/up でクアッド展開（rotation はビュー軸ロール）して VB を更新し
  `ParticleRenderItem` を積む。shader は `Assets/Shader/Particle.fx` を lazy ロード。
- `Component/BodyComponentSystem.cpp`（`RenderSystem::BuildRenderFrame`）… `Foreach<ParticleEmitterComponent>` を追加。
  カメラ右/上は `Camera::GetViewMatrixInverse()` の行（`_11.._13`=right, `_21.._23`=up）から取得。
- `Rendering/ParticleDrawCommand.{h,cpp}` + `Renderer.cpp` … 海の後・ポストプロセスの前に専用パス。`ParticleDrawCommand` が
  `OMSetBlendMode(Additive/AlphaBlend)` を自前設定して描画し、最後に `Opaque` へ戻す（forward は Opaque 固定のため）。
- **未対応（P5 以降）**: テクスチャ（Unity `Assets/…` パス解決＝§7.4）、フリップブック、StretchedBillboard、距離ソート、
  simulationSpace のエミッタ回転、velocityOverLifetime.space。サンプルは非ループ一発なのでレベル読み込み時に一度だけ再生。

### 7.4 未決（P4 で判断が要る）

- `renderer.texture` の Unity `Assets/…` → aq リソースパス解決（仕様 §7.10、本書 §5 TODO）
- バーストのループ折り返し発火の取りこぼし（仕様 §7.3）
- simulationSpace=Local/World の保持空間と描画変換の扱い（本書 §3.4）
