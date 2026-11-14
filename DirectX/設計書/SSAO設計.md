# SSAO(Screen Space Ambient Occlusion)設計

> 対象コミット: 1fd4004 / 最終更新: 2026-11-14(設計。承認済み。P1 から実装)

対象: `aqEngine/Rendering/SSAO/` + `aqEngine/Rendering/Pipeline/Passes/SSAOPass.*` + `aqEngine/Assets/Shader/SSAO.fx` / `SSAOBlur.fx` /
`SSAOComposite.fx` + `PBRFunctions.fx`(IBL の拡散部分の括り出し)+ `PipelinePresets` + デバッグパネル。
**全バックエンド(D3D11 / D3D12 / Vulkan / Metal)**で動かす。Mac で確認できるのは Metal / Vulkan。

一次資料の分担:
- **本書** … SSAO の方針決定・新規/変更ファイルの責務・フェーズ計画・チェックリスト。
- [SSR設計.md](SSR設計.md) … 「ライティングを変えずに差分だけを加算する」合成の作法(§0.2)・`WasIBLUsed()`・worldPos の half 精度と誤差幅(§0.1 / §0.5)・
  画素比較の手順(§3)。本書はそれを使う側として差分だけを書く。
- [IBL設計.md](IBL設計.md) … `EvalIBL` の式・`IBLCB`・`PSMainIBL`。
- [レンダーパイプライン設計.md](レンダーパイプライン設計.md) … パス列方式とパス間契約。
- [エンジン不足機能メモ.md](エンジン不足機能メモ.md) §5 … 本機能を入れる判断。

---

## 0. 現状と方針

### 0.1 現状(実コードで確認)

- **環境光はライティングの 1 項にまとまっている。** `PBRLighting.fx` の `PSMain` は `environment = ambient.color × ambient.intensity × baseColor × (1 − metallic)`、
  `PSMainIBL` は `EvalIBL(...)` = `(diffIBL + specIBL) × intensity` を `ComputePBRLighting` の先頭で足す(`PBRFunctions.fx`)。
  `diffIBL = irradiance(N) × baseColor × kD`、`kD = (1 − Fr) × (1 − metallic)`。**金属(metallic = 1)は拡散の環境光が 0**。
- **ライティング後に差分を加算する前例がある**(SSR の合成。[SSR設計](SSR設計.md) §0.2)。`BlendMode::Additive` は `src.rgb × src.a + dst`、
  シーン RT は `R16G16B16A16_Float` で負の加算はクランプされない(SSR P1 で Metal / Vulkan 実測済み)。
  合成 PS のスロットは `PBRLighting.fx` と同じ(b1 = LightingCB、b2 = 合成の CB、t5〜t7 / s0 = IBL、t8〜t11 = GBuffer0〜3)。
- **そのビューのライティングが IBL を使ったかは `DeferredRenderer::WasIBLUsed()` で分かる**(同じビューの後続パスから読む。SSR が使っている)。
- **深度は worldPos(GBuffer2)からしか取れない**(half 精度の絶対座標。[SSR設計](SSR設計.md) §0.1)。背景は pixelTag(GBuffer3.a)< 0.5。
  **フォワードで描いた物は G-Buffer に載らない**(AquaDash の道路など。[被写界深度設計](被写界深度設計.md) §0.3)。
- **compute の SRV のスロット数はバックエンドで違う。** D3D12 の compute ルートシグネチャは **t0〜t2 の 3 枚・u0〜u1**(`D3D12RootSignature.h` の
  `CS_SRV_TABLE_SIZE = 3`)で、`D3D12RenderContextImpl::CSSetShaderResource` は範囲外のスロットを**黙って捨てる**。Vulkan の SRV は t0〜t11
  (`VulkanRenderContextImpl.h` の `MAX_SRV = 12`。グラフィクスと compute 共通)、Metal は t0〜t15。
  → **SSR のトレース(`SSRTraceCommand` が t3 に GBuffer3 を置く)は D3D12 で t3 が届かない**。README「既知の課題」に記録する(本書では直さない)。
- 使える RT の形式は `R8G8B8A8_Unorm` / `R16G16B16A16_Float` / `R32_Float` / `R32G32B32A32_Float` など(`GraphicsTypes.h` の `PixelFormat`)。1 チャンネルは `R32_Float` だけ
  (DoF の CoC が UAV で使っている)。
