# IBL(Image Based Lighting)設計

> 対象コミット: b82f314(P1 実装はこの次のコミット) / 最終更新: 2026-11-11(P1 完了。P2 以降は未実装)

対象: `Tools/IBLGen/` + `aqEngine/Assets/Sky/` の IBL 用 DDS 3 本 + `aqEngine/Rendering/IBL/` +
`aqEngine/Assets/Shader/PBRLighting.fx` / `PBRFunctions.fx` + `DeferredRenderer` / `DeferredLightingCommand` + `PipelinePresets`。
**全バックエンド(D3D11 / D3D12 / Vulkan / Metal)**で動かす。Mac で確認できるのは Metal / Vulkan。

一次資料の分担:
- **本書** … IBL の方針決定・新規/変更ファイルの責務・フェーズ計画・チェックリスト。
- [Skybox設計.md](Skybox設計.md) … スカイキューブ(IBL の入力)の生成と描画。キューブマップの読み込み経路・深度の罠は向こうが正。
- [エンジン不足機能メモ.md](エンジン不足機能メモ.md) §5 … 本機能を入れる判断(「エンジンを作品として仕上げる」方針)。
- [レンダーパイプライン設計.md](レンダーパイプライン設計.md) … パス列方式。本書はパスを足さない(既存の Lighting パスの中身を変える)。

---

## 0. 現状と方針

### 0.1 現状(実コードで確認)

- **環境光は定数 1 色だけ。** `PBRFunctions.fx` の `ComputePBRLighting` 冒頭が
  `ambient.color * ambient.intensity * baseColor * (1.0 - metallic)` を足している
  (コメントに「Phase 3 で IBL に置換予定」)。**金属には環境光が一切乗らず**、鏡面反射は直接光のハイライトだけ。
  `AmbientLight` の既定は `(0.1, 0.1, 0.1) × 1.0`(`Graphics/Lighting.h`)。
- **`ComputePBRLighting` の呼び出し元は `PBRLighting.fx` の `PSMain` だけ**(grep で確認)。
  フォワードのシェーダ(`ModelLit` / `SkeletalModelLit` / `TerrainLit` / `OceanLit`)は `Lighting.fx` の
  Blinn-Phong で、PBR ではない。
- **スカイキューブは在る。** `aqEngine/Assets/Sky/DefaultSkyCube.dds`(512² × 6 面 / BGRA8 / ミップ 1 枚。
  `Tools/SkyCubeGen/gen_skycube.py` の生成物)。`SkyRenderer` が `ResourceManager` で非同期ロードする。
- **ミップの自動生成は「DDS がミップ 1 枚のときだけ」**(`Resource.cpp` の `TextureLoader::Loading`、
  `if (info.mipLevels == 1) GenerateMipMaps`)。**ミップを全段持つ DDS はそのまま使われる**ので、
  前置フィルタ済みのミップを DDS に焼けば壊されない。
- `R16G16B16A16_FLOAT` の DDS は `PixelFormatFromDXGI` が `PixelFormat::R16G16B16A16_Float` に写す(`Resource.cpp`)。
- **GPU でキューブマップを生成する抽象は無い。** `RenderTargetDesc` は 2D のみ(配列・キューブ・ミップ指定なし)、
  UAV は `IRenderTarget::GetRenderTargetUAV()` と `IGpuBuffer::AsUAV()` だけで、
  **キューブの面 × ミップごとに書き込む手段が 4 バックエンドとも無い**。
- シェーダの空きスロット(`PBRLighting.fx` の PS): **t5〜t7 が空き**(t4 = シャドウ、t8〜t11 = GBuffer)、
  **b2 が空き**(b0 = PerDraw、b1 = LightingCB、b3 = ShadowCB。b2 はマテリアル用だがライティングパスでは未使用)、
  **s0 が空き**(s1 = `shadowSampler`。`PBRLighting.fx` は GBuffer を `Load` で読むのでサンプラを使っていない)。
  Metal の binding シフト(b:0 / t:8 / s:0 / u:24)では t5〜t7 → 13〜15 で、b / u と重ならない。
- `python3` に numpy は無い(`gen_skycube.py` も標準ライブラリのみ)。

### 0.2 確定した方針

