# FBX 読み込み・ParticleSystem 導入 設計（メモ・第2版）

> 対象コミット: 674a35a / 最終更新: 2026-07-08
>
> Particle 側は [particleフォーマット仕様v1.md](particleフォーマット仕様v1.md) が一次資料（フォーマットは同書が正）。
> 本書はそれを **どう実装に落とすか**（データ表現・ランタイム・描画・フェーズ）を決める設計メモ。

> Unity で作成した **FBX モデル**と **ParticleSystem** を本エンジン（aq）で使えるようにする。
> 既存の Sprite 描画（UIBatchRenderer）とモデル描画（StaticMesh / SkeletalMesh → RenderFrame）
> の資産を最大限流用する。
> **進捗: FBX（§2・ufbx）と Particle P3〜P5（§7）は実装済み。残りは Particle P6（任意）。**

## 0. 決定事項

- **FBX 方式**（実装済み）: **ufbx**（単一ヘッダ・`ThirdParty/ufbx/`）でランタイム直読み。
  静的/スキン/アニメを `MeshLoader`/`SkeletalMeshLoader`/`AnimationLoader` の `.fbx` 分岐で処理する（§2）。
  当初案の Assimp / FBX SDK・PC 限定二段構えは不採用（ufbx は単一ヘッダで UWP でも積める）。
- **Particle 取り込み**: **Unity から独自 JSON（`.particle`）をエクスポート**して読む。
  Unity の `.prefab` YAML 直接パースは採用しない（バージョン差・フォーマット変化に弱いため）。
- **配置**: 新規モジュールは `aqEngine/Particle/`（本ディレクトリ）。FBX 側は既存 `aqEngine/Resource/` に追記。

## 1. 現状資産と再利用方針