- Mobile 構成(`PipelinePresets::Mobile`)は G-Buffer を持たない。
- Sample の見本列は上段(metallic 0)・下段(metallic 1)の 2 段で、床は金属(metallic 1、roughness 0.05)。**床と下段には拡散の環境光が無いので AO は出ない**。
  上段の箱どうしは 0.5 の隙間で並ぶ(`SampleModule.cpp`)。

### 0.2 確定した方針

| 項目 | 決定 | 理由 |
|---|---|---|
| 何に掛けるか | **拡散の環境光だけ**(`PSMain` の ambient 項、`PSMainIBL` の `diffIBL × intensity`)。直接光・自己発光・鏡面の環境光(IBL の鏡面キューブ・SSR)には掛けない | AO は「環境から届く光の遮られ方」の近似で、直接光の遮蔽は影が担う。鏡面に掛けると SSR の差分合成(キューブの項を差し引く式)と食い違うので、鏡面の遮蔽は §5 課題 1 に回す |
| 合成のしかた | **ライティングは変えない。** SSR と同じく差分だけを加算する: `rgb = −(1 − ao) × 拡散の環境光`、**α = 1**、`BlendMode::Additive`。拡散の環境光は合成 PS が G-Buffer から**ライティングと同じ式で計算し直す** | ライティングのシェーダとエントリを増やさずに済む(`PBRLighting.fx` に AO の SRV を足すと、Vulkan / D3D12 のグラフィクス SRV は t0〜t11 しか無く、空いているのは材質用の t0〜t3 だけ)。SSAO を外せば(パス除外・デバッグで OFF・生成失敗)絵は今と完全に同じ。負の加算がクランプされないことは SSR P1 で実測済み |
| 環境光の式の共用 | `PBRFunctions.fx` の `EvalIBL` から拡散部分を **`EvalIBLDiffuse(N, V, baseColor, metallic, roughness, specular, irrCube, s)`**(`irradiance × baseColor × kD`。強度は掛けない)に括り出し、`EvalIBL` と合成 PS が共用する。**`EvalIBL` の結果は変えない**(SSR の `EvalIBLSpecular` と同じ作法。P1 で画素一致を確かめる)。ambient の式は 1 行なので合成 PS に同じ式を書き、`PBRLighting.fx` 側に「`SSAOComposite.fx` と一致させること」と注記する | 差し引く値とライティングが足した値がずれると、ao = 1 でも絵が変わる |
| IBL の有無 | 合成 PS は **`PSMain`(ambient)と `PSMainIBL`(IBL の拡散)の 2 エントリ**。同じビューの `DeferredRenderer::WasIBLUsed()` で選ぶ(SSR と同じく判断はライティングの 1 回だけ) | `PSMain` に TextureCube を持ち込まない([IBL設計](IBL設計.md) §0.2 と同じ理由)。IBL のロード完了をまたぐフレームでも、ライティングと合成の判断が食い違わない |
| 置く位置 | **`DeferredLightingPass` の直後、`SSRPass` の前**(Standard のみ)。View scope | ライティング済みのシーンに差分を足す。SSR のトレースは AO の掛かったシーンの色を拾う(映り込みにも AO が乗る)。SSR の差分は鏡面だけなので AO と独立 |
| AO の求め方 | **worldPos と法線を直接使う半球の積分(Alchemy AO。McGuire et al. 2011)**。compute、全解像度、§0.3 | 深度から位置を復元する手法(Crytek 式・HBAO)と違い、worldPos がそのまま使える。サンプル点を投影し直す必要も無い |
| ノイズとぼかし | 画素ごとにサンプルの円盤を回す(interleaved gradient noise)。その後、**法線と平面距離で重みを付けた 5×5 のぼかし**(compute、§0.4) | 少ないサンプル数の縞をノイズに変え、ぼかしで均す。境界(別の面)をまたいでぼかさない |
| compute の SRV | 各 compute は **入力 3 枚(t0〜t2)・出力 1 枚(u0)まで** | D3D12 の compute は t0〜t2 しか届かない(§0.1) |
| RT | 生の AO と、ぼかした AO の 2 枚。**`R32_Float`・全解像度**。リサイズは `PrepareResize` / `CommitResize` / `AbortResize`(SSR と同じ) | 1 チャンネルの形式が `R32_Float` しか無い(§0.1)。半解像度は §4 |
| 対象にしない物 | フォワード(半透明・海・パーティクル・AquaDash の道路)は **AO を受けず、AO を落とさない**。Mobile 構成は対象外 | G-Buffer に載っていない(§0.1)。§5 課題 2 |
| 有効化 | `RendererPreset::enableSSAO`(**既定 false**)。`enableDeferred` が前提(IBL は不要)。パラメータは `SSAOPass::GetSSAORenderer()` の setter | 「既定値なら従来と同じ絵」を守る。ゲームで使うかは P3 で決める |
| 実行時の調整値 | パラメータは**コマンド生成時に値で写す**(メインスレッド)。シェーダ・RT はレンダラーが所有し、コマンドは生ポインタとハンドルで参照する | SSR / DoF と同じ |
| 生成失敗 | シェーダの生成に失敗したら `SSAORenderer::Create()` が false を返し、`PipelinePresets` は `SSAOPass` を足さない。RT が作れなければ何も積まない | SSR と同じ。どちらも絵は SSAO なしと同じ |