| 項目 | 決定 | 理由 |
|---|---|---|
| 手法 | **split-sum 近似**(Karis 2013): 拡散 = irradiance キューブ、鏡面 = 前置フィルタ済みキューブ(ミップ = roughness)× BRDF LUT | 実時間 PBR の標準。既存の GGX / Smith 項(`PBRFunctions.fx`)とそのまま整合させられる |
| 生成場所 | **オフライン(Python ツール)で DDS を生成し、生成物をコミットする** | GPU 生成にはキューブ RT かミップ別 UAV の抽象を 4 バックエンドに足す必要があり(§0.1)、IBL 本体より大きい。空は静的なので実行時生成の利点が薄い。スカイキューブと同じ流儀(`gen_skycube.py`)で揃う。実行時生成は §5 課題 4 に残す |
| ツールの依存 | **標準ライブラリのみ**(`multiprocessing` で並列化) | numpy が無い環境。`gen_skycube.py` と同じ前提 |
| 生成物 | ① irradiance キューブ 32² / ② 鏡面キューブ 128²・**ミップ 6 段**(128→4)/ ③ BRDF LUT 128² の 2D。**3 本とも `R16G16B16A16_FLOAT`** | 鏡面の荒いミップと LUT は 8bit だと段が出る。`PixelFormat` に RG16F が無いので LUT も 4ch(B・A は 0) |
| 入力の色空間 | **スカイキューブの値を変換せずそのまま積分する**(sRGB → linear 変換をしない) | `Skybox.fx` が DDS の値をそのままシーン RT(リニア HDR)へ出している。映り込みと見えている空を一致させるには同じ解釈にする |
| irradiance の格納値 | **`E / π`**(Lambert で albedo 1 の出射輝度)。シェーダは `irradiance × baseColor × kD` | 一様な空 `L` で irradiance = `L` になり、自己テストで確かめやすい |
| 鏡面のミップ対応 | `mip = roughness × (mipCount − 1)`(線形)。フィルタと LUT の GGX は **`alpha = max(roughness², 0.001)`** | `PBRFunctions.fx` の `EvalDisneyBRDF` と下限まで同じ定義にする(LUT の roughness 最小の行で差が出ないように) |
| LUT の可視項 | **height-correlated Smith**(`V_SmithGGX` と同じ式) | 直接光と環境光で可視項が違うと、同じ材質が光源の種類で質感を変える |
| シェーダの切り替え | **`PBRLighting.fx` にエントリ `PSMainIBL` を足し、IBL が使えるときだけそちらを使う**。従来の `PSMain` は出力を変えない | 「IBL 無効 / 未ロード」で t5〜t7 を未バインドのまま `TextureCube` を宣言すると、Metal のフォールバックは 2D テクスチャ(`MetalRenderContextImpl::CreateFallbackResources`)で型が合わない。エントリを分ければ未使用リソースはそのエントリに現れない。**IBL 無効時は従来と画素一致**を回帰確認にできる |
| 置き場所(実行時) | `aqEngine/Rendering/IBL/IBLEnvironment` を新設し、`DeferredRenderer` が `shared_ptr` で持つ | IBL を使うのはディファードライティングだけ。空(`SkyPass`)とは寿命も使う側も別 |
| 有効化 | `RendererPreset::enableIBL`(**P2 では既定 false**)。P3 で既定を true にするかをユーザー確認で決める | `RendererPreset` は「既定値のまま渡せば従来と同じ絵になる」が約束(`PipelinePresets.h` のコメント)。既定を変えると AquaDash の絵が変わる |
| 対象構成 | **Standard(ディファード)のみ**。Mobile / フォワードは対象外 | フォワードは Blinn-Phong で PBR ではない(§0.1)。§5 課題 3 |

### 0.3 落とし穴(実装前に読むこと)

1. **ミップ 1 枚の DDS は読み込み時にミップが自動生成される**(§0.1)。鏡面キューブは**必ず全段(6 段)を DDS に書く**。
   irradiance と LUT はミップ 1 枚で書く(自動生成されても `SampleLevel(..., 0)` で読むので害は無い。
   ただし float16 キューブの自動生成が Mac の DirectXTex で通るかは P2 のロードログで確かめる)。
2. **キューブの面の向きは `gen_skycube.py` の書き出しと同じ規約で読む。** IBL ツールは
   DDS の面順(+X, −X, +Y, −Y, +Z, −Z)と各面の (u, v) → 方向の対応を `gen_skycube.py` から写す
   (独自に書くと面が入れ替わっても一様な空の自己テストでは検出できない)。P1 で「鏡面 mip0 と元の空の同じ方向の画素」を比べて確かめる。
