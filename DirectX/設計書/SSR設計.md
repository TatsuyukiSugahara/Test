# SSR(Screen Space Reflection)設計

> 対象コミット: 71cbd34 / 最終更新: 2026-11-11(設計のみ。未実装。Codex レビュー 2 回分を反映。1 回目で合成方式を差分加算に変えた)

対象: `aqEngine/Rendering/SSR/` + `aqEngine/Rendering/Pipeline/Passes/SSRPass.*` + `aqEngine/Assets/Shader/SSRTrace.fx` / `SSRComposite.fx` +
`PBRFunctions.fx`(IBL の鏡面部分の括り出し)+ `DeferredRenderer`(そのビューで IBL を使ったかの記録)+ `PipelinePresets` + Sample の床。
**全バックエンド(D3D11 / D3D12 / Vulkan / Metal)**で動かす。Mac で確認できるのは Metal / Vulkan。

一次資料の分担:
- **本書** … SSR の方針決定・新規/変更ファイルの責務・フェーズ計画・チェックリスト。
- [IBL設計.md](IBL設計.md) … IBL(SSR が外れたときの戻り先)。`EvalIBL` の式・`IBLCB`・`PSMainIBL` は向こうが正。本書はその差分だけを書く。
- [レンダーパイプライン設計.md](レンダーパイプライン設計.md) … パス列方式とパス間契約。本書は SSR パスを 1 本足す差分だけを書く。
- [エンジン不足機能メモ.md](エンジン不足機能メモ.md) §5 … 本機能を入れる判断。

---

## 0. 現状と方針

### 0.1 現状(実コードで確認)

- **深度は worldPos(GBuffer2)からしか取れない。** GBuffer0 の深度(D24S8)は `IRenderTarget` に深度 SRV の口が無く
  (`GetRenderTargetSRV` / `GetRenderTargetUAV` だけ)、シェーダから読めない。GBuffer1〜3 は `R16G16B16A16_Float`
  (`DeferredRenderer.cpp` の `CreateTargets`)なので、**worldPos は絶対座標の half 精度**。量子化誤差は**世界座標の成分の大きさに比例**し
  (原点から 1000 単位で約 0.5)、カメラに近い面でも原点から遠ければ粗い。MotionBlur も Hi-Z も同じ worldPos を使っている。
- **Hi-Z はそのままでは使えない。** `HiZRenderer` は **max-reduction(各レベル = 最遠深度)**で、レベル 0 が 1/2 解像度、
  各レベルが別々の RT(`levels_.handles[kMaxLevels]`)。SSR の階層トレースに要るのは **最近深度(min)**のピラミッドで、
  さらに全レベルを 1 本のミップ付きテクスチャとして読めないと、トレース中にレベルを切り替えられない。
- **compute でシーンを読み、別 RT に書く前例がある。** `MotionBlurPassCommand` は Scene の SRV と WorldPos の SRV を t0/t1、
  出力 RT の UAV を u0 に置き、`IsComputeSupported()` で可否を判定する。出力 RT は `CreateOffscreenRenderTarget`
  (`R16G16B16A16_Float`)で作り、リサイズは `PrepareResize` / `CommitResize` / `AbortResize`。
- **加算ブレンドは `src.rgb × src.a + dst.rgb`**(`BlendMode::Additive`。D3D11 の `SrcBlend = SRC_ALPHA / DestBlend = ONE`、
  Metal の `sourceRGBBlendFactor = SourceAlpha` など 4 バックエンドとも同じ)。
- **シーン RT は `R16G16B16A16_Float`**(メイン RT。`MetalGraphicsDeviceImpl.mm` ほか)。float の RT のブレンドは
  D3D11/12・Vulkan・Metal とも**クランプされない**ので、負の値を加算すれば差し引きになる(P1 で実際に確かめる。§2 P1)。
- **`PipelineBuilder::Build` は `IsSupported()` が false のパスを列から外す**(compute 非対応環境など)。
- **ビューごとにパス列を回す**(`RenderPipeline::BuildViews` は外側がビュー、内側がパス)。同じビューの中で
  `DeferredLightingPass` → 後続パスの順に `Build` が呼ばれる。
- **IBL の鏡面項はライティングの中で足されている**(`EvalIBL` の `specIBL`。[IBL設計](IBL設計.md) §3)。
  IBL が使えない間(ロード中・失敗)は `PSMain` で、鏡面キューブの項は無い。