### 0.3 AO の計算(`SSAO.fx`)

画素 p の worldPos を `P`、法線を `N`、ビュー空間の深度を `z`(`view` 行列で変換した z。前方が正)とする。背景(pixelTag < 0.5)は `ao = 1` を書いて終わる。

1. **画面上の半径**: `rPx = radius × projScaleY × (viewH / 2) / w`。`projScaleY` は射影行列の y の拡大率(`projection._22`)、
   `w` はクリップ空間の w(`w = projection._34 × z + projection._44`。透視投影なら z、正射影(`Camera::SetOrthographic`)なら 1)。
   C++ 側は `projection._22` / `_34` / `_44` を CB に写す(投影の種別を別に持たない)。`rPx < 1` なら `ao = 1`。`rPx` は `maxScreenRadius`(既定 64 画素)で頭打ち。
2. **サンプル点**: `sampleCount`(既定 12)個を、黄金角の螺旋(i 番目の半径 `rPx × sqrt((i + 0.5) / sampleCount)`)に置き、
   画素ごとの回転角 `2π × IGN(p)`(`IGN` = interleaved gradient noise)で全体を回す。**`IGN` に渡すのはビュー内の画素座標**(矩形の原点を引いた値)、
   テクスチャの参照は原点を足した RT 全体の座標(`SSRTrace.fx` と同じ)。同じカメラ・同じ寸法のビューなら、どの位置のビューでも同じノイズになる
   (§2 P2 の分割画面の評価①の前提)。サンプル画素はビュー矩形の中に丸める。
3. **各サンプルの寄与**: サンプル画素が背景なら 0。それ以外は `v = Q − P`(`Q` はサンプル画素の worldPos)、
   `term = max(0, dot(v, N) − bias) / (dot(v, v) + ε) × falloff`。
   - `bias = max(biasScale × z, 4 × e)`。`e` は P の half 精度の誤差幅([SSR設計](SSR設計.md) §0.5 と同じ式 `2^-10 × max(|P.x|, |P.y|, |P.z|) × 2`)。平面上の自己遮蔽を防ぐ。
   - `falloff = saturate(1 − dot(v, v) / radius²)`。半径の外(奥行きの離れた物)は寄与しない(ハロー対策)。
   - `ε = 0.01 × radius²`。
4. **結果**: `ao = saturate(1 − (2 × intensity / sampleCount) × Σ term) ^ power`。

既定値(P2 で調整し、変えたら本書へ反映する): `radius = 0.5`(ワールド単位)、`intensity = 1.0`、`power = 1.0`、`biasScale = 0.002`、
`sampleCount = 12`、`maxScreenRadius = 64`。

### 0.4 ぼかし(`SSAOBlur.fx`)

中心画素 c の周り 5×5(c を含む 25 画素。ビュー矩形の中だけ)で、生の AO を重み付き平均する。背景の画素は使わない(中心が背景なら 1 を書く)。