3. **`.fx` にエントリを足したら `shader_entries.txt` にも足す**(Skybox設計 §1。忘れると Vulkan / Metal でシェーダが見つからない)。
4. **`ComputePBRLighting` のシグネチャを変える**ので、呼び出し元(`PBRLighting.fx` のみ。§0.1)を同時に直す。
5. IBL は**影も遮蔽も受けない**。軒下や車体の下も空の明るさで照らされる。遮蔽は SSAO(不足機能メモ §5)で掛ける前提とし、本書では扱わない(§5 課題 2)。

### 0.4 確認対象(P2 着手後に判明。2026-11-11)

- **Sample の箱はディファード PBR を通らない。** `BoxStaticMeshComponent` は `StaticMesh::ShaderType::SimpleBox`
  (`SimpleBox.fx`。ライティングなしのフォワード、G-Buffer PS なし)で初期化する(`BodyComponentSystem.cpp`)。
  IBL はディファードライティングにしか掛からないので、Sample では IBL の有無で絵が変わらない。
- **PBR の G-Buffer PS は基本色を必ず t0 から読む**(`PBRGBuffer.fx` の `baseColorTex.Sample`。単色の CB 値は無い)。
  エンジンのアセットに既定の白テクスチャも球などのモデルも無い(`aqEngine/Assets/` は Shader と Sky だけ)。
- そこで箱に PBR モードを足し(§1、P2-0)、基本色は 1x1 テクスチャで与える。`StaticMesh::SetTextureOverride` と
  `GraphicsDevice::CreateTexture2D` は既に在るので、抽象 IF への追加は要らない。

---

## 1. 責務

| ファイル | 責務 |
|---|---|
| `Tools/IBLGen/gen_ibl.py` **(新規)** | スカイキューブ DDS(BGRA8 / RGBA8 の非圧縮キューブ。`gen_skycube.py` の出力形式)を読み、§2 の 3 本を DDS(DX10 ヘッダ、`R16G16B16A16_FLOAT`)で書き出す。`--selftest` で §2.5 の自己テストを実行する。ビルドには組み込まず、生成物をコミットする(`gen_skycube.py` と同じ理由)。空を差し替えたら手で再実行する |
| `aqEngine/Assets/Sky/DefaultSkyCube_Irradiance.dds` **(新規・生成物)** | 32² × 6 面 / ミップ 1 / `R16G16B16A16_FLOAT`。値は `E / π` |
| `aqEngine/Assets/Sky/DefaultSkyCube_Specular.dds` **(新規・生成物)** | 128² × 6 面 / ミップ 6(128, 64, 32, 16, 8, 4)/ `R16G16B16A16_FLOAT`。mip k は roughness = k / 5 |
| `aqEngine/Assets/Sky/BRDF_LUT.dds` **(新規・生成物)** | 128² の 2D / ミップ 1 / `R16G16B16A16_FLOAT`。u = NdotV、v = roughness、rg = (A, B)(`F0 × A + B`) |
| `aqEngine/Rendering/IBL/IBLEnvironment.{h,cpp}` **(新規)** | 3 本の非同期ロード要求・サンプラ(MinMagMipLinear / Clamp)の生成・`IsReady()`(3 本のロード完了と SRV、2 本のキューブが `isCubemap` であること)・強度の保持。ロード結果を `[ibl]` の接頭辞で 1 回だけログに出す(`SkyRenderer::LogCubemapOnce` と同じ流儀)。**生成・ロードに失敗しても落とさない**(従来のライティングで続行) |
| `aqEngine/Rendering/Deferred/DeferredRenderer.{h,cpp}` | `PBRLighting.fx` の `PSMainIBL` を追加で生成する(失敗しても続行し、IBL を使わない)。`SetIBLEnvironment(std::shared_ptr<IBLEnvironment>)` を持つ(**呼ぶのはパイプライン構築時だけ**。§1.1)。`BuildLightingCommandList` で IBL が `IsReady()` なら `PSMainIBL` と **`IBLEnvironment` の `shared_ptr`**・強度の値を、そうでなければ従来どおり `PSMain` をコマンドへ渡す |
| `aqEngine/Rendering/Deferred/DeferredLightingCommand.{h,cpp}` | IBL 用の引数(`std::shared_ptr<const IBLEnvironment>` と強度の値。IBL を使わないときは null)を受ける。使うときは `Execute` で `fc.perDrawCBPool->Allocate()` から b2 用の CB を取り `IBLCBData` を書き、t5〜t7 に SRV、s0 にサンプラをバインドし、描画後に t5〜t7 を解除する |
| `aqEngine/Rendering/Pipeline/PipelinePresets.{h,cpp}` | `RendererPreset` に `enableIBL`(既定 false)と 3 本のパス(既定はエンジン所有の §1 の 3 本)を足す。`Standard()` で `enableDeferred && enableIBL` のとき `IBLEnvironment` を生成して `DeferredRenderer` に渡す。`Mobile()` は触らない |
| `aqEngine/Assets/Shader/PBRFunctions.fx` | `ComputePBRLighting` の冒頭の簡易 ambient を外し、引数 `float3 environment`(環境光の寄与)を足す。IBL の評価関数 `EvalIBL`(§3)を足す(テクスチャは引数で受け、宣言は持たない) |
| `aqEngine/Assets/Shader/PBRLighting.fx` | t5〜t7・s0・b2(`IBLCB`)を宣言し、エントリ `PSMainIBL` を足す。`PSMain` は従来の簡易 ambient を計算して `environment` に渡す(**出力は変更前と同じ**) |
| `aqEngine/Assets/Shader/shader_entries.txt` | `PBRLighting.fx PSMainIBL ps` を 1 行足す(ヘッダコメントのエントリ総数も更新) |
| `aqEngine/Rendering/Deferred/Debug/`(既存の `DeferredDebugPanel`) | P3: IBL の ON/OFF・強度のスライダ |
| `aqEngine/Component/BodyComponentSystem.{h,cpp}`(`BoxStaticMeshComponent`) | P2-0: `SetPBRMaterial(float metallic, float roughness)` を足す。**初期化前(最初の `Update` より前)に呼んだときだけ**、`SimpleBox` の代わりに `StaticMesh::ShaderType::PBRLit` で初期化し、`SetMetallic` / `SetRoughness` を反映する。基本色は `color_` の 1x1 テクスチャ(`R8G8B8A8_Unorm`、`GraphicsDevice::CreateTexture2D`)を作り `SetTextureOverride(TextureSlot::Albedo, ...)`(t0)で差す(PBR の G-Buffer シェーダは基本色を必ず t0 から読むため。§0.4)。PBR モードで初期化後に `SetColor` したら 1x1 テクスチャを作り直して差し替える。**箱を集める処理(`BodyComponentSystem.cpp` の `Foreach<BoxStaticMeshComponent>`。現状は無条件に `frame.forwardItems` へ積む)を、`StaticMeshComponent` と同じ振り分けにする**: `PBRLit` は `item.gbufferPS` があるときだけ `frame.items`、無い間(ロード中)は積まない。それ以外は従来どおり `forwardItems`。呼ばなければ従来どおり `SimpleBox`(既存の利用者の絵は変わらない) |
| `Sample/Application/SampleModule.cpp` | P3: metallic × roughness を振った PBR の箱の列(見本) |
| `Engine.vcxproj` / `.filters` | `IBLEnvironment.{h,cpp}` の登録(`vs-project-files` スキル。**フィルターは実装前にユーザーへ確認する**) |