### 0.2 確定した方針

| 項目 | 決定 | 理由 |
|---|---|---|
| トレース方式 | **画面空間の線形マーチ + 二分探索で詰める**(compute、全解像度)。当たり判定は §0.4 | Hi-Z が最遠深度で用途が合わない(§0.1)。min のピラミッドを別に作るのは 1 フェーズ分の仕事になる。まず線形で品質と負荷を見て、足りなければ §5 課題 1 で階層化する |
| 合成のしかた | **ライティングは変えない**(IBL の鏡面キューブの項も今までどおり足す)。SSR は**差分**だけを加算する: `確信度 × (当たった画素の色 − 鏡面キューブ × iblIntensity) × (F0·A + B)`。合成 PS の出力 α は **1 に固定** | 確信度 1 なら鏡面項がキューブから当たった色に置き換わり、0 なら何も足さない。ライティングを変えないので、①トレースが読むシーンに当たった面自身の鏡面反射も入っている(金属が暗く映らない)、②SSR を外せば(パス除外・デバッグで OFF・生成失敗)絵が IBL だけのときと完全に同じ。負の値の加算は float RT でクランプされない(§0.1)ことを前提にする |
| 当たった色に IBL の強度を掛けるか | **掛けない**(キューブ側だけに強度を掛けて差し引く) | ライティングが足したのは `キューブ × 強度 × (F0·A + B)` なので、それを引いて、画面に実際に映っている色で置き換える |
| SSR を動かす条件 | **そのビューのライティングが IBL(`PSMainIBL`)で描いたときだけ**合成する。`DeferredRenderer::BuildLightingCommandList` が「このビューで IBL を使ったか」を記録し(`WasIBLUsed()`)、同じビューの `SSRPass::Build` が見る | IBL が使えない間はライティングにキューブの項が無いので、差し引くと誤る。`IsReady()` を 2 つのパスで別々に見ると、間で切り替わったフレームに食い違う。ビュー内のパス順(§0.1)で判断を 1 か所にまとめる |
| 置く位置 | **`DeferredLightingPass` の直後、`SkyPass` の前**(Standard のみ) | 当たった色はライティング済みのシーンから読む。空の画素は worldPos が無いので「当たり」にならず、外れ = 何も足さない(キューブのまま)。合成は G-Buffer の画素だけを `clip` で描くので、空より前でも後でもよいが、空より前なら Sky の深度テストとも無関係 |
| 対象にしない物 | フォワード(半透明・海・パーティクル)は**映らないし、映り込みも受けない** | G-Buffer に載っていない。§5 課題 3 |
| 荒さ | **roughness 0.5 以上は SSR を使わない**(0.3〜0.5 で確信度を下げる) | 線形マーチは 1 本のレイなので、荒い面のぼけた映り込みは作れない。荒い面はキューブの前置フィルタのほうが正しい |
| 有効化 | `RendererPreset::enableSSR`(**既定 false**)。`enableDeferred && enableIBL` が前提。P3 でゲームごとに判断 | 「既定値なら従来と同じ絵」を守る([IBL設計](IBL設計.md) と同じ扱い) |
| 実行時の調整値 | パラメータ(強度・最大距離・厚み・デバッグ表示など)は**コマンド生成時に値で写す**(メインスレッド)。レンダースレッドは写した値と不変の資源だけを読む | [IBL設計](IBL設計.md) §1.1 と同じ。デバッグ UI からの変更が実行中のコマンドと競合しない |

### 0.3 確信度(0〜1)

当たった画素の色をどれだけ信じるか。次の積:

1. **当たり判定**(§0.4): 当たらなければ 0。
2. **画面端**: 当たった画素がビュー矩形の端から一定幅(既定 10%)に入るほど 0 へ。
3. **距離**: レイの長さ(ワールド単位)が最大距離(既定 50)に近づくほど 0 へ(最後の 20% で線形に)。
4. **荒さ**: roughness 0.3 → 0.5 で 1 → 0。
5. **カメラ向きのレイ**: ビュー空間で反射ベクトルの z が負(カメラ側)になるほど 0 へ(-0.2 → 0 の範囲で線形)。

### 0.4 当たり判定(トレースの手順)

ビュー空間の深度を `z`(カメラから前方へ正)とする。