- `wN = saturate(dot(N_c, N_q)) ^ 8`(法線が大きく違う面をまたがない)。
- `wP = saturate(1 − |dot(N_c, P_q − P_c)| / (planeTolerance × z_c + 4 × e_c))`(中心の面の平面から離れた画素を使わない。`planeTolerance` 既定 0.05)。
- 結果 `= Σ(ao_q × wN × wP) / Σ(wN × wP)`(中心は wN = wP = 1 なので分母は 1 以上)。

入力は t0 = 生の AO、t1 = GBuffer1(法線)、t2 = GBuffer2(worldPos)。pixelTag は読まず、**生の AO が背景で 1 を書いていること**と worldPos = 0 の判定で背景を見分ける
(t0〜t2 に収めるため。背景の worldPos は 0 で、原点ちょうどの G-Buffer 画素と区別できない点は §5 課題 4)。

### 0.5 落とし穴(実装前に読むこと)

1. **合成 PS の出力 α は必ず 1。** 加算は `src.rgb × src.a + dst`。
2. **差し引く環境光はライティングと同じ式・同じ入力で求める。** `PSMainIBL` 側は `EvalIBLDiffuse` を共用し、強度は IBL の CB の値(コマンド生成時に写す。SSR と同じ)を使う。
3. **合成は `WasIBLUsed()` でエントリを選ぶ**(IBL を使ったビューで `PSMain` を使うと、ambient を差し引いて誤る)。
4. **compute は 3 SRV まで**(§0.1)。4 枚目を足したくなったら設計に戻す。
5. **行列の規約は `MotionBlur.fx` / `SSRTrace.fx` に合わせる**(`mul(M, v)`。C++ 側は `Mull(view, projection)` をそのまま渡している)。
6. **ビューポート。** View scope。compute はビュー矩形の範囲だけを dispatch し(CB に矩形を渡し、シェーダで原点を足す)、サンプルも矩形の中に限る。
   合成はパイプラインが設定したビューのビューポートでフルスクリーン三角形を描く(SSR の合成と同じ)。
7. compute は G-Buffer だけを読むので、シーン RT の RTV を外す必要は無い。ただし契約 3 に従い、**合成の前にシーン RT を自分でバインドし直す**(SSRPass と同じ)。
8. `.fx` を足したら `shader_entries.txt` にも足す。`RWTexture2D<float>`(R32)は DoF の CoC と同じ形で、Vulkan の Rgba32f の警告は出ない見込み(P2 で確認)。
9. Metal の `CreateShader` は失敗しても null を返さない(README「既知の課題」)。生成失敗の経路は Vulkan(Release。Debug は `.spv` 欠落で assert)で確かめる。

---

## 1. 責務