`IBLCBData`(`IBLEnvironment.h` に置く。**192 byte 固定**):

```cpp
// perDrawCBPool(= sizeof(VSConstantBuffer) = 192B)から Allocate するため 192B に固定する。
// UpdateSubresource → UpdateConstantBuffer はバッファのサイズ分を読むので、小さい構造体を渡すと範囲外を読む。
// SkyCBData(SkyCommand.h)と同じ作法。PBRLighting.fx の cbuffer IBLCB と先頭を一致させること。
struct IBLCBData
{
    float         intensity      = 1.0f;  // 環境光(拡散 + 鏡面)全体の倍率
    float         specularMaxMip = 5.0f;  // 鏡面キューブのミップ数 - 1(ロード後の TextureData から取る)
    float         _pad0          = 0.0f;
    float         _pad1          = 0.0f;
    math::Vector4 pad[11];                // 192B へのパディング
};
static_assert(sizeof(IBLCBData) == 192, "IBLCBData must match perDrawCBPool slot size (192B)");
```

`specularMaxMip` は固定値にせず、ロード後の `TextureData::desc.mipLevels - 1` から取る(空を差し替えて段数が変わっても合う)。

### 1.1 スレッドと寿命

- `IBLEnvironment` の生成(ロード要求・サンプラ生成)はメインスレッドのパイプライン構築時(`PipelinePresets::Standard()`)。
  `IsReady()` と `BuildLightingCommandList` はコマンドリストを作る側(メインスレッド)で呼ぶ。