1. **出発点**: 画素の worldPos を `P`、法線を `N` として、`P + N × bias` から出る(`bias` は §0.5 の誤差幅の 2 倍)。自分自身に当たらないため。
2. **終点**: `P + R × 最大距離`。ビュー空間で near 面の手前に出る場合は near 面で切る。両端を画面座標に投影し、その間を
   **最大 64 ステップ**で等間隔に進む(1 ステップは最低 1 画素)。レイの深度は 1/z を画面上で線形補間して求める(透視補正)。
3. **各ステップの判定**: サンプル画素が
   - **ビュー矩形の外** → 外れで打ち切り。
   - **背景(pixelTag < 0.5)** → 遮る物が無いので進み続ける。
   - それ以外は、その画素の worldPos から `zScene` を作り、`Δ = zRay − zScene` を見る。
     - `Δ < 0`(レイが面の手前)→ 進み続ける。
     - `0 ≤ Δ ≤ 厚み`(§0.5)→ **当たり候補**(このステップの位置を `hi` とする)。
     - `Δ > 厚み`(レイが物の裏側を通過している)→ 当たりにせず進み続ける(輪郭の偽ヒットを避ける)。
4. **二分探索(候補を詰める)**: **直前のステップが「手前」(`Δ < 0` か背景)だったときだけ**、その位置を `lo` として `lo`〜`hi` を
   **6 回**詰める。中点が「手前」(`Δ < 0` か背景)なら `lo = 中点`、それ以外(`Δ ≥ 0`。厚み超過を含む)なら `hi = 中点`。
   直前のステップが「手前」でない(厚み超過の直後・出発直後)ときは詰めずに `hi` をそのまま使う。
5. **候補の棄却**: 最終的な `hi` で `0 ≤ Δ ≤ 厚み` を**もう一度判定**し、外れたら外れ(輪郭の不連続をまたいで詰めた場合)。
   さらに `hi` の画素の法線が `dot(N_hit, R) > 0`(面の裏に当たった)なら外れ。
6. **結果**: 当たったら出力 RT に `rgb = その画素の Scene の色`、`a = 確信度`。外れたら (0,0,0,0)。

### 0.5 厚みと誤差幅(half 精度の worldPos への手当て)

- **誤差幅** `e = 2^-10 × max(|P.x|, |P.y|, |P.z|) × 2`(half の仮数 10 bit。世界座標の成分の大きさで決まる。§0.1)。
- **厚み** `= max(厚み係数 × zScene, 4 × e_hit)`(`e_hit` はサンプル画素の worldPos から同じ式で求める)。既定の厚み係数は 0.02。
- 原点から遠い場所(AquaDash のコース後半)では誤差幅が厚みを上回り、当たり判定が粗くなる。P3 の AquaDash 確認で見る。
  根本的には worldPos の精度を上げるか、深度を 32bit で持つ必要がある(§5 課題 5)。

### 0.6 落とし穴(実装前に読むこと)

1. **合成 PS の出力 α は必ず 1。** 加算は `src.rgb × src.a + dst` なので、確信度を α に入れると差分が二重に弱まる(確信度は rgb に掛けて済ませる)。
2. **トレースは Scene を SRV で読み、合成は Scene に描く。** 同じ RT を同時に読み書きしない。トレースの compute は
   `OMSetRenderTargets(0, nullptr)` で RTV を外してから回す(`MotionBlurPassCommand` と同じ)。合成は SSR の出力 RT と G-Buffer を読む。
3. **行列の規約は `MotionBlur.fx` に合わせる**(`mul(M, v)`。C++ 側は `Mull(view, projection)` をそのまま渡している)。
4. **ビューポート。** SSR パスは View scope。トレースはビュー矩形の中だけを dispatch し(CB に矩形を渡す)、当たり判定も矩形の中に限る。
5. **`WasIBLUsed()` を見ずに合成しない**(§0.2)。
6. `.fx` を足したら `shader_entries.txt` にも足す。

---

## 1. 責務