| ファイル | 責務 |
|---|---|
| `aqEngine/Assets/Shader/PBRFunctions.fx` | `EvalIBL` の拡散部分を `EvalIBLDiffuse` に括り出す。**`EvalIBL` の結果は変えない** |
| `aqEngine/Assets/Shader/PBRLighting.fx` | `PSMain` の ambient の式に「`SSAOComposite.fx` の `PSMain` と一致させること」の注記だけを足す(式は変えない) |
| `aqEngine/Assets/Shader/SSAO.fx` **(新規・P2)** | compute(8×8)。§0.3。t0 = GBuffer1、t1 = GBuffer2、t2 = GBuffer3、u0 = 生の AO(`RWTexture2D<float>`) |
| `aqEngine/Assets/Shader/SSAOBlur.fx` **(新規・P2)** | compute(8×8)。§0.4。t0 = 生の AO、t1 = GBuffer1、t2 = GBuffer2、u0 = ぼかした AO |
| `aqEngine/Assets/Shader/SSAOComposite.fx` **(新規)** | VS はフルスクリーン三角形(`SSRComposite.fx` と同じ)。`PSMain` は `−(1 − ao) × ambient の式`、`PSMainIBL` は `−(1 − ao) × EvalIBLDiffuse(...) × iblIntensity` を **α = 1** で返す。G-Buffer の画素だけ(pixelTag で `clip`)。ao は t0(ぼかした AO)を同じ画素で `Load`。デバッグ表示なら ao をグレーで返す(コマンドは `Opaque` で上書き)。`testMode`(§2 P1)で ao を固定値にできる。スロットは b1 = LightingCB、b2 = `SSAOCompositeCB`、t0 = AO、t5 = irradiance キューブ、s0 = IBL のサンプラ、t8〜t11 = GBuffer0〜3 |
| `aqEngine/Rendering/SSAO/SSAORenderer.{h,cpp}` **(新規)** | シェーダ(AO・ぼかしの CS、合成の VS / PS / PSIBL)と RT 2 枚の所有、パラメータ(有効・`radius`・`intensity`・`power`・`biasScale`・`sampleCount`・`maxScreenRadius`・`planeTolerance`・デバッグ表示・`testMode`)の保持、リサイズ。`BuildComputeCommandList` / `BuildCompositeCommandList`。`IsActive()` = 有効 && シェーダと RT がそろっている。P1 では CS を持たず、`Create()` は合成のシェーダだけを作る |
| `aqEngine/Rendering/SSAO/SSAOComputeCommand.{h,cpp}` **(新規・P2)** | `IRenderCommand` 派生。AO の dispatch → ぼかしの dispatch を順に行う(間で生の AO の UAV を外して SRV にする)。RT はハンドルで持ち `Execute` で解決。CB は `fc.perDrawCBPool` から取る(ビューごとに別の値。分割画面で上書きしない) |
| `aqEngine/Rendering/SSAO/SSAOCompositeCommand.{h,cpp}` **(新規)** | `IRenderCommand` 派生。`SSRCompositeCommand` と同じ手順(資源を先にそろえ、欠けたら描かない。終わりに Opaque / ReadWrite に戻す)。IBL を使うときは `IBLEnvironment` を `shared_ptr` で持ち、強度は値で写す |
| `aqEngine/Rendering/SSAO/SSAODebugPanel.{h,cpp}` **(新規・P3)** | Rendering パネルの「SSAO」タブ(ON/OFF・AO の表示・各パラメータ)。あわせて、ぼかした AO の RT を `ImGui::Image` で縮小表示する(`DeferredDebugPanel` の G-Buffer 表示と同じ。SSR の映り込みが重ならない AO そのものを見るため)。`#ifdef AQ_DEBUG_IMGUI` |
| `aqEngine/Rendering/Pipeline/Passes/SSAOPass.{h,cpp}` **(新規)** | `SSAORenderer` を包むパス。View scope。`IsSupported()` は `IsComputeSupported()`。宣言は `Reads(GBuffer0..3)`・`Writes(Scene)`。`DeferredRenderer` と `IBLEnvironment`(null 可)を `shared_ptr` で共有する。`Build` は compute → シーン RT のバインド → 合成(`WasIBLUsed()` を渡す)。`ResizePolicy::Resizable`。`Setup` で RT を作り、作れなくても false を返さない |
| `aqEngine/Rendering/Pipeline/PipelinePresets.{h,cpp}` | `RendererPreset::enableSSAO`(既定 false)。`Standard()` の `DeferredLightingPass` の直後(`SSRPass` の前)に、生成に成功したら `SSAOPass` を足す |
| `aqEngine/Core/AppHost.cpp` | P3: `Find<SSAOPass>()` で「SSAO」タブを登録(SSR と同じ) |
| `aqEngine/Assets/Shader/shader_entries.txt` | `SSAOComposite.fx` の `VSMain vs` / `PSMain ps` / `PSMainIBL ps`(P1)、`SSAO.fx main cs` / `SSAOBlur.fx main cs`(P2) |
| `Engine.vcxproj` / `.filters` | 新規ファイルの登録(`vs-project-files`。**フィルターは実装前にユーザーへ確認する**) |
| `設計書/README.md`(既知の課題) | D3D12 の compute が t0〜t2 しか届かず、SSR のトレースの t3 が捨てられる件(§0.1)(P1) |
| `設計書/エンジン不足機能メモ.md` §5 | 被写界深度の行を「済」、SSAO の行を本書へのリンクに(本書の承認時) |

CB:
どちらも `perDrawCBPool`(1 枠 = `sizeof(VSConstantBuffer)` = 192B)から取る。`UpdateSubresource` は型のサイズを渡さずバッファ全体を更新するので
(`RenderContext::UpdateSubresource` → `UpdateConstantBuffer`)、**どちらも 192B ちょうどにパディングし、`static_assert(sizeof(...) == 192)` を置く**
(`SSRCompositeCBData` / `IBLCBData` と同じ作法)。HLSL の cbuffer とは先頭の並びを一致させる。
- `SSAOCBData`(compute。AO とぼかしで共用): `view`、ビュー矩形(x, y, w, h)、`projScaleY`・`projW34`・`projW44`(§0.3)、
  `radius`、`intensity`、`power`、`biasScale`、`sampleCount`、`maxScreenRadius`、`planeTolerance`、パディング。