- **コマンドは `IBLEnvironment` を `shared_ptr` で保持する。** `Execute` はレンダースレッドで後のフレームに走るので、
  `DeferredRenderer` 側の保持だけだと差し替え・破棄の後に古い SRV を使いうる。`shared_ptr` を持たせれば、
  実行待ちのリストが残る間は環境(= `RefGPUResource` 3 本とサンプラ)が生きる
  (`RenderThread::Finalize` は未実行リストを join 後に破棄する)。
- `IBLEnvironment` の 3 本とサンプラは `Create` 後に差し替えない(強度だけが変わり、これは値でコマンドへ写す)。
  `SetIBLEnvironment` は構築時に 1 回だけ呼ぶ前提とし、実行時の差し替えは本書の範囲外。
- CB はレンダースレッドの `Execute` 内で `fc.perDrawCBPool` から取る(フレームスロットごとのプールなので、GPU 実行中の CB を上書きしない)。

---

## 2. 生成ツール(`Tools/IBLGen/gen_ibl.py`)

使い方(予定):

```
python3 Tools/IBLGen/gen_ibl.py aqEngine/Assets/Sky/DefaultSkyCube.dds aqEngine/Assets/Sky/DefaultSkyCube
#   → DefaultSkyCube_Irradiance.dds / DefaultSkyCube_Specular.dds を出力
python3 Tools/IBLGen/gen_ibl.py --lut aqEngine/Assets/Sky/BRDF_LUT.dds
#   → 空に依存しない LUT。1 回作れば空を差し替えても作り直さない
python3 Tools/IBLGen/gen_ibl.py --selftest
```

### 2.1 前処理

- 入力キューブ(512²)を 2×2 平均で縮小したミップ列(512 → 4)をツール内で作る。以降の積分はこれを使う。
- 面の向き・面順は `gen_skycube.py` から写す(§0.3-2)。

### 2.2 irradiance

1. 64² のミップで、各テクセルの立体角で重み付けして **SH 9 係数(L2)** へ射影する。
2. cos ローブで畳み込む(Ramamoorthi & Hanrahan 2001 の係数 π, 2π/3, π/4)。
3. 32² × 6 面の各テクセル方向で評価し、`/ π` して書く。

全テクセル × 全テクセルの直接積分は標準ライブラリでは遅すぎる。SH なら一様な空で厳密に `L` が戻る。

### 2.3 鏡面(前置フィルタ)

- mip 0(roughness 0)= 入力の 128² ミップをそのまま書く。
- mip k ≥ 1: roughness = k / 5、`alpha = max(roughness², 0.001)`。Karis の **N = V = R 近似**で、Hammersley 列 + GGX 重点サンプリング
  (サンプル数 256)、`NdotL` で重み付け。**サンプルごとに PDF から元ミップを選ぶ**(filtered importance sampling。
  Křivánek & Colbert 2008)ことで、少ないサンプルでも明るい太陽が点々にならないようにする。

### 2.4 BRDF LUT

- 128²。u = NdotV((i + 0.5) / 128)、v = roughness((j + 0.5) / 128)。行 j = 0 が roughness 最小(テクスチャの上端)。
- GGX 重点サンプリング 512 サンプル。可視項は `V_SmithGGX` と同じ height-correlated Smith。
  `G_Vis = Vis × 4 × NdotL × VdotH / NdotH`、`A += (1 − Fc) × G_Vis`、`B += Fc × G_Vis`(`Fc = (1 − VdotH)^5`)。

### 2.5 自己テスト(`--selftest`)

| 対象 | 条件 | 許容差 |
|---|---|---|
| irradiance | 一様な空 `L = (0.2, 0.5, 0.8)` → 全テクセルが `L` | 各成分 1% 以内 |
| 鏡面 | 一様な空 → 全ミップの全テクセルが `L` | 各成分 1% 以内 |
| 鏡面の向き | 上半球だけ 1、下半球 0 の空 → mip 0 の +Y 面中央が 1、−Y 面中央が 0 | 0.01 以内 |
| LUT | 全テクセルで `A ≥ 0`、`B ≥ 0`、`A + B ≤ 1.0` | +0.01 まで |
| LUT | roughness 最小の行で NdotV → 1 のとき `A + B` → 1(鏡面は損失が無い) | 最右列で 0.98 以上 |
| DDS | 3 本のヘッダ(DX10、format = 10、キューブフラグ、ミップ数)とファイルサイズ | 一致 |

---

## 3. シェーダ

`PBRLighting.fx` に足す宣言:

```hlsl
TextureCube  gIblIrradiance : register(t5);
TextureCube  gIblSpecular   : register(t6);
Texture2D    gIblBrdfLut    : register(t7);
SamplerState gIblSampler    : register(s0);

cbuffer IBLCB : register(b2)
{
    float iblIntensity;
    float iblSpecularMaxMip;
    float2 _iblPad;
};
```

`EvalIBL`(`PBRFunctions.fx`。F0 は直接光と同じ定義):

```hlsl
// specular(引数)= 材質の F0 スケール(GBuffer2.w)。結果の変数名と衝突させない
float3 F0        = lerp(0.08 * specular * globalSpecularScale, baseColor, metallic);
float  NdotV     = max(dot(N, V), 1e-5);
float3 R         = reflect(-V, N);
float2 ab        = brdfLut.SampleLevel(s, float2(NdotV, roughness), 0).rg;
float3 specEnv   = specCube.SampleLevel(s, R, roughness * maxMip).rgb;
float3 Fr        = F0 + (max(1.0 - roughness, F0) - F0) * SchlickFresnel(NdotV);  // roughness 考慮の Fresnel
float3 kD        = (1.0 - Fr) * (1.0 - metallic);
float3 diffIBL   = irrCube.SampleLevel(s, N, 0).rgb * baseColor * kD;
float3 specIBL   = specEnv * (F0 * ab.x + ab.y);
return (diffIBL + specIBL) * intensity;
```

- `PSMainIBL` は `EvalIBL(...)` を、`PSMain` は従来の `ambient.color * ambient.intensity * baseColor * (1.0 - metallic)` を
  `ComputePBRLighting` の `environment` に渡す。**IBL 有効時は `AmbientLight` を使わない**(二重に足さない)。
- 2 つのエントリの差は環境光の 1 項だけにし、GBuffer 読み取り・影は共通の関数に括り出す。

---

## 4. フェーズ計画

### P1: 生成ツールと生成物

実装:
- `Tools/IBLGen/gen_ibl.py` と §1 の DDS 3 本(生成物をコミット)。エンジンのコードは触らない。

評価(自動)(2026-11-11):
- [x] `--selftest` が §2.5 の全項目を通る(一様な空の最大相対誤差 0.0244% = half の丸め / 半球の空 +Y 1.0000・−Y 0.0000 /
      LUT max(A+B) 1.00037 / 行 0 最右列 A+B 1.00000 / DDS ヘッダ 3 本一致。加えて `gen_skycube.py` の `face_direction` と 30 点照合して不一致 0)
- [x] 鏡面 mip 0 と入力の空を、6 面それぞれの中央と 4 隅付近の方向で比べ、差が 2/255 以内(面の向きの取り違えが無い)
      → 最大 0.062/255(`--verify`)
- [x] 実際の空から作った鏡面キューブで、**+Y 面(空の上半分)の画素分散が mip 1 → 5 で単調に減る**(roughness が上がるほどぼける。
      直接光や Fresnel が混ざらないツールの段階で確かめる)→ 4.87e-3 → 4.41e-3 → 3.35e-3 → 2.05e-3 → 1.24e-3
- [x] 生成時間を記録する(目安: 全体で 5 分以内。超えるならサンプル数か並列化を見直す)
      → 鏡面 + irradiance 3.8 秒、LUT 1.8 秒(10 並列)。再生成でバイト一致(決定的)

評価(ユーザー確認):
- [x] ツールが書き出す確認用 PNG(各ミップの 6 面展開図。スクラッチパッドへ出し、コミットしない)を見て、ミップが進むほど滑らかにぼけ、太陽が点々になっていない
      (`--png DIR` で出力する。2026-11-11 ユーザー確認済み)

#### P1 で決めたこと・分かったこと

- **面の向き**は `gen_skycube.py` の `face_direction` をそのまま写し、逆写像 `direction_to_face_uv` を足した。自己テストで実体と照合している。
- **ミップ選択**(§2.3)は `lod = 0.5·log2(Ωs/Ωp) + 1`(Ωp は入力 512² のテクセル立体角、pdf = D/4)。隣接 2 段を線形補間。
  入力の参照は面内の双線形で、**面の端はクランプして隣の面へ跨がない**(荒いミップの継ぎ目は今のところ目立たない)。