| ファイル | 責務 |
|---|---|
| `aqEngine/Assets/Shader/SSRTrace.fx` **(新規)** | compute(8×8)。§0.4 / §0.5 の手順で反射レイを追い、出力 RT に `rgb = 当たった画素の Scene の色`、`a = 確信度`(§0.3)を書く。G-Buffer の無い画素・roughness 0.5 以上は (0,0,0,0) |
| `aqEngine/Assets/Shader/SSRComposite.fx` **(新規)** | フルスクリーン三角形(VS は `PBRLighting.fx` と同じく z = 0、深度無効)。PS は G-Buffer の画素だけ(`pixelTag` で `clip`)、`ssr.a × (ssr.rgb − 鏡面キューブ × iblIntensity) × (F0·A + B)` を rgb、**α = 1** で返す(`BlendMode::Additive`)。デバッグ表示が有効なら確信度をグレーで返し、コマンド側は `BlendMode::Opaque` で描く(上書き) |
| `aqEngine/Assets/Shader/PBRFunctions.fx` | `EvalIBL` の鏡面部分を `EvalIBLSpecular(N, V, baseColor, metallic, roughness, specular, specCube, brdfLut, s, maxMip, out float3 prefiltered, out float3 specWeight)` に括り出し、`EvalIBL` と `SSRComposite.fx` が共用する。**`EvalIBL` の結果は変えない**(`PSMainIBL` の画が変わらないこと。P1 で画素一致を確かめる) |
| `aqEngine/Rendering/Deferred/DeferredRenderer.{h,cpp}` | `BuildLightingCommandList` で `PSMainIBL` を積んだかを `lastIBLUsed_` に記録し、`WasIBLUsed()` で返す。メインスレッドだけで読み書きする |
| `aqEngine/Rendering/SSR/SSRRenderer.{h,cpp}` **(新規)** | トレースの CS・合成の VS/PS・サンプラ・出力 RT(`R16G16B16A16_Float`、全解像度)の所有、パラメータ(有効・最大距離・厚み係数・ステップ数・画面端の幅・roughness のしきい値・デバッグ表示)の保持、リサイズ(`PrepareResize` / `CommitResize` / `AbortResize`。`MotionBlurEffect` と同じ作法)。`BuildCommandList` でトレースと合成のコマンドを積む。**生成に失敗しても落とさない**。デバッグ表示(確信度の可視化)は P2 で入れる(評価に使うため) |
| `aqEngine/Rendering/SSR/SSRTraceCommand.{h,cpp}` / `SSRCompositeCommand.{h,cpp}` **(新規)** | `IRenderCommand` 派生。RT はハンドルで持ち `Execute` で解決する(`MotionBlurPassCommand` と同じ)。パラメータと IBL の強度は値で持つ。合成は `IBLEnvironment` を `shared_ptr` で持つ([IBL設計](IBL設計.md) §1.1 と同じ寿命の理由)。CB は `fc.perDrawCBPool` から 192B で取る |
| `aqEngine/Rendering/Pipeline/Passes/SSRPass.{h,cpp}` **(新規)** | `SSRRenderer` を包むパス。View scope。`IsSupported()` は `IsComputeSupported()`。宣言は `Reads(GBuffer0..3)`・`Writes(Scene)`。`DeferredRenderer` と `IBLEnvironment` を `shared_ptr` で共有し、**SSR が有効 && `WasIBLUsed()`** のビューでだけコマンドを積む。`ResizePolicy::Resizable` |
| `aqEngine/Rendering/Pipeline/PipelinePresets.{h,cpp}` | `RendererPreset::enableSSR`(既定 false)。`Standard()` で `enableDeferred && enableIBL && enableSSR` かつ IBL の生成に成功したとき、`DeferredLightingPass` の直後に `SSRPass` を足す。`SSRRenderer` の生成に失敗したら足さない |
| `aqEngine/Assets/Shader/shader_entries.txt` | `SSRTrace.fx main cs` / `SSRComposite.fx VSMain vs` / `SSRComposite.fx PSMain ps` の 3 行 |
| `aqEngine/Rendering/Deferred/Debug/DeferredDebugPanel.cpp` か SSR 専用パネル | P3: SSR の ON/OFF・最大距離・厚み係数・確信度の可視化の切り替え |
| `Sample/Application/SampleModule.cpp` | P2: 見本列の下に反射する床(`BoxStaticMeshComponent` を平たく伸ばし `SetPBRMaterial(1.0f, 0.05f)`)を置き、`enableSSR = true` |
| `Engine.vcxproj` / `.filters` | 新規ファイルの登録(`vs-project-files`。**フィルターは実装前にユーザーへ確認する**) |

`SSRCBData`(192B 固定。`perDrawCBPool` から取る): `viewProj`、`view`、ビューの矩形(x, y, w, h)、画面全体の寸法、最大距離、
厚み係数、ステップ数、二分探索の回数、画面端の幅、roughness のしきい値 2 つ、デバッグ表示。具体の並びは実装時に決め、
HLSL の cbuffer と static_assert で一致させる。