- `SSAOCompositeCBData`(b2。先頭を `IBLCB` と同じ並びにする): `iblIntensity`、`iblSpecularMaxMip`(未使用。並びを揃えるため)、`testMode`、`debugView`、パディング。

---

## 2. フェーズ計画

### 評価の共通条件

- **シーン**: Sample(カメラ位置 (1.5, 2.5, -6)、注視点 原点、ウィンドウ 1280×752、撮影 2560×1504)。画素比較は [SSR設計](SSR設計.md) §3 と同じ
  (オーバーレイ (0,0)-(460,200) を除く。基準画像は変更前のコミットの worktree に同じ一時差分を当てて撮る)。
- **AO の見え方を比べるときは `enablePostProcess = false` と `enableSSR = false` の一時差分**で撮る。Bloom・DoF は差を周辺へ広げ、
  SSR は SSAO の後ろでシーンをトレースして差分を足すので、AO の表示(グレーの上書き)にも映り込みが重なる(`SSRPass` は SSAO の表示を知らない)。
  このときシーン RT の α は比較に含めない(SSR P1 の知見)。
- **Sample の床と下段は金属で AO が出ない**(§0.1)ので、AO の見え方の評価は**床を非金属にする一時差分**(`SetPBRMaterial(0.0f, 1.0f)`)で行う。
- **FPS**: この Mac(Apple Silicon)の Release、FPS オーバーレイの値を起動 10 秒後に読む。59.5 以上を 60 FPS とみなす。
- Vulkan の validation は Debug 構成でしか効かない。

### P1: 差分合成の土台(AO の計算なし)

実装:
- `EvalIBLDiffuse` の括り出し、`SSAOComposite.fx`、`SSAOCompositeCommand`、`SSAORenderer`(合成のシェーダ・RT 2 枚とリサイズ・`testMode`)、`SSAOPass`、`PipelinePresets` の配線。
- `testMode`: 0 = 通常(ぼかした AO を読む。**P1 では AO を計算しないので合成を積まない**)、1 = ao を 1 に固定(差分 0)、2 = ao を 0 に固定(拡散の環境光を全部差し引く)。
  評価に使う固定入力で、P2 以降もデバッグ用として残す(SSR の `testMode` と同じ扱い)。
- README「既知の課題」に D3D12 の compute の SRV の件を記録。

評価(自動。Mac / Metal と Vulkan):
- [ ] ビルドが通り、警告が増えない(SSAO の新規・変更ファイルに警告なし)
- [ ] **`EvalIBL` の括り出しで `PSMainIBL` の画が変わらない**: `enableSSAO = false` で、P1 前のコミットと画素一致(差 0)
- [ ] **差分が 0 のとき何も変わらない**: `enableSSAO = true`・`testMode = 1` で、`enableSSAO = false` と画素一致(差 0)。パス列に `DeferredLightingPass > SSAOPass > SSRPass` が出る
- [ ] **IBL の拡散を正しく差し引く**: `enablePostProcess = false`・`testMode = 2` の画の**上段(非金属)の 5 箱の正面中央 16×16 の平均**が、
      `PBRLighting.fx` の `EvalIBL` で拡散を 0 にした一時差分(`enableSSAO = false`)の同じ範囲と、各 2/255 以内。
      誤合格の防止: `testMode = 1` の同じ範囲は、拡散 0 の一時差分と 10/255 以上違う
- [ ] **ambient を正しく差し引く**: `enableIBL = false`・`enablePostProcess = false` で、`testMode = 2` の上段 5 箱の正面中央 16×16 の平均が、
      ゲーム側の一時コードで環境光の強度を 0 にした画(`enableSSAO = false`)と各 2/255 以内。誤合格の防止: `testMode = 1` とは 10/255 以上違う