- irradiance の SH 打ち切りで負になる値は 0 で止める(実データでは発生しない)。
- **LUT は roughness 1 の浅い角度で A+B が大きい**(NdotV 0.05 で 0.85、NdotV 1 で 0.31)。半球の総当たり積分と 3 桁一致したので
  生成の誤りではなく、シェーダの BRDF(height-correlated Smith の `V_SmithGGX` + GGX)そのものの性質。
- ツールの追加オプション: `--verify <sky> <prefix> [--lut PATH] [--png DIR]`、`--png DIR`、`--jobs N`。

### P2-0: 箱の PBR モード(確認用の土台)

P2 の実装中に、**Sample の箱が `SimpleBox`(ライティングなしのフォワード)で描かれ、ディファード PBR を通らない**ことが分かった
(§0.4)。このままでは IBL の効果も回帰も Sample で確かめられないので、P2 の前に箱へ PBR モードを足す(2026-11-11 ユーザー判断)。
P2 と別にコミットし、P2 の画素比較の基準をこのコミットで撮る。

実装:
- `BoxStaticMeshComponent::SetPBRMaterial` と、箱の描画項目の振り分け(§1 の該当行)。

評価(自動。Mac / Metal。2026-11-11):
- [x] ビルドが通り、警告が増えない(`BodyComponentSystem` に警告なし)
- [x] `SetPBRMaterial` を呼ばない Sample(現状のまま)が、P2-0 の前のコミットと画素一致する(P2 の比較条件のうち、オーバーレイの除外と撮影タイミング)
      → 2560×1504 の撮影でオーバーレイ矩形 (0,0)-(460,200) を除く 3,758,240 画素が差 0。同じビルドの撮り直しも差 0(撮影の揺らぎなし)
- [x] Sample の箱に一時的に `SetPBRMaterial(0.0f, 0.5f)` を与えると、見えている 3 面の平均色が互いに異なり(ライティングが効く)、
      3 面とも成分の大小が `SetColor` の色と同じ順になる(Sample の色 (0.2, 0.6, 1.0) なら B > G > R)
      → 上面 (23,88,134)・左面 (12,52,92)・右面 (2,16,32)
- [x] **G-Buffer を通っていることの確認**(ディファード有効のまま): Debug 構成でデバッグパネルの Deferred(`DeferredDebugPanel`)を開き、
      GBuffer0 のプレビューに箱(基本色 (0.2, 0.6, 1.0) の領域)が出る。一時差分を外した `SimpleBox` のままではプレビューに箱が出ない。
      **この確認だけ一時差分を `SetPBRMaterial(1.0f, 0.5f)` にする**(プレビューは GBuffer0 をそのまま `ImGui::Image` に渡すので、A = metallic が透明度になり metallic 0 の箱は見えない)。
      プレビューの撮影と操作は memory の ImGui 検証手順に従う。
      (`enableDeferred = false` で消えることは確認に使えない: `ForwardPass` は深度が無い構成で `frame.items` も描くため)
      → PBR の箱はプレビューの箱の画素が (52,153,255) = 基本色 ×255 と一致。`SimpleBox` のままではプレビューに箱が出ない
- [x] Metal API Validation エラー 0 / 終了コード 0(ウィンドウの閉じるボタンで終了。`kill` や System Events の Cmd+Q では測れない)

### P2: 実行時の組み込み

実装:
- `IBLEnvironment`・`RendererPreset` の項目・`DeferredRenderer` / `DeferredLightingCommand` の配線・
  `PBRFunctions.fx` / `PBRLighting.fx`・`shader_entries.txt`・`.vcxproj` 登録。
- 確認のため、Sample の箱に一時的に `enableIBL = true` と `SetPBRMaterial(1.0f, 0.2f)` を与える(P3 で正式な見本に置き換える)。

**画素一致の比較条件**(P2・P3 共通):
- 基準画像は **P2-0 のコミット**に、「Sample の箱を PBR にする一時差分」(`SetPBRMaterial(1.0f, 0.2f)`)を当てて撮る。
  比較対象は P2 の実装に**同じ一時差分**を当て、`enableIBL = false` にして撮る。材質の差が比較に混ざらないようにする。
- Sample は固定カメラ・同じ解像度・同じバックエンドで撮る。撮影は非同期ロードの完了後に行う。完了の目印は、
  基準画像と `enableIBL = false` では `[sky] cubemap ... srv=ok` の 1 行(IBL は生成されないので `[ibl]` は出ない)、
  `enableIBL = true` ではそれに加えて `[ibl]` のロード結果の行。
