# FBX 読み込み・ParticleSystem 導入 設計（メモ・初版）

> 対象コミット: 586d40b / 最終更新: 2026-07-06

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

## 3. ParticleSystem 対応（Unity 風・JSON エクスポート方式）

### 3.1 パイプライン

```
Unity ParticleSystem
   │  C# エクスポータ（1本自作 / モジュール値をリフレクションで書き出し）
   ▼
xxx.particle（独自 JSON / Unity モジュールをミラー）
   │  SimpleJson でロード
   ▼
ParticleSystemData（リソース）
   │  ParticleEmitterComponent が参照
   ▼
ParticleSystem（ecs System）= CPU sim → ビルボード RenderItem → RenderFrame.forwardItems
```

### 3.2 データ層 `ParticleSystemData`（リソース）

Unity のモジュールをミラーする。取り込む主な値:

- **Main**: duration / loop / startLifetime / startSpeed / startSize / startColor /
  gravityModifier / maxParticles / simulationSpace（Local/World）
- **Emission**: rateOverTime / bursts
- **Shape**: Cone / Sphere / Box / Circle（生成位置・初速方向）
- **over Lifetime**: velocity / color（グラデーション）/ size / rotation（カーブ）
- **Renderer**: Billboard / Stretched / Mesh、テクスチャパス、blend（Additive / Alpha）

> カーブ/グラデーションは Unity の `AnimationCurve` / `Gradient` をキーポイント配列で JSON 化 →
> エンジン側で線形/エルミート評価。

### 3.3 ランタイム層（ECS）

- `ParticleEmitterComponent`: アセット参照 + 実行時パーティクル（SoA: position/velocity/life/size/color/rotation）
  + emit アキュムレータ + 再生時刻。既存 `StaticMeshComponent` と同流儀。
- `ParticleSystem`（`ecs::SystemBase`）の `Update()`:
  1. Emit（rate + burst、shape から初期 pos/vel）
  2. Integrate（velocity・gravity・over-lifetime カーブ、寿命減算、死んだ粒の除去）
  3. ビルボード頂点生成 → `RenderItem` を組み `RenderFrame.forwardItems` へ push
- **CPU シミュレーション先行**（既存の CPU 駆動設計に一致）。後で compute/UAV
  （`IsComputeSupported()` ゲート）へ拡張可能。
- スレッド境界（[../../設計書/05_マルチスレッド設計.md](../../設計書/05_マルチスレッド設計.md)）: sim=ゲームスレッド、
  GPU リソース生成=メイン、描画=レンダースレッド消費。

### 3.4 描画層（ビルボード）

- `CameraData` の right/up でクアッド展開、動的VBを毎フレーム更新（既存 `UpdateVertices` 流用）。
- `Particle.fx`（VS: 変換 / PS: texture × color）を追加。Additive/Alpha は `SetBlendModeCommand`。
- Alpha ブレンドは視点距離ソートが必要（Additive は不要）。

## 4. フェーズ計画

- [ ] **P1** Assimp 導入 + `FbxLoader::Loading()` 実装（static mesh）→ .fbx 表示
- [ ] **P2** スキン/アニメの FBX 対応（`SkeletalMeshResource` / bone / anim）
- [ ] **P3** `ParticleSystemData` + `.particle` JSON ローダー + Unity C# エクスポータ
- [ ] **P4** `ParticleEmitterComponent` + `ParticleSystem`（CPU sim）+ ビルボード forward 描画
- [ ] **P5** over-lifetime カーブ / shape / burst / アルファ距離ソート
- [ ] **P6**（任意）GPU compute シミュレーション

## 5. 未確認・TODO

- [ ] Assimp の UWP/Xbox ビルド可否（現状は PC 限定前提）。
- [ ] TKM/TKA と FBX 直読みの座標系・スケール差（Unity は左手Y-up・cm 単位）の吸収方針。
- [ ] Unity C# エクスポータの出力スキーマ確定（`AnimationCurve`/`Gradient` の表現）。
- [ ] パーティクルのメッシュ描画モード（Mesh renderer）を初版に含めるか。