- [ ] **ロード境界で判断が食い違わない**: 一時コードで `DeferredRenderer::BuildLightingCommandList` の IBL の判断に「呼び出し回数が 60 未満なら false」を足し
      (一時の static カウンタ。単一ビューの Sample では `BuildLightingCommandList` は 1 フレームに 1 回なので回数 = フレーム番号。エンジンに共通のフレーム番号の口は無い)、一時ログで起動から 120 フレームの各ビューについて「ライティングが `PSMainIBL` を積んだか」と「合成が `PSMainIBL` を選んだか」を 1 行ずつ出す。
      全フレームで一致し、かつ 0 → 1 に切り替わるフレームが含まれる(`IsReady()` を偽るのではなく判断の側で区切る。`IsReady()` は実際のロード完了も待つので、
      切り替わるのは 60 フレーム目か、ロード完了がそれより遅ければそのフレーム)。`testMode = 2` で行う
- [ ] Metal API Validation エラー 0 / VK validation(Debug。`testMode = 2`)エラー 0 / 終了コード 0

### P2: AO の計算とぼかし

実装:
- `SSAO.fx`(§0.3)、`SSAOBlur.fx`(§0.4)、`SSAOComputeCommand`、AO の表示(デバッグ表示)。`testMode = 0` で合成を積むようにする。

評価(自動。共通条件の「床を非金属にする一時差分」と `enablePostProcess = false`。座標は撮影画像から実測して評価記録に書く):
- [ ] ビルドが通り、警告が増えない
- [ ] **平らな面は暗くならない**: AO の表示で、床の箱から離れた所(列の左右の外側、箱から 2 単位以上離れた範囲)の 64×64 の平均が 247/255 以上、最小が 235/255 以上
      (half 精度の worldPos での自己遮蔽が出ていない。§0.3 の bias)
- [ ] **接地の部分は暗くなる**: AO の表示で、下段の中央の箱の手前の下端に接する床(箱の下端から画面上 4〜12 画素下の帯、横 32 画素)の平均が 205/255 以下
- [ ] **奥行きの離れた境界にハローが出ない**: 上段の箱の上辺の輪郭のうち、すぐ奥が床である部分(箱から 2 単位以上奥の床。無ければ箱の左右の外側の輪郭)で、
      輪郭から 3〜8 画素外側の帯の AO の平均が 245/255 以上
- [ ] **G-Buffer の外は変わらない**: SSAO の ON/OFF で、空の画素(pixelTag < 0.5)が差 0
- [ ] **ノイズが均されている**: AO の表示で、上の「接地」の帯の各画素の値の標準偏差が 12/255 以下
- [ ] **分割画面**: Sample の一時コードで `AppHost::SetSplitViews` を左右 2 ビュー(各 640×752 相当)にし、AO の表示で次の 3 つを確かめる。
      ①左右とも同じカメラ A なら、左半分と右半分が画素一致(矩形の原点の足し忘れ・境界をまたぐぼかしが無い)。
      ②左 = A・右 = B(A を x 方向に 1 ずらしたカメラ)の左半分が、①の左半分と画素一致(ビューごとの CB が上書きされていない)。
      ③同じく右半分が、左右とも B にした画の右半分と画素一致
- [ ] **リサイズ**: AO の表示で、ウィンドウの右下の角を `ui drag` で 200 ポイント広げる。落ちず、ログに失敗が出ず、広げた後の撮影で右端・下端まで AO が出る
- [ ] **生成失敗**(Vulkan、Release): `SSAO.fx` のパスを一時的に存在しない名前にして起動し、ログに失敗が 1 回出て、パス列から `SSAOPass` が外れ、画が `enableSSAO = false` と画素一致
- [ ] **60 FPS を保つ**: Sample と AquaDash で `enableSSAO` を一時的に true にして、共通条件の FPS の読み方で 59.5 以上
- [ ] Metal API Validation エラー 0 / VK validation(Debug)エラー 0(WARN は既知の Rgba32f の種類だけ)/ 終了コード 0

評価(ユーザー確認):
- [ ] Sample(床を非金属にした一時差分)の静止画で、AO の付き方が自然か(縞・粒々・輪郭の暗い縁が目立たない)。既定パラメータの調整が要るか

### P3: デバッグ UI と、ゲームでの判断