- **FPS オーバーレイ(`AppHost.cpp` の `##FPSOverlay`。左上 (10,10) から始まる自動サイズの窓)の矩形は比較から除く**。
  除く矩形は撮影画像から実測して評価記録に書く。オーバーレイ以外の全画素で差 0 を合格とする。

評価(自動。Mac / Metal と Mac / Vulkan の両構成):
- [ ] ビルドが通り、警告が増えない
- [ ] ロードログで 3 本とも `srv=ok`、2 本のキューブが `isCubemap=1`、鏡面が `mips=6`、irradiance と LUT の mips を記録する(§0.3-1)
- [ ] **`enableIBL = false` で、基準画像と画素一致**(上の比較条件。オーバーレイ以外で差 0)
- [ ] `enableIBL = true` で、金属の箱に空が映る(箱の上面が空の天頂色に、側面が地平の色に寄る。撮影して画素の色相で確認)
- [ ] IBL のファイルを一時的に存在しないパスへ向けても起動し、従来の絵(`PSMain`)で描かれる。ログに失敗が 1 回だけ出る
- [ ] Metal API Validation エラー 0(`METAL_DEVICE_WRAPPER_TYPE=1`)/ VK の validation エラー 0 / 終了コード 0

評価(ユーザー確認):
- [ ] 金属・非金属の箱の見た目が破綻していない(真っ白に飛ぶ・真っ黒に沈む・面ごとに色が不連続、が無い)

### P3: 調整と見本

実装:
- `DeferredDebugPanel` に IBL の ON/OFF(`PSMain` / `PSMainIBL` の切り替え)と強度スライダ。
- Sample に metallic(0 / 1)× roughness(0, 0.25, 0.5, 0.75, 1)の PBR の箱(`SetPBRMaterial`)の列を置き、`enableIBL = true` にする。
- 強度の既定値を決める。
- `RendererPreset::enableIBL` の既定を true にするか(= AquaDash の絵が変わる)をユーザーに確認し、決めた方を反映する。

評価(自動):
- [ ] デバッグ UI で OFF にした画と、`enableIBL = false` で起動した画が画素一致する(P2 の比較条件。どちらも見本列の状態で撮る)

評価(ユーザー確認):
- [ ] Sample の見本列の見た目(roughness を上げるほど映り込みがぼけ、質感の段階が分かる。
      画面上の分散は直接光のハイライトや Fresnel も混ざるので自動判定にしない。ぼけの単調性は P1 でツール側に確かめてある)
- [ ] AquaDash のステージで、既定を true にした場合の見た目(明るさ・コントラストの変化を許容できるか)

Windows(D3D11 / D3D12)の確認は Windows 機の宿題に積む(`windows-regression-pending` と同じ扱い)。

---

## 5. オープン課題

1. **空の色調(`SkyRenderer::SetTint`)に IBL が追従しない。** 空の rgb を変えても映り込みは生成時の色のまま。
   合わせるなら `IBLCB` に tint を足して掛ける(線形なので掛けるだけで一致する)。P3 で要否を判断。
2. **IBL は遮蔽されない**(§0.3-5)。SSAO を入れたら拡散と鏡面の両方に掛ける(鏡面は specular occlusion の近似)。
3. **Mobile / フォワードは対象外。** フォワードの PBR 化は別件。
4. **実行時生成(GPU)。** 動的な空・時間帯変化をやるなら必要。キューブ RT かミップ別 UAV の抽象を 4 バックエンドに足すのが前提。
5. **入力が LDR(BGRA8)。** 太陽が 1.0 で頭打ちになり、映り込みのハイライトが弱い。空を HDR にする話(Skybox設計 §4 課題 3)と一緒に扱う。
6. **太陽の向きとディレクショナルライトの不一致**(Skybox設計 §4 課題 1)が、IBL で映り込みとしても見えるようになる。

---

## 6. チェックポイント

- [ ] P1 の評価を全て満たした
- [ ] P2 の評価を全て満たした
- [ ] P3 の評価を全て満たした
- [ ] 抽象 IF(`IRenderContextImpl` / `IGraphicsDeviceImpl`)への追加が 0 本
- [ ] `shader_entries.txt` に `PSMainIBL` が登録されている
- [ ] 新規 `.h/.cpp` が `.vcxproj` / `.filters` に登録されている
- [ ] IBL の生成・ロードに失敗しても起動できる
- [x] [エンジン不足機能メモ.md](エンジン不足機能メモ.md) §5 の IBL の行を本書へのリンクに置き換えた(2026-11-11)
- [ ] 設計と実装の食い違いを本書へ反映し、`対象コミット` を更新した