| 既存資産 | 場所 | 本件での役割 |
|---|---|---|
| FBX ローダー（ufbx） | [../Resource/Resource.cpp](../aqEngine/Resource/Resource.cpp) `LoadFbxStaticMesh` 他 | **実装済み**。`MeshLoader`/`SkeletalMeshLoader`/`AnimationLoader` が `.fbx` を分岐 |
| `MeshResource` / `MeshData` | [../Resource/Resource.h:160](../aqEngine/Resource/Resource.h#L160) | FBX → `VertexData`/`indices`/`MaterialTexturePaths` の受け皿（既存） |
| `SkeletalMeshResource` / `BoneData` | [../Resource/Resource.h:295](../aqEngine/Resource/Resource.h#L295) | スキン FBX の bone / inverseBindPose 受け皿（既存） |
| TKM/TKA ローダー | [../Resource/Resource.cpp](../aqEngine/Resource/Resource.cpp) `MeshLoader`/`SkeletalMeshLoader` | 出荷パス（変換済みバイナリ）。維持 |
| 非同期ロード基盤 | [../Resource/Resource.h](../aqEngine/Resource/Resource.h) `ResourceLoaderBase` / `ResourceManager` | FBX/Particle 双方の Load 経路に流用 |
| 動的頂点バッファ | [../Graphics/StaticMesh.h:70](../aqEngine/Graphics/StaticMesh.h#L70) `InitializeDynamic`/`UpdateVertices` | パーティクルのビルボード VB を毎フレーム更新 |
| forward 透明パス | [../Rendering/RenderFrame.h:145](../aqEngine/Rendering/RenderFrame.h#L145) `forwardItems` | パーティクル（半透明）の描画キュー |
| ブレンド切替 | [../Rendering/SetBlendModeCommand.h](../aqEngine/Rendering/SetBlendModeCommand.h) | Additive / Alpha の切替 |
| スプライトバッチ | [../UI/Rendering/UIBatchRenderer.h](../aqEngine/UI/Rendering/UIBatchRenderer.h) | クアッド生成・アトラス処理の**実装雛形**（UI はスクリーン空間なので流用は雛形まで） |
| JSON | [../Util/SimpleJson.h](../aqEngine/Util/SimpleJson.h) | `.particle` のロード |
| カメラスナップショット | [../Rendering/RenderFrame.h:23](../aqEngine/Rendering/RenderFrame.h#L23) `CameraData` | ビルボード展開の right/up |

> 新規に本当に必要なのは **①FBX パーサ結線**、**②`ParticleSystemData` + JSON ローダー**、
> **③`ParticleEmitterComponent` + `ParticleSystem`（CPU sim）+ ビルボード描画**の3点。
> 生成/描画の実体は既存の Resource / RenderFrame / 動的VB を使う。

## 2. FBX 対応（**実装済み**・ufbx）

> ライブラリは **ufbx**（単一ヘッダ・`ThirdParty/ufbx/`）を採用。当初案の Assimp / FBX SDK は不採用。
> `.fbx` は既存の各ローダの `Loading()` 内で拡張子分岐し、そのまま `SetModelPath("...fbx")` で表示できる。
> 未使用だった `FbxLoader` スタブと旧 FBX SDK コメントは削除済み。

### 2.1 実装マップ（すべて [../Resource/Resource.cpp](../aqEngine/Resource/Resource.cpp) 内）

| 用途 | 関数 | 入口ローダー | 出力 |
|---|---|---|---|
| 静的メッシュ | `LoadFbxStaticMesh` | `MeshLoader`（`.fbx` 分岐） | `MeshData`（`VertexData`/`indices`/albedo パス） |
| スキン（スケルタル） | `LoadFbxSkeletalMesh` | `SkeletalMeshLoader` | `SkeletalMeshData`（bone/inverseBindPose/skin weight） |
| アニメ | `LoadFbxAnimation`（`path.fbx#index` / `#clipName` でクリップ選択） | `AnimationLoader` | `AnimationClipData` |
| クリップ名列挙（エディタ用） | `GetFbxAnimationClipNames` | — | クリップ名配列 |

### 2.2 座標系・取り込みの要点（実装準拠）

- `ufbx_load_opts.target_axes = ufbx_axes_left_handed_y_up` で **DirectX 左手 Y-up** へ正規化。
- 単位変換（`target_unit_meters`）は**使わない**（手元 FBX は単位メタデータが不整合で約 1/100 に潰れるため）。
- UV は V 反転（FBX 左下原点 → DirectX 左上）、行列式が負の geom-to-world は winding 反転。
- 取り込み基準回転 `FbxImportBasis()` に座標系補正を集約（**ここを変えると全 FBX の向きが変わる**）。
- テクスチャパス解決は `FbxResolveTexturePath`（ufbx 絶対 → FBX 同階層 相対 → ファイル名のみ、の順で実在を探索）。

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
  （`IsComputeSupported()` ゲート）へ。スレッド境界（[../../設計書/05_マルチスレッド設計.md](05_マルチスレッド設計.md)）：
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

- [x] **P1**（実装済み）**ufbx** 導入 + `MeshLoader` の `.fbx` 分岐（`LoadFbxStaticMesh`）→ static mesh の .fbx 表示
- [x] **P2**（実装済み）スキン/アニメの FBX 対応（`LoadFbxSkeletalMesh`：bone/inverseBindPose/weight、
  `LoadFbxAnimation`：`#index`/`#clipName` クリップ選択、`GetFbxAnimationClipNames`）
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
- [x] **P5**（2026-07-07 実装）overLifetime（color/size/rotation/velocity）+ shape 各種 + bursts は P4a で完了。
  本フェーズで **テクスチャ対応**（`renderer.texture` を Unity の `Assets/…` パスのまま `Load<GPUResource>`、
  `Particle.fx` に `PSTextured` 追加、`ParticleRenderItem.texture/samplerState` + `ParticleDrawCommand` で t0/s0 束縛。
  未指定/ロード失敗時は手続き円 PS へフォールバック）、**フリップブック**（§7.9 の小矩形 UV を CPU で頂点へ焼き込み）、
  **距離ソート**（Alpha かつ `sortMode=ByDistance` のみ遠い順）、**StretchedBillboard**（速度方向 × `lengthScale`/`speedScale`）を実装。
  `DirectX.sln` Debug/x64 フルビルド成功。
- [x] **P6**（2026-07-08 実装・実機検証済み）**Mesh renderer**（+ Unity 互換対応は §7.3e）：
  - **P6a**（エクスポータ）`renderMode==Mesh` のメッシュを `Meshes/<name>.obj` へ出力し `renderer.mesh` に書く + `Meshes/` へ同梱。
  - **P6b**（engine）`MeshLoader` に `.obj` 分岐（`LoadObjMesh`：v/vt/vn/f、UV は V 反転、巻き順そのまま）。
  - **P6c**（engine）`FillMeshEmitter`：Mesh エミッタは `renderer.mesh`（`MeshResource`）を粒子ごとに
    `scale(size)×Rz(rotation)×R(emitter euler)×T(worldPos)` で変換し動的VBへ展開。テクスチャ/加算は billboard と共通。
    ロード未完了のうちは描画しない（板を出さない）。
  - **要ランタイム検証/既知の限界**：①巻き順（Unity CW前面=DX CULL_BACK 前提。裏面カリングで消えたら反転が必要）
    ②オイラー回転順（`XMMatrixRotationRollPitchYaw` ≠ Unity ZXY で回転オーラの向きがずれ得る）
    ③エフェクト根の回転は未適用（エミッタ `localRotationEuler` のみ）。GPU compute は未着手。
  - **既存 .particle は要再エクスポート**（P6a 追加前は `renderer.mesh` が空でメッシュが出ない）。

## 5. 未確認・TODO

- [x] FBX ライブラリ選定・座標系/スケール吸収 → **ufbx で実装済み**（§2.2。単位変換は不整合のため不使用、
  基準回転 `FbxImportBasis` に集約）。
- [x] **テクスチャのパス解決規約**（仕様 §7.10.1）：`"Assets/…"` はコンテンツルート相対、それ以外は **`.particle` 相対**。
  エクスポータは参照テクスチャを出力先 `Textures/` へフラットコピーし `texture` を `"Textures/<file>"` に書き換える
  （自己完結バンドル）。engine 側は `ParticleEmitterComponent::ResolveTexturePath` で解決。メッシュ（`renderer.mesh`）は P6 で同方針。
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

> 現在ブランチ: `feature/particle`。P3 + P4（CPU sim + 描画）+ **P5（テクスチャ/フリップブック/ソート/Stretched）** まで実装済み。次は **P6（任意）** か FBX の P1。
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

- **`Engine.vcxproj` を単体で msbuild すると `DirectXTex\DirectXTex.h` が見つからず失敗する。**
  原因は依存欠落ではなく `$(SolutionDir)` 未定義（インクルードパスが `$(SolutionDir)ThirdParty`）。
  実体は `ThirdParty/DirectXTex/` に同梱済み。**`DirectX.sln` をビルドする**か、
  単体なら `/p:SolutionDir=<repo>\DirectX\` を渡すこと。
  （※旧版メモの「DirectXTex 未配置で失敗」は誤り。Debug/x64 フルビルドは通る。）
- 新規 C++ ファイルは他の engine ファイルに合わせ **UTF-8 BOM 付き**（`.particle` は仕様 §2 で BOM なし）。

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
- **未対応（P4b 時点）**: テクスチャ、フリップブック、StretchedBillboard、距離ソート → **いずれも P5 で実装済み（§7.3c）**。
  残: simulationSpace のエミッタ回転、velocityOverLifetime.space。サンプルは非ループ一発なのでレベル読み込み時に一度だけ再生。

### 7.3c 実装済み（P5・テクスチャ/フリップブック/ソート/Stretched）

- `Rendering/RenderFrame.h` … `ParticleRenderItem` に `texture` / `samplerState` を追加。
- `Game/Assets/Shader/Particle.fx` … `Texture2D albedo : t0` / `SamplerState smp : s0` と
  **`PSTextured`**（`albedo.Sample(smp,uv) * color`）を追加。`PSMain`（手続き円）は据え置き。
- `Rendering/ParticleDrawCommand.{h,cpp}` … `item.texture` があれば t0/s0 を束縛。
- `Component/ParticleComponentSystem.{h,cpp}` …
  - `EnsureTextures()`: `renderer.texture`（**Unity の `Assets/…` をそのまま**）を `Load<res::GPUResource>`。
    s0 サンプラ（linear/clamp）を 1 個生成。**未指定/ロード失敗なら手続き円 PS へフォールバック**。
  - `FillParticleItems(out, camRight, camUp, camForward, camPos)` へ拡張（呼び出し元 `BodyComponentSystem.cpp` も更新）。
  - フリップブック: `frame = floor(startFrame + frameOverTime(u)*tiles*cycles) % 総数`、左上原点・行優先で小矩形 UV。
    **クアッドの v を反転修正**（左下=vMax / 左上=vMin）。手続き円は対称なので見た目不変。
  - 距離ソート: Alpha かつ `sortMode=ByDistance` のみ、カメラからの平方距離で遠い順に並べて頂点生成。
  - StretchedBillboard: 縦軸 = 速度方向 ×(`size*lengthScale + speed*speedScale*0.5`)、横軸 = `cross(dir, camForward)`。
- 検証: `DirectX.sln` Debug/x64 **フルビルド成功**（`Game.exe` 生成）。
- テクスチャ素材: **プレースホルダを手続き生成して配置済み** — `Game/Assets/FX/spark.png`（128² 暖色グロー）/
  `Game/Assets/FX/smoke_sheet.png`（256² = 4×4 フリップブック）。サンプル `.particle` の `renderer.texture` と一致。
  差し替え自由（アーティスト素材を同じ相対パスで置けばよい）。
- **実行時の実描画は未確認**（この環境ではゲーム描画まで走らせられないため）。ビルドは通る。

### 7.3d 修正 (2026-07-08・ちらつき/頂点破れの解消)

- **深度**: パーティクルは `DepthMode::ReadOnly`(テストのみ・書き込みOFF)で描き、描画後に既定へ戻す
  (`ParticleDrawCommand`)。半透明が Z を塗って背後の粒を四角く切る問題を解消。
- **放出順序**: `StepEmitter` を「積分・寿命死 → 放出」(Unity と同順)に変更。maxParticles 上限に
  張り付く常駐エミッタ(リング/グロー)が死亡フレームに補充されず 1 フレーム明滅する問題を解消。
- **VB 書き込みをレンダースレッドへ移動**(最重要): `ParticleRenderItem.vertices`(CPU 頂点、shared_ptr)を
  運び、`ParticleDrawCommand::Execute` が描画直前に `vb->Update`。ゲームスレッドから Map していた旧実装は
  **D3D11=immediate context 競合 / D3D12=フレームインデックス不一致**で頂点破れ・ちらつきの真因だった
  (スレッド契約 §5「描画 API はレンダースレッドのみ」違反)。

### 7.3e Unity 互換対応 (2026-07-08・市販 VFX での検証で追加)

Eric VFX Studio の `FX_Fireball` / `FX_LightPillar` を実機比較して潰した差分。
**エクスポート時の `warnings` 配列が非対応機能の一覧になっている — 見た目が違うときはまず warnings を読むこと。**

| 対応 | 内容 | 主なファイル |
|---|---|---|
| TGA テクスチャ | `.tga` → `LoadFromTGAFile` 分岐 | `Resource/Resource.cpp` |
| 加算の検出強化 | `DetectBlend`: シェーダ名 "additive" / `_DstBlend==One` / URP `_Blend==2` | `AqParticleExporter.cs` |
| **D3D12 Additive のα反映** | PSO を `SrcAlpha,One` に(D3D11/Vulkan と同意味論)。`One,One` だと白RGB+α形状テクスチャが**白い四角**に | `D3D12PipelineStateCache.cpp` |
| Mesh renderer (P6) | エクスポータが `Meshes/<name>.obj` 出力 + `LoadObjMesh` + `FillMeshEmitter`。ロード完了まで描かない | エクスポータ / `Resource.cpp` / `ParticleComponentSystem.cpp` |
| 3D Start Size | `initial.size3D {x,y,z}`。粒 size を **Vector3 化**。ビルボード=x横/y縦、メッシュ=xyz | 同上一式 |
| Separate Axes | `sizeOverLifetime.size3D {x,y,z}`(軸別倍率カーブ)。ビーム板が寿命中に萎む | 同上 |
| 3D Start Rotation | `initial.rotation3D {x,y,z}`(度)。粒 rotation を **Vector3 化**。メッシュは RollPitchYaw(z→x→y=**Unity Euler と同順**) | 同上 |
| マテリアル Tiling/Offset | `renderer.uvScale/uvOffset`。**V はエクスポータで左下→左上原点へ変換**。細帯/1装飾の切り出し再現 | 同上 |
| エミッタ回転の sim 適用 | `localRotationEuler` を shape 生成位置/初速方向と `velocityOverLifetime(space=Local)` に適用(X=270° エミッタの「ローカル+Z」=ワールド上、を再現) | `ParticleComponentSystem.cpp` |
| localPosition 適用 | エミッタのルート相対オフセットを生成位置に加算(従来は無視) | 同上 |

**検証ノウハウ(重要)**:
- 加算エフェクトは**明るい空バックだと白飛び**して全部白く見える。**暗い背景**(夜/日陰/地形背負い)で確認する。
- `looping=false` + burst@t=0 の**ワンショット構成**が多い。Enter(再生し直し)**直後の 0.5 秒**を見る。
- エクスポータ変更後は「最新 .cs を Unity `Assets/Editor/` へコピー → 再エクスポート」が必要。
  ランタイム変更のみなら再エクスポート不要。ゲーム起動中は Game.exe のリンクが失敗する(閉じてからビルド)。

### 7.4 未決 / 残課題(Unity 参考画像との既知の残差)

- **色味**: Unity 参考(FX_LightPillar)はオレンジの柱。うちは白っぽい → まず**暗背景で再確認**。
  それでも白いなら `grad_lb` 等の startColor / テクスチャ RGB を追う(スキャンは輝度のみ確認済み)。
- **エミッタ間の描画順**: `.particle` 定義順で描いている。Alpha 同士の前後や加算の重なり順が Unity と異なり得る。
- **エンティティ(エフェクト根)回転の未適用**: エミッタ回転は適用済みだが、Entity の Transform 回転はまだ粒に効かない。
- **lensflare(pzpl.obj)が「点の格子」に見える**: 小クアッド群メッシュの UV/向きの詰めが必要か、Unity 側の見た目と要比較。
- rotationOverLifetime の Separate Axes(現状 z のみ)/ バーストのループ折り返し取りこぼし(仕様 §7.3)
- GPU compute シミュレーション(任意)
