# カスケードシャドウ(CSM)設計

> 対象コミット: 85ed9fe / 最終更新: 2026-11-14(設計。未承認)

対象: `aqEngine/Rendering/Shadow/` + `aqEngine/Rendering/Pipeline/Passes/ShadowPass.*` + `aqEngine/Assets/Shader/ShadowCB.h` /
`ShadowSampling.fx` / `ShadowDepth.fx` + Sample の見本 + デバッグパネル。
**全バックエンド(D3D11 / D3D12 / Vulkan / Metal)・Standard / Mobile の両構成**で動かす。Mac で確認できるのは Metal / Vulkan。

一次資料の分担:
- **本書** … CSM の方針決定・変更ファイルの責務・フェーズ計画・チェックリスト。
- [SSR設計.md](SSR設計.md) §3 … 画素比較の手順(基準画像を変更前のコミットの worktree で撮る作法)。本書はそれを使う。
- [レンダーパイプライン設計.md](レンダーパイプライン設計.md) … パス列方式・Frame scope / View scope。
- [エンジン不足機能メモ.md](エンジン不足機能メモ.md) §5 … 本機能を入れる判断。
- [README「既知の課題」](README.md#既知の課題) … 「影の cascade は名前だけ」の件(本書の P1 で解消する)。

---

## 0. 現状と方針

### 0.1 現状(実コードで確認)

- **影のスライスは「ディレクショナルライトごと」。** `HardShadowRenderer::FillShadowCBData` はライト i(最大 4)ごとに、
  `sceneCenter` を中心にした固定の正射影(`orthoWidth` × `orthoHeight`、`nearPlane`〜`farPlane`)を `lightViewProj[i]` に入れ、
  `cascadeCount` にライト数を入れる。`cascadeSplits` は x に `farPlane` を入れるだけで、シェーダは読まない。
  `BuildShadowCommandList` はライトごとにスライス i をクリアし、影を落とす全アイテムを描く。
- **深度マップは 4 枚の配列で、全バックエンドが同じ。** D3D11 / D3D12(`D3D12DepthMap::kArraySize = 4`)/ Vulkan(`VulkanDepthMap`。ArraySize 4)/ Metal。
  `IDepthMap::GetSliceSRV(i)` でスライスごとの SRV が取れる(デバッグ表示が使用)。→ **カスケード 4 枚はバックエンドを変えずに入る。**
- **影を読むシェーダは 4 本で、全て `SampleShadowForLight(worldPos, ライト番号)` を通す。**
  `PBRLighting.fx`(ディファード)/ `ModelLit.fx` / `TerrainLit.fx` / `SkeletalModelLit.fx`(フォワード。Mobile 構成の PBR の物は `ModelLit.fx` の `PSMain` で描かれる。`StaticMesh.cpp` のシェーダ表)。
  関数本体は `ShadowSampling.fx` の 1 か所。呼び出し側は `directionalLightCount > i` のとき i = 0〜3 を呼ぶ。
  ほかに深度パスの `ShadowDepth.fx` が `ShadowCB.h` を読む(サンプリングはしない。CB の変更に追従させる)。
  → **呼び出し側を変えずに、関数の中でカスケードを選べる。**
- `ShadowSampling.fx` はテクセルサイズを `1.0 / 2048.0` で固定している(`ShadowSettings::resolution` を変えると PCF の幅がずれる)。
- `ShadowCBData`(b3、288B)は `RenderFrame::shadow` に入り、フレームに 1 個(`FrameContext::shadowCB`)。CB のサイズはどこも `sizeof(ShadowCBData)` で作っている
  (`Renderer.cpp` / `RenderThread.cpp`)。
- **`ShadowPass` は Frame scope で、`frames[0]`(分割画面ではビュー 0)で 1 回だけ Build される**(`RenderPipeline::BuildViews`)。
  `RenderFrame::camera`(`CameraData`: view / projection / position / nearZ / farZ)が使える。分割画面(`AppHost::SetSplitViews`)を呼ぶゲームは今は無い。
- `FillShadowCBData` の引数は `LightingData` だけで、カメラを受け取れない(`IShadowRenderer`)。
- **影を落とす物は、どこでもカリングされていない。** `BodyComponentSystem` は「影はライト視錐台で別途判定する」として castShadow のアイテムをカメラ視錐台で切らないが、
  その「別途判定」はどこにも無い。`RenderItem` には `localBounds` / `hasBounds` がある。
- 使っているディレクショナルライトは、Sample・AquaDash とも 1 本(`LightManager` の既定。向き (0, −1, 0.5))。本数を変えるのはデバッグパネルだけ。
- ゲームの設定: AquaDash は `preset.shadow` で `nearPlane 0.1`・`farPlane 60`・`softness 2` を指定し、毎フレーム `SetSceneCenter(注視点 + 2m 上)` を呼ぶ。
  カメラの far は 3000(`AquaDashStates.cpp`)。Sample は既定値のままで、**影を落とす物が無い**(`BoxStaticMeshComponent` は castShadow を立てない。
  `GetStaticMesh()->SetCastShadow()` は呼べる)。
- AquaDash のオフスクリーン(`GameModule.cpp`)は `MakeNeutralShadowCBData()`(全スライスを範囲外へ落とす行列 + `cascadeCount = 0`)で影を切っている。
- **スキンメッシュの影は骨を無視している。** `ShadowCastCommand` は `ShadowDepth.fx`(POSITION だけ)で描き、`boneMatrices` を使わない。
  `SkeletalModelLit.fx` は GPU スキニング(b4 の `BonesCB`)なので、**AquaDash のキャラクターの影はバインドポーズの形**になっている。本書では直さない(§4・README へ記録)。
- 行列の規約: DirectXMath の左手系・行ベクトル(`MakeLookAt` = `XMMatrixLookAtLH`、`MakeOrthographic` = `XMMatrixOrthographicLH`、深度 0〜1)。
  HLSL 側は `mul(lightViewProj[i], float4(worldPos, 1))` で今の影が合っている。**この規約は変えない。**

### 0.2 確定した方針

| 項目 | 決定 | 理由 |
|---|---|---|
| 方式 | **1 本目のディレクショナルライトだけ**を、カメラの視錐台を距離で 1〜4 区間に割ったカスケードで描く(スライス i = カスケード i)。2〜4 本目のライトは影なし | 深度マップの 4 枚をそのまま使える(§0.1)。ライト × カスケードにすると配列と CB を増やす必要があり、使っているライトは 1 本だけ |
| 従来方式の扱い | **残す。** `ShadowSettings::mode` に `PerLight`(今の方式。**既定**)と `Cascaded` を持ち、同じ `HardShadowRenderer` が両方を描く | 「既定値なら従来と同じ絵」を守る(`RendererPreset` の約束)。デバッグパネルで切り替えて比べられる。AquaDash で使うかは P3 で決める |
| 名前の整理 | CB の欄を中身どおりの名前にする(§1 の CB 表)。`lightViewProj` → `sliceViewProj`、`cascadeCount` → `sliceCount`、`cascadeSplits` → `cascadeSplitDepths`、`MaxShadowCascades` → `MaxShadowSlices`、`ShadowSliceCBData::lightSlice` → `slice` | README「既知の課題」の解消。「スライス」= 深度マップの 1 枚(PerLight ではライト、Cascaded ではカスケード) |
| カメラの受け渡し | `IShadowRenderer::FillShadowCBData(const RenderFrame&, ShadowCBData&)` に変える(`frame.lighting` と `frame.camera` を読む) | カスケードの計算にカメラが要る。呼び出し元は `ShadowPass::Build` の 1 か所 |
| 分割の位置 | 対数分割と等分割を `splitLambda` で混ぜる(practical split scheme)。範囲は `cameraNear` 〜 `min(shadowDistance, cameraFar)` | AquaDash のカメラ far は 3000 で、far まで影を出すと手前が粗くなる。影を出す距離を別に持つ |
| カスケードの包み方 | 区間の視錐台の 8 隅を包む**球**で正射影の幅を決める(幅 = 直径、正方形)。中心はライト空間で**テクセル単位に丸める** | 球はカメラの回転で大きさが変わらず、丸めると平行移動でも影の縁がちらつかない(stable CSM) |
| ライト方向の奥行き | 正射影の手前を球の中心からライトの逆向きに `radius + casterExtension` 下げる。範囲は `0` 〜 `2 × radius + casterExtension` | カメラに映らない位置(ライト側)の物の影を落とすため |
| 選び方(シェーダ) | 画素のビュー深度 `viewDepth`(カメラの view 行列の第 3 列。`dot(cameraDepthAxis.xyz, worldPos) + cameraDepthAxis.w`)が `cascadeSplitDepths[i]` 未満の最初の i | 区間で選ぶとブレンドの幅を距離で決められる。呼び出し側(4 本)を変えずに済む |
| 境目 | 各カスケードの奥側 `cascadeBlend`(区間長に対する割合。既定 0.1)で次のカスケードと線形に混ぜる。最後のカスケードは同じ幅で「影なし(1.0)」へ薄める | 境目の粗さの段差と、影の届く距離の端を目立たせない |
| バイアス | スライスごとの深度バイアス `sliceDepthBias[i]`(NDC)を CB に持つ。PerLight は全スライス `depthBias`(従来どおり)。Cascaded は `cascadeBiasTexels × テクセルのワールド幅 ÷ 奥行きの範囲` | カスケードごとにテクセルの大きさと奥行きの範囲が違うので、1 つの NDC 値では遠いカスケードでアクネか浮きが出る |
| テクセルサイズ | CB の `texelSize` = `1 / resolution`(シェーダの固定値 `1/2048` をやめる) | `resolution` を変えると PCF の幅がずれる不具合(§0.1)を同時に直す。2048 なら値は同じ |
| 影を落とす物のカリング | `BuildShadowCommandList` で、スライスごとに `sliceViewProj[i]` の視錐台(`math::Frustum::FromViewProjection`)とアイテムのワールド AABB を比べ、外れた物を描かない。`hasBounds == false` は描く。**両方の方式に効かせる** | Cascaded は描く回数が最大 4 倍になる。範囲外の物は描いても深度マップに何も残らないので、絵は変わらない(P2 で画素一致を確かめる) |
| 分割画面 | ビュー 0 のカメラで決めたカスケードを全ビューが使う(Frame scope のまま) | 使うゲームが無い(§0.1)。ビュー 1 以降で影が欠ける・粗い点は §5 に残す |
| `SetSceneCenter` | Cascaded では使わない(呼ばれても無視。PerLight 用に値は保持する) | カスケードはカメラから決まる。AquaDash は毎フレーム呼んでいるが害は無い |
| デバッグ表示 | CB の `debugView = 1` で、`SampleShadowForLight` がカスケード番号に応じた段階の値(0 → 1.0、1 → 0.75、2 → 0.5、3 → 0.25。範囲外 → 1.0)を返す。**これは直接光への乗数で、画素の値そのものではない**(環境光・BRDF・後処理を経る)。評価では「明るさが段で変わる位置と段の数」だけを見る | 4 本のシェーダ・両構成でそのまま効き、合成のパスを足さずに済む。境目の位置を画面上で測れる(P2 の評価に使う) |
| スライス数での打ち切り | PerLight でも `lightIndex >= sliceCount` なら 1.0 を返す | 通常は `sliceCount` = ライト数(最大 4)で、呼ばれるのはライト数未満だけなので絵は変わらない。`MakeNeutralShadowCBData()`(`sliceCount = 0`)が行列だけに頼らず影を切れる |

### 0.3 カスケードの計算(`ShadowCascade.{h,cpp}`)

入力: カメラの `view` / `projection` / `nearZ`(n)/ `farZ`、ライトの向き `L`(正規化。ゼロなら (0, −1, 0))、設定(§1 の `ShadowSettings`)、深度マップの解像度 `res`。

0. **入力の検査**(設定はデバッグパネルが参照で書き換えるので、ここで毎回丸める): `n = max(nearZ, 1e-3)`、`N = clamp(cascadeCount, 1, 4)`、
   `λ = clamp(splitLambda, 0, 1)`、`β = clamp(cascadeBlend, 0, 0.5)`、`cascadeBiasTexels` と `casterExtension` は 0 以上。
   `f = min(shadowDistance, farZ)` が `n × 1.01` 以下なら **`sliceCount = 0`(影なし)**にして終わる(ログは状態が変わったときに 1 回)。
   解像度 `res` は 16 以上が前提(`HardShadowRenderer::Create` が `resolution < 16` を false で弾く。手順 5 の `res − 2` で割るため)。
   クランプした `n` と `β` は CB の `cascadeNearDepth` / `cascadeBlend` にもそのまま書く(§0.4)。
1. **範囲**: 境界を `b_0 = n`、`b_N = f` とする。
2. **分割**: k = 1〜N − 1 について `b_k = λ × n × (f / n)^(k/N) + (1 − λ) × (n + (f − n) × k / N)`。
   カスケード k(0〜N − 1)の受け持ちは `[b_k, b_{k+1}]`。`cascadeSplitDepths[k] = b_{k+1}`。使わない要素(k ≥ N)は `f` を入れる。
3. **包む区間**: カスケード k の奥側 `β × (b_{k+1} − b_k)` は次のカスケードとも混ぜる(§0.4)ので、**カスケード k + 1 はその分だけ手前まで包む**。
   包む区間は k = 0 が `[b_0, b_1]`、k ≥ 1 が `[b_k − β × (b_k − b_{k−1}), b_{k+1}]`。
   `invVP = Inverse(view × projection)`。NDC の (±1, ±1, 0) と (±1, ±1, 1) を `invVP` で戻し(w で割る)、カメラの near 面の 4 隅 `Nc[j]` と far 面の 4 隅 `Fc[j]` を得る。
   深度 d の隅は `Nc[j] + (Fc[j] − Nc[j]) × (d − nearZ) / (farZ − nearZ)`(透視でも正射影でも、隅の辺に沿ってビュー深度は線形)。包む区間の両端で 4 隅ずつ、計 8 隅。
4. **球**: 中心 `C` = 8 隅の平均、半径 `r` = 中心から隅への最大距離を `1/16` 単位に切り上げる(浮動小数の揺れで幅が毎フレーム変わらないように)。
5. **テクセル丸め**: `rot = LookAt(原点, L, up)`(`up` は今と同じ選び方: `|L.y| > 0.99` なら (1, 0, 0)、それ以外は (0, 1, 0))。
   `texel = 2r / (res − 2)`、正射影の幅 `w = texel × res`(= `2r + 2 × texel`。**丸めで中心が最大 1 テクセル動いても球が収まる余白**)。
   `C` を `rot` でライト空間へ移し、x・y を `floor(v / texel) × texel` に丸め、`Inverse(rot)` でワールドへ戻す(`C'`)。
   丸めで x・y はそれぞれ 1 テクセル未満しか動かないので、球の点の x・y は `C'` から `r + texel = w / 2` 未満に収まる。
6. **行列**: `eye = C' − L × (r + casterExtension)`、`view = LookAt(eye, C', up)`、`proj = Orthographic(w, w, 0, 2r + casterExtension)`。`sliceViewProj[k] = view × proj`。
   球は奥行き `[casterExtension, 2r + casterExtension]` に入る。
7. **バイアス**: `sliceDepthBias[k] = cascadeBiasTexels × texel / (2r + casterExtension)`。

`ShadowCascade` はグラフィクスに依存しない純粋な計算にする(`math` だけを使う)。P2 の自己確認(§2)はこの関数の出力を検算する。

### 0.4 シェーダ(`ShadowSampling.fx`)

```
float SampleShadowForLight(float3 worldPos, uint lightIndex)
  shadowMode == 0(PerLight): lightIndex >= sliceCount → 1.0。slice = lightIndex。今と同じ判定と PCF(テクセルは texelSize、バイアスは sliceDepthBias[slice])
  shadowMode == 1(Cascaded):
    lightIndex != 0 → 1.0
    z = dot(cameraDepthAxis.xyz, worldPos) + cameraDepthAxis.w
    i = z < split[0] ? 0 : z < split[1] ? 1 : ...(sliceCount まで。どれにも入らなければ 1.0)
    debugView == 1 → カスケード番号の段階値(§0.2)
    s = PCF(slice i)
    区間の奥 cascadeBlend の範囲なら t = 区間内の位置 → s = lerp(s, 次の値, t)
      次の値 = i + 1 < sliceCount ? PCF(slice i + 1) : 1.0
    return s
```

- 区間の手前の端 `start_i` は `i == 0 ? cascadeNearDepth : split[i − 1]`。**CPU(§0.3 の `b_0 = n`)と同じ値を CB で受け取る**
  (0 と置くと、1 枚目のブレンドが `β × n` だけ早く始まり、2 枚目の包む区間から外れる)。
- ブレンドの割合は CB の `cascadeBlend` を使う。**CPU は §0.3 手順 0 でクランプした β を CB に書く**(未検査の設定値を渡すと、CPU が包んだ区間とシェーダのブレンド範囲が食い違う)。
- 範囲外の判定(uv が 0〜1 外、ndc.z が 0〜1 外 → 1.0)は今の判定をスライスごとに使う。
- `[unroll]` の 4 分岐で書く(CB の配列を動的な添字で引くのは全バックエンドで通っている。`ShadowDepth.fx` が if の連鎖なのは歴史的なもの。P1 でそのまま残す)。

### 0.5 落とし穴(実装前に読むこと)

1. **CB のレイアウトは C++(`ShadowData.h`)と HLSL(`ShadowCB.h`)で完全に一致させる。** `static_assert(sizeof(ShadowCBData) == 336)`。
   Metal / Vulkan は `.fx` から生成した MSL / SPIR-V を使うので、**`ShadowCB.h` を変えたら全シェーダを作り直す**(Sample だけのビルドでは生成されない。memory の IBL の到達点の注意と同じ)。
2. **`MakeNeutralShadowCBData()` を新しい欄に合わせる**(`shadowMode = 0`・`sliceCount = 0`・行列は今と同じ範囲外行列)。AquaDash のオフスクリーンが使う。
3. **カスケードの添字とスライスの添字を混ぜない。** 深度パスの b2(`ShadowSliceCBData::slice`)はスライス番号。Cascaded ではカスケード番号と同じだが、PerLight ではライト番号。
4. **改行と BOM はファイルごとに違う。** CRLF は `HardShadowRenderer.cpp` と `Debug/ShadowDebugPanel.{h,cpp}`。他の Shadow のファイルは LF
   (`IShadowRenderer.h` / `ShadowData.h` / `ShadowPassCommand.h` は BOM 付き)。改行と BOM を保持する(CLAUDE.md)。
5. **カメラの near が極端に小さい**(AquaDash のタイトルは 0.01)と、対数分割の 1 枚目が極端に狭くなる。`splitLambda` で等分割を混ぜて抑える(既定値は P2 で決める)。
6. `Frustum::FromViewProjection` は正射影の VP でも 6 平面を出す前提で使う。P2 の「カリングで絵が変わらない」で確かめる。
7. Metal の `CreateShader` は失敗しても null を返さない(README「既知の課題」)。シェーダの不整合は Metal API Validation と Vulkan の validation(Debug)で見る。

---

## 1. 責務

| ファイル | 責務 |
|---|---|
| `aqEngine/Rendering/Shadow/ShadowData.h` | `MaxShadowSlices`。`enum class ShadowMode { PerLight, Cascaded }`。`ShadowSettings` に `mode`(既定 PerLight)・`cascadeCount`(既定 4)・`shadowDistance`・`splitLambda`・`casterExtension`・`cascadeBlend`・`cascadeBiasTexels`・`debugView` を足す(既定値は P2 で決める。仮: 80 / 0.75 / 50 / 0.1 / 1.5 / false)。`ShadowCBData` を下の表のレイアウトに。`MakeNeutralShadowCBData()` の更新。`ShadowSliceCBData::slice` |
| `aqEngine/Assets/Shader/ShadowCB.h` | `ShadowCBData` と同じ並びの `cbuffer ShadowCB : register(b3)`。`MAX_SHADOW_SLICES` |
| `aqEngine/Assets/Shader/ShadowSampling.fx` | §0.4。`kShadowTexelSize` の固定値をやめ `texelSize` を使う |
| `aqEngine/Assets/Shader/ShadowDepth.fx` | 名前の変更だけ(`slice`、`sliceViewProj`) |
| `aqEngine/Rendering/Shadow/IShadowRenderer.h` | `FillShadowCBData(const RenderFrame&, ShadowCBData&)`。冒頭のコメント(`CascadedShadowRenderer` などの例)を今の形に直す |
| `aqEngine/Rendering/Shadow/ShadowCascade.{h,cpp}` **(新規・P2)** | §0.3 の計算。`ComputeCascades(const CameraData&, const Vector3& lightDir, const ShadowSettings&, uint32_t res, ShadowCBData& out)` が `sliceViewProj`・`cascadeSplitDepths`・`sliceDepthBias`・`sliceCount`・`cascadeBlend`・`cascadeNearDepth`(後ろ 2 つは §0.3 手順 0 でクランプした値)を埋める。グラフィクス非依存 |
| `aqEngine/Rendering/Shadow/HardShadowRenderer.{h,cpp}` | `Create` で `resolution < 16` を false にする(P1)。`FillShadowCBData` で `mode` により PerLight(今の計算)/ Cascaded(`ShadowCascade`)を分ける。共通の欄(`shadowMode`・`softness`・`texelSize`・`cameraDepthAxis`・`debugView`)を埋める(`cascadeBlend`・`cascadeNearDepth` は `ShadowCascade` がクランプ済みの値で埋める)。`BuildShadowCommandList` はスライス数(PerLight = ライト数、Cascaded = カスケード数)だけ回し、P2 でスライスごとのカリング(§0.2)を足す。カリングの前後の数を Debug 構成で数える(P2 の評価とデバッグパネル用) |
| `aqEngine/Rendering/Shadow/ShadowPassCommand.{h,cpp}` | 名前の変更だけ |
| `aqEngine/Rendering/Pipeline/Passes/ShadowPass.cpp` | `FillShadowCBData(frame, frame.shadow)` |
| `aqEngine/Rendering/Shadow/Debug/ShadowDebugPanel.{h,cpp}` | P1: 名前の変更だけ。P3: 方式の切り替え、カスケードの設定(数・距離・λ・ブレンド・バイアス)、デバッグ表示、スライスのプレビューの見出し(PerLight は「Light i」、Cascaded は「Cascade i(〜d_i m)」)、カリング前後の描画数 |
| `Sample/Application/SampleModule.cpp` | P2: `preset.shadow.mode = Cascaded`。既存の箱と床に castShadow / receiveShadow を立てる。遠くまでの影の見本(§2 P2) |
| `Game/Application/GameModule.cpp` | P3: AquaDash で Cascaded を使うと決めたときだけ `preset.shadow` を変える |
| `Engine.vcxproj` / `.filters`・CMake | `ShadowCascade.{h,cpp}` の登録(`vs-project-files`。**フィルターは実装前にユーザーへ確認する**。CMake がファイルを列挙しているなら同時に足す) |
| `設計書/README.md`(既知の課題) | P1: 「cascade は名前だけ」を解消済みにする。「スキンメッシュの影がバインドポーズ」と「影を落とす物がどこでもカリングされない」(§0.1)を足す。P2: カリングの件を解消済みにする |
| `設計書/エンジン不足機能メモ.md` §5 | CSM の行を本書へのリンクに(本書の承認時) |

`ShadowCBData`(b3。336B。16B 境界):

| オフセット | 欄 | 内容 |
|---|---|---|
| 0 | `float4x4 sliceViewProj[4]` | スライスごとのライトの VP |
| 256 | `float4 cascadeSplitDepths` | Cascaded: 各カスケードの奥端のビュー深度。PerLight: 未使用(0) |
| 272 | `float4 sliceDepthBias` | スライスごとの深度バイアス(NDC) |
| 288 | `float4 cameraDepthAxis` | xyz = view 行列の (`_13`, `_23`, `_33`)、w = `_43`。ビュー深度 = dot(xyz, P) + w |
| 304 | `uint sliceCount` / `uint shadowMode` / `float softness` / `float texelSize` | |
| 320 | `float cascadeBlend` / `uint debugView` / `float cascadeNearDepth` / `uint pad0` | `cascadeBlend` はクランプ済みの β、`cascadeNearDepth` は `b_0`(§0.3)。PerLight では 0 |

---

## 2. フェーズ計画

### 評価の共通条件

- **シーン**: Sample(カメラ位置 (1.5, 2.5, −6)、注視点 原点、ウィンドウ 1280×752、撮影 2560×1504)。画素比較は [SSR設計](SSR設計.md) §3 と同じ
  (オーバーレイ (0,0)-(460,200) を除く。基準画像は変更前のコミットの worktree に同じ一時差分を当てて撮る)。
- **P1 は Sample に影を落とす物が無い**ので、比較は「見本列の箱と床に castShadow / receiveShadow を立てる一時差分」を前後の両方に当てて行う。
- **影の明るさを測る評価は「測定用の構成」で撮る**: 一時差分で `enablePostProcess = false`・`enableSSR = false`・`enableSSAO = false`・`enableDoF = false`
  (IBL はそのまま。前後で同じ構成なら値の比較に影響しない)。
- **影の縁の幅**: 影の縁を横切る 1 行(座標は最初の撮影で決めて本書に書く)の上で、影の内側 8 画素の平均を S、影の外側 8 画素の平均を L とし(輝度 = (R+G+B)/3)、
  値が `S + 0.1 × (L − S)` 〜 `S + 0.9 × (L − S)` の間にある画素の数を「縁の幅」とする。`L − S` が 20/255 未満なら測定不能として条件を見直す。
- **FPS**: この Mac(Apple Silicon)の Release、FPS オーバーレイの値を起動 10 秒後に読む。59.5 以上を 60 FPS とみなす。
- Vulkan の validation は Debug 構成でしか効かない。Metal は `METAL_DEVICE_WRAPPER_TYPE=1` で API Validation を入れる。終了は閉じるボタンで。

### P1: 名前の整理と CB の拡張(絵は変えない)

実装:
- §1 の名前の変更、`ShadowCBData` / `ShadowCB.h` の新レイアウト、`ShadowMode`(この時点では PerLight だけが動く。Cascaded の欄は 0)、
  `texelSize` と `sliceDepthBias` をシェーダが使う形に、`FillShadowCBData` の引数を `RenderFrame` に、`MakeNeutralShadowCBData()` の更新。
- README「既知の課題」の更新(§1)。

評価(自動。Mac / Metal と Vulkan):
- [ ] ビルドが通り、警告が増えない(Shadow の変更ファイルに警告なし)
- [ ] **絵が変わらない**: 共通条件の一時差分(影あり)で、P1 前のコミット(`85ed9fe` の worktree)と画素一致(差 0)。Metal と Vulkan の両方。
      誤合格の防止: 同じ一時差分の有無で、箱の影の範囲に差が出る(影が実際に描かれている)
- [ ] **`resolution` が PCF に効く**: 測定用の構成で `resolution = 1024`・`softness = 4` にしたとき、影の縁の幅(共通条件の定義)が
      P1 前の同じ設定より 2 画素以上広い(P1 前は 1/2048 固定で狭く出る)
- [ ] **影を切る CB が効く**: 一時コードで `ShadowPass::Build` の直後に `frame.shadow = MakeNeutralShadowCBData()` で上書きした画が、
      同じ一時差分から castShadow だけを外した画(receiveShadow は立てたまま)と画素一致(差 0)。
      あわせて、行列だけ範囲外のまま `sliceCount` を 1 にした一時コードでも影が出ない(行列による切り方が今も効く)
- [ ] AquaDash がタイトルからゲームまで起動し(オフスクリーンを含む)、validation エラーが出ない
- [ ] Metal API Validation エラー 0 / VK validation(Debug)エラー 0 / 終了コード 0

### P2: カスケードの計算・選択・カリングと Sample の見本

実装:
- `ShadowCascade`(§0.3)、`HardShadowRenderer` の Cascaded の経路、`ShadowSampling.fx` の選択・ブレンド・薄め・デバッグ表示(§0.4)、スライスごとのカリング。
- Sample: `preset.shadow.mode = Cascaded`。見本列の箱と床に castShadow / receiveShadow。**遠くまでの影の見本**として、
  見本列の奥に非金属の地面(y = −1.75、幅 120 × 奥行き 120)と、x = +6 に 8m 間隔で z = 8〜64 に細い柱(0.4 × 4 × 0.4)を 8 本置く
  (ライトは (0, −1, 0.5) なので影は奥へ伸びる)。置き方は見え方を見て P2 で調整し、本書へ反映する。

評価(自動。Mac / Metal と Vulkan):
- [ ] ビルドが通り、警告が増えない
- [ ] **カスケードが区間を包む**: 一時コードで、Sample のカメラと、カメラを 360° 回した 8 方向それぞれについて、各カスケードの**包む区間**(§0.3 手順 3。ブレンドの分を含む)の 8 隅を
      `sliceViewProj[k]` で写した NDC が x・y ∈ [−1, 1]、z ∈ [0, 1] に収まる(許容 1e-4)。分割は単調増加で、最後が `min(shadowDistance, farZ)`。
      同じ検算を、カメラの near 0.01・far 3000(AquaDash の値)と、`shadowDistance` を near 以下にした場合(`sliceCount = 0` になる)でも行う
- [ ] **テクセル丸めが効く**: 一時コードで、カメラを x 方向に 0.001 ずつ 100 フレーム動かしたとき、各 `sliceViewProj[i]` で原点を写した NDC の x・y の変化が
      全て `2 / res` の整数倍(許容 1e-3 × 2 / res)。誤合格の防止: 丸めを外した一時差分では整数倍にならないフレームがある
- [ ] **カリングで絵が変わらない**: Cascaded・PerLight のそれぞれで、カリングを一時的に外した画と画素一致(差 0)。
      このとき Sample の影の描画数(Debug の数)がカリングありで少ない(柱の数を含めて、外す前より減っていることを一時ログで確認)
- [ ] **境目が区間どおりの位置に出る**: 測定用の構成・`debugView = 1`・`cascadeBlend = 0` の画で、地面の上を縦に通る 1 列(柱の影を避ける)をたどり、
      隣の行と輝度が 3/255 以上変わる行が、カスケードの境界の深度 `b_1`〜`b_3` の地面の点を投影した行と ±3 行以内で一致する(行の位置は一時ログでカメラの VP から求める)。
      段の数はカスケード数 − 1(画面内に入る境界の数)
- [ ] **遠くの影が出る**: 測定用の構成で、最も奥の柱(z = 64)の影の中央 8×8 が、その柱の横の影の無い地面 8×8 より 20/255 以上暗い。
      誤合格の防止: PerLight(既定の `orthoWidth` 50)の同じ範囲では差が 5/255 未満(範囲外で影が出ない)
- [ ] **近くの影が細かくなる**: 見本列の箱の影の縁の幅(共通条件の定義)が、PerLight より 1 画素以上狭い
- [ ] **ブレンドで薄くならない**: 測定用の構成・`cascadeBlend = 0.1` で、地面の上の柱の影を境界 `b_1` の手前・ブレンド範囲の中で横切る 1 行の S(影の内側)が、
      同じ影のブレンド範囲の手前での S と 5/255 以内(ブレンド先のカスケードの範囲外で「影なし」が混ざると明るくなる)
- [ ] **Mobile 構成でも効く**: 一時差分で Sample を `preset.pipeline = Mobile` にした画(`ModelLit.fx` のフォワード)で、上の「遠くの影が出る」と同じ判定を満たす
- [ ] **ライト 1〜3 は影なし**: 一時コードでライトを 2 本にした Cascaded の画で、2 本目のライトだけが当たる面(1 本目を強度 0 にする)に影が出ない(柱の影の範囲が周りと 2/255 以内)
- [ ] FPS: Sample(Release)で 60 FPS
- [ ] Metal API Validation エラー 0 / VK validation(Debug)エラー 0 / 終了コード 0

評価(ユーザー確認):
- [ ] カメラを動かしたとき(Sample のエディタのカメラ操作)、影の縁がちらつかない・カスケードの境目が目立たない
- [ ] 影のアクネ(縞)と浮き(peter panning)が気にならない。気になれば `cascadeBiasTexels` などの既定値を調整し、本書に反映する

### P3: デバッグ UI と、ゲームでの判断

実装:
- `ShadowDebugPanel`(§1)。
- AquaDash で Cascaded を使うか、Mobile 構成(iOS / Android)で `cascadeCount` をいくつにするかをユーザーに確認し、決めた方を反映する。

評価(自動。Mac / Metal / Debug):
- [ ] デバッグ UI で方式を PerLight に切り替えた画と、`mode = PerLight` で起動した画が画素一致(パネル等の範囲を除く)
- [ ] デバッグ UI で `cascadeCount` を 4 → 2 にすると、P2 の「境目が区間どおりの位置に出る」と同じ測り方で、輝度の段が 1 つ(境界 `b_1` だけ)になる。
      4 のときは 3 つ
- [ ] AquaDash で Cascaded にした場合: 走行中に Release で 60 FPS(P2 の FPS 計測と同じ手順。[SSAO設計](SSAO設計.md) §2 P3 の撮り方)

評価(ユーザー確認):
- [ ] AquaDash で使うか(走行中の見た目。キャラクターの影がバインドポーズである点(§0.1)を含めて判断する)
- [ ] Mobile 構成の `cascadeCount`(iOS シミュレータで `preset.pipeline` を Auto のままの Sample を Cascaded で動かし、FPS と、P2 の「遠くの影が出る」の判定を見て決める。
      実機の確認は iOS 移植の P5 の宿題に積む)

Windows(D3D11 / D3D12)の確認は Windows 機の宿題に積む。

---

## 3. テスト方針のまとめ

- 画素比較・基準画像の作り方・Vulkan の validation は [SSR設計](SSR設計.md) §3 と同じ。
- カスケードの計算は `ShadowCascade` がグラフィクス非依存なので、一時コードで出力を検算する(包含・単調性・テクセル丸め)。
- 自動確認: 画素一致(P1 で絵が変わらない・カリングで絵が変わらない・UI の切り替え)、計算の検算、境目の位置、遠近の影の濃さ、FPS、validation。
- ユーザー確認: ちらつき・境目・アクネと浮き、AquaDash / Mobile で使うか。

---

## 4. 決めていないこと

- `shadowDistance` / `splitLambda` / `casterExtension` / `cascadeBiasTexels` の既定値(P2 で Sample を見て決める)。
- **スキンメッシュの影を骨で動かすか**(§0.1)。CSM とは独立した不具合で、直すなら `ShadowDepth.fx` にスキン用の VS を足し、`ShadowCastCommand` で b4 に骨を渡す。
  本書の範囲外とし、README「既知の課題」に記録する。AquaDash で CSM を使うと決めたら、その前に直すかを判断する。
- PerLight を将来消すか(P3 で AquaDash が Cascaded に移ったら検討する)。

## 5. オープン課題

1. **分割画面ではビュー 0 のカスケードを全ビューが使う**(§0.2)。使うゲームが出たら、ビューごとに CB を分ける(Frame scope の影を View scope にする)必要がある。
2. **2〜4 本目のディレクショナルライトは Cascaded で影なし。** 複数のライトに影が要るなら、配列を増やしてライト × カスケードにする。
3. **PCF の幅はテクセル単位で全カスケード共通**なので、遠いカスケードほどワールドでのぼけが大きい。
4. 影の中でフォワードの半透明は影を落とさない(今と同じ。`HardShadowRenderer` のコメント)。

---

## 6. チェックポイント

- [ ] P1 の評価を全て満たした
- [ ] P2 の評価を全て満たした
- [ ] P3 の評価を全て満たした
- [ ] 抽象 IF(`IRenderContextImpl` / `IGraphicsDeviceImpl`)への追加が 0 本
- [ ] 新規 `.h/.cpp` が `.vcxproj` / `.filters` に登録されている(`ShadowCascade` 1 組)
- [ ] `mode` の既定(PerLight)で、P1 前と同じ絵になる(P1 の評価)
- [ ] README「既知の課題」の更新(cascade の名前を解消済みに・スキンメッシュの影を追加)
- [ ] [エンジン不足機能メモ.md](エンジン不足機能メモ.md) §5 の CSM の行を本書へのリンクに置き換えた
- [ ] 設計と実装の食い違いを本書へ反映し、`対象コミット` を更新した