---

## 2. フェーズ計画

### P1: 差分合成の土台(トレースなし)

実装:
- `EvalIBLSpecular` の括り出し、`DeferredRenderer::WasIBLUsed`、`SSRComposite.fx`、`SSRCompositeCommand`、
  `SSRRenderer`(合成だけ)、`SSRPass`、`PipelinePresets` の配線。
- この段階では**トレースの代わりに、合成シェーダへ固定の入力を与えるテスト用の切り替え**(`SSRRenderer` の `testMode`。
  0 = 通常(P1 ではトレースが無いので ssr = (0,0,0,0) 扱い)、1 = `ssr = (0,0,0,1)`)を持たせる。P2 で残すかは実装時に判断し、残すならデバッグ用と明記する。

評価(自動。Mac / Metal と Vulkan):
- [ ] ビルドが通り、警告が増えない
- [ ] **`EvalIBL` の括り出しで `PSMainIBL` の画が変わらない**: `enableSSR = false` で、P1 前のコミット(+ Sample の一時差分)と画素一致
      ([IBL設計](IBL設計.md) P2 と同じ比較条件。オーバーレイ (0,0)-(460,200) を除いて差 0)
- [ ] **差分が 0 のとき何も変わらない**: `enableSSR = true`・`testMode = 0` で、`enableSSR = false` と画素一致(差 0。足すのは 0 × … = 0)
- [ ] **負の加算が効く(float RT でクランプされない)**: ポストプロセスを切った一時差分(`enablePostProcess = false`。Bloom が段をまたいで
      明るさを広げないように)で、次の 2 つを比べる。見本列の**金属の段**の各面の中央 16×16 の平均を使う。
      - `testMode = 1`(当たった色 = 黒、確信度 1 = キューブ由来の鏡面を全部差し引く)の金属の段が、**IBL なし(`enableIBL = false`)と各面 2/255 以内**
        (金属は IBL の拡散が 0 なので、差し引けば直接光だけが残る)
      - 誤合格の防止: `testMode = 0` の金属の段は IBL なしと **20/255 以上**違う(差し引く前は鏡面キューブの分だけ明るい)
      非金属の段は IBL の拡散が残るので比べない
- [ ] **IBL が使えない間は合成しない**: IBL の鏡面ファイルを存在しないパスにした起動で、`testMode = 1` でも画が `enableIBL = false` と画素一致
      (`WasIBLUsed()` が false なので合成しない)
- [ ] **ロード境界で判断が食い違わない**: 一時ログ(評価後に外す)で、起動から 120 フレームの各ビューについて
      「ライティングが `PSMainIBL` を積んだか」と「SSR が合成を積んだか」を 1 行ずつ出し、**全フレームで一致**し、かつ
      **0 → 1 に切り替わるフレームが含まれる**(IBL のロード完了をまたいでいる)ことを確かめる
- [ ] Metal API Validation エラー 0 / VK validation(Debug)エラー 0 / 終了コード 0

### P2: トレース

実装:
- `SSRTrace.fx`(§0.3〜§0.5)、`SSRTraceCommand`、出力 RT とリサイズ、確信度のデバッグ表示。
- Sample に反射する床を置き、`enableSSR = true`。

評価(自動):
- [ ] ビルドが通り、警告が増えない
- [ ] Sample で、床に見本列の箱が映る: 床の、箱の真下に当たる画素の色相が箱の色 (0.2, 0.6, 1.0) に寄り(B > G > R、かつ SSR OFF の同じ画素より
      G/B が箱の値に近づく)、列の外側の床は SSR OFF との差が 2/255 以内。座標は撮影画像から実測して評価記録に書く
- [ ] 確信度の可視化で、**roughness 0.5 以上の箱(見本列の右 3 列)の面が 0(黒)**。可視化は合成 PS が G-Buffer の画素に
      確信度のグレーを `BlendMode::Opaque` で上書きする経路とし、ポストプロセスを切った一時差分で撮る
      (空は合成で `clip` され Sky が描くので、可視化の対象外)
- [ ] SSR の ON/OFF で、Bloom・トーンマップを切った構成(`enablePostProcess = false` の一時差分)のとき、**床と箱の画素以外**が差 0
      (Bloom が明るさの変化を周辺へ広げるので、ポストプロセスありでは比べない)