実装:
- デバッグ UI(「SSAO」タブ。ON/OFF・AO の表示・各パラメータ)と `AppHost` の登録。
- Sample で既定で有効にするか、Sample の床(金属)をどうするか、AquaDash で使うかをユーザーに確認し、決めた方を反映する。

評価(自動。Mac / Metal / Debug):
- [ ] デバッグ UI で OFF にした画と、`enableSSAO = false` で起動した画が画素一致する(パネル・メニュー・Inspector の範囲を除く)
- [ ] デバッグ UI でパラメータ(`radius`)を変えると、AO の表示が変わる(変える前の画と、接地の帯の平均が 10/255 以上違う)

評価(ユーザー確認):
- [ ] Sample で既定で有効にするか。有効にするなら床の材質(SSR の見本の金属のままか、AO が見える非金属の台を足すか)
- [ ] AquaDash で有効にした場合の見た目(キャラクターと地形の AO、フォワードの道路が AO を受けない点)と、使うかどうか。走行中のちらつきは
      Space(加速)を押しっぱなしにして走らせながら、`screencapture -l <窓 ID>` で 0.2 秒おきに 10 枚撮って並べる
      (窓 ID は `CGWindowListCopyWindowInfo` で取る。この Mac では押しっぱなしを個人の補助ツール `evthold` で合成しているが、別 PC では手で押してよい)

Windows(D3D11 / D3D12)の確認は Windows 機の宿題に積む。

---

## 3. テスト方針のまとめ

- 画素比較・基準画像の作り方・Vulkan の validation は [SSR設計](SSR設計.md) §3 と同じ。
- パラメータ・`testMode`・デバッグ表示は、テスト用の一時コード(Sample の `OnInitialize` で `Find<SSAOPass>()` → setter)で切り替える。
- 自動確認: 画素一致(既定で絵が変わらない・差分 0 で変わらない)、固定入力での差し引きの正しさ、AO の値のしきい値(平らな面・接地・ハロー・ノイズ)、
  リサイズ、生成失敗、FPS、validation。
- ユーザー確認: 見た目の自然さ、Sample / AquaDash で使うか。

---

## 4. 決めていないこと

- 半解像度で計算するか。全解像度で P2 の FPS を見て決める(DoF・SSR は全解像度で 60 FPS だった)。
- `sampleCount` などの既定値の最終値(P2 のユーザー確認で決める)。

## 5. オープン課題

1. **鏡面の遮蔽(specular occlusion)が無い。** 金属や光沢の強い面には AO が出ない。入れるなら SSR の合成の差し引きにも同じ係数を掛ける必要がある。
2. **フォワードの物は AO を受けず、落とさない**(§0.2)。深度 SRV か、フォワードの深度・法線の書き込みが要る([被写界深度設計](被写界深度設計.md) §5 課題 1 と同じ土台)。
3. **時間方向の安定化は無い。** カメラが動くときノイズがちらつくなら、TAA と一緒に扱う。
4. **ぼかしは pixelTag を読まない**(D3D12 の compute の SRV が 3 枚のため)。worldPos が原点ちょうどの G-Buffer の画素を背景と誤る(実害は原点に置いた物の 1 画素程度)。
5. **画面の外・手前の物の裏は遮蔽物として数えない**(画面空間の手法の一般的な限界)。
6. **worldPos が絶対座標の half 精度**(原点から遠い場所で bias が大きくなり、細かい AO が消える)。[SSR設計](SSR設計.md) §5 課題 5 と同じ。

---

## 6. チェックポイント

- [ ] P1 の評価を全て満たした
- [ ] P2 の評価を全て満たした
- [ ] P3 の評価を全て満たした
- [ ] 抽象 IF(`IRenderContextImpl` / `IGraphicsDeviceImpl`)への追加が 0 本
- [ ] `shader_entries.txt` に 5 行が登録されている(P1 で 3 行、P2 で 2 行)
- [ ] 新規 `.h/.cpp` が `.vcxproj` / `.filters` に登録されている
- [ ] SSAO の生成に失敗しても起動できる(SSAO なしの絵で続行する)
- [x] [エンジン不足機能メモ.md](エンジン不足機能メモ.md) §5 の SSAO の行を本書へのリンクに置き換え、被写界深度の行を「済」にした(2026-11-14)
- [ ] 設計と実装の食い違いを本書へ反映し、`対象コミット` を更新した