- [ ] 1280×752 の Release で 60 FPS を保つ(FPS オーバーレイの値。Sample と AquaDash で `enableSSR` を一時的に true にして各 10 秒)
- [ ] Metal API Validation エラー 0 / VK validation(Debug)エラー 0 / 終了コード 0

評価(ユーザー確認):
- [ ] 静止画の映り込みが破綻していない(縞・箱から浮いた映り込み・画面端での急な途切れ、が目立たない)。
      時間方向(ちらつき)は Sample のカメラが動かないので P3 の AquaDash で見る

### P3: 調整とデバッグ UI

実装:
- デバッグ UI(ON/OFF・最大距離・厚み係数・確信度の可視化)。
- 既定パラメータを決める。AquaDash で `enableSSR` を有効にするかをユーザーに確認し、決めた方を反映する。

評価(自動):
- [ ] デバッグ UI で OFF にした画と、`enableSSR = false` で起動した画が画素一致する(パネルとメニューバーを除く)

評価(ユーザー確認):
- [ ] Sample の床の映り込み(既定パラメータ)
- [ ] AquaDash で有効にした場合の見た目。**走行中のちらつき**を見るため、`evthold` で加速しながら 0.2 秒おきに 10 枚撮って並べる
      (memory の `mac-visual-verification` の手順)。コース後半(原点から遠い)の当たり判定の粗さもここで見る(§0.5)

Windows(D3D11 / D3D12)の確認は Windows 機の宿題に積む。

---

## 3. テスト方針のまとめ

- 画素比較は [IBL設計](IBL設計.md) P2 と同じ条件(Sample は静止画。オーバーレイ (0,0)-(460,200) を除く)。基準画像は
  「変更前のコミットの worktree に同じ一時差分を当てる」方式で作る。
- Vulkan の validation は Debug 構成でしか効かない(`VulkanGraphicsDeviceImpl.cpp` の `#ifdef _DEBUG`)。
- 見た目(映り込みの自然さ、ちらつき)はユーザー確認に残す。

---

## 4. 決めていないこと

- 出力 RT を半解像度にするか(P2 の負荷計測で判断。全解像度で 60 FPS を割るなら半解像度 + 合成時の拡大にする)。
- `testMode` を P2 以降に残すか(P1 の評価のためだけに入れる)。

## 5. オープン課題

1. **階層トレース(Hi-Z)。** 最近深度のミップ付きピラミッドを別に作れば、ステップ数を減らして長いレイを追える。
   今の `HiZRenderer`(最遠深度・レベルごとの別 RT)は遮蔽カリング用のまま残す。
2. **荒い面のぼけた映り込み。** 今は roughness 0.5 以上を切り捨ててキューブに任せる。ぼかすなら、当たり位置の色を
   ミップ付きでサンプルするか、出力 RT を roughness に応じてぼかす。
3. **フォワードの物(海・半透明・パーティクル)は映らない。** 海の映り込みは海のシェーダ側の話。
4. **時間方向の安定化(TAA と同じ仕組み)は無い。** 走行中のちらつきが目立つなら、不足機能メモ §5 の AA(TAA)と一緒に扱う。
5. **worldPos が絶対座標の half 精度**(§0.5)。原点から遠い場所の当たり判定が粗い。深度 SRV を読めるようにするか、
   ビュー空間 z を 32bit で別に書けば改善する。
6. **当たった画素の色は「その画素から見た色」**(視点依存の鏡面反射を含む)で、反射経路から見た色ではない。画面空間の手法の一般的な近似。

---

## 6. チェックポイント

- [ ] P1 の評価を全て満たした
- [ ] P2 の評価を全て満たした
- [ ] P3 の評価を全て満たした
- [ ] 抽象 IF(`IRenderContextImpl` / `IGraphicsDeviceImpl`)への追加が 0 本
- [ ] `shader_entries.txt` に 3 行が登録されている
- [ ] 新規 `.h/.cpp` が `.vcxproj` / `.filters` に登録されている
- [ ] SSR の生成に失敗しても起動できる(SSR なしの絵で続行する)
- [x] [エンジン不足機能メモ.md](エンジン不足機能メモ.md) §5 の SSR の行を本書へのリンクに置き換えた(2026-11-11)
- [ ] 設計と実装の食い違いを本書へ反映し、`対象コミット` を更新した
