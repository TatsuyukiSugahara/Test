# Metal バックエンド設計

> 対象コミット: 7625249 / 最終更新: 2026-09-11

対象: `aqEngine/Graphics/Metal/`(新規)。macOS(Apple Silicon)でネイティブ Metal 描画を行う。
前提: `Mac移植設計.md` の P0〜P4b(足回り・入力・音・ImGui)が完了済み。本書はその **P6** にあたる。
本書は設計のみを扱い、実装コードは含まない。
姉妹文書: [VulkanBackend設計.md](VulkanBackend設計.md) / [D3D12Backend設計.md](D3D12Backend設計.md) / [Mac移植設計.md](Mac移植設計.md)。

---

## 0. 設計の指針

1. **抽象レイヤは不変**。`IGraphicsDeviceImpl` / `IRenderContextImpl` への**追加は 0 本**を目標とする
   (D3D12 / Vulkan の実績)。
2. **差分は `MetalRenderContextImpl` に閉じ込める**。エンジン側の描画コードは 1 行も変えない。
3. **段階的に積む**。三角形 1 枚 → バッファ/テクスチャ → ステージ一式 → compute → ImGui の順で、
   各段で実機確認を挟む(§12)。
4. **`.fx` は無改変**。Vulkan と同じ HLSL ソースから、シフト値だけ変えて `.spv` を作る(§9)。

### 0.1 確定した方針

| 項目 | 決定 | 理由 |
|---|---|---|
| 言語 | **Objective-C++(`.mm`)**。metal-cpp は使わない | 出荷実績のあるエンジン(Godot `rendering_device_driver_metal.mm` / bgfx `renderer_mtl.mm` / MoltenVK / UE / Unity)はいずれも ObjC++。既存の `Platform/Mac`・`HID/Mac`・`Sound/CoreAudio` と作法が揃う。新 API が出た日から使える |
| 参照カウント | **MRR(手動 retain/release)**。ARC は使わない | 既存の Mac コード(`PlatformMac.mm` 等)が MRR。混在させない |
| ヘッダの ObjC 型 | **出さない**。不透明構造体 `struct MetalObjects;` を前方宣言し、定義は `.mm` に置く | `Engine.cpp` は素の `.cpp` のまま `MetalGraphicsDeviceImpl.h` を include して `GraphicsDevice::Create<>` を呼ぶ。`PlatformMac.h` と同じ手 |
| シェーダ | **ビルド時に `.metal`(MSL)を生成し、実行時に `newLibraryWithSource:` でコンパイル** | `xcrun metal` が使えない(§0.2)。Windows が実行時 DXC なのと同じ構図 |
| binding 写像 | **Metal 専用の dxc シフト `b:0 / t:8 / s:0 / u:24`** + `spirv-cross --msl-decoration-binding` | Metal はバッファ/テクスチャ/サンプラが**別の番号空間**。Vulkan の積み上げシフト(b:0/t:16/s:32/u:48)は `sampler(32)` が上限 16 を超えて破綻する(§0.2)。リフレクション不要の 1 対 1 写像になる |
| 深度フォーマット | **`Depth32Float`**(`D24_Unorm_S8_Uint` は使わない) | Apple Silicon が D24S8 非対応(§0.2)。Vulkan バックエンドも既に D32_SFLOAT |
| BC 圧縮 | **そのまま使う**。再エンコードしない | Apple Silicon が BC1〜BC7 に対応(§0.2) |
| アップロード | **`MTLStorageModeShared` へ直接書く**。ステージングバッファを作らない | ユニファイドメモリ。VMA 相当の仕組みが丸ごと不要 |
| 抽象IF追加 | **0 本**を目標 | Vulkan が `BeginFrame`/`EndFrame` を遅延発火で吸収した手をそのまま使う(§2.3) |

### 0.2 事前検証の結果(2026-09-11 / Apple M5 / macOS 26.6.2)

着手前に、設計の前提が成り立つかを**実機で確かめた**。`Mac移植設計.md` §7 の当初案は 2 点で成り立たない。

| 検証 | 結果 |
|---|---|
| `spirv-cross`(Vulkan SDK 同梱)の有無 | **あり**。`~/VulkanSDK/1.4.357.1/macOS/bin/spirv-cross` |
| 現行 59 本の `.spv` → MSL 変換 | **59/59 成功** |
| MSL の実行時コンパイル(`newLibraryWithSource:`) | **59/59 成功**。全部で約 2 秒(1 本 ≒ 34ms) |
| `xcrun metal`(ビルド時 `.metallib`) | **使えない**。Metal Toolchain 未導入(`xcodebuild -downloadComponent MetalToolchain` が要る) |
| Vulkan のシフト規約(b:0/t:16/s:32/u:48)をそのまま流す | **破綻**。`sampler(32)` が Metal の上限(0〜15)超過で **25/59 が失敗** |
| Metal 専用シフト(b:0/**t:0**/s:0/u:16)+ `--msl-decoration-binding` | 58/59 成功。**残り 1 本は b/t の衝突で壊れた MSL になる**(§5.1)|
| **Metal 専用シフト(b:0/t:8/s:0/u:24)+ `--msl-decoration-binding`** | **59/59 成功**(P0.5 で確定) |
| `.fx` の実レジスタ使用 | `b` ≤ 4 / `t` ≤ 11 / `s` ≤ 1 / `u` ≤ 1。Metal の上限に**大きく余裕がある** |
| `supportsBCTextureCompression` | **YES**。BC1/3/4/5/6H/7 のテクスチャ生成も全て成功 |
| `isDepth24Stencil8PixelFormatSupported` | **NO**。`Depth32Float` / `Depth32Float_Stencil8` は生成成功 |
| `argumentBuffersSupport` | **Tier 2** |
| `hasUnifiedMemory` | **YES**。`maxBufferLength` 8.9GB |
| 生成 MSL の頂点入力 | `[[attribute(location)]]` が SPIR-V の location と一致。インスタンス属性は `in_var_I_*` で、`VulkanShader::BuildInputLayout` の `I_` 判定がそのまま効く |

**結論**: ビルド時 `.metallib` と Vulkan シフト流用の 2 つを諦めれば、**追加ダウンロード無しで着手できる**。

---

## 1. アーキテクチャ全体像

```
    RenderContext (Abstraction)          GraphicsDevice (Abstraction)
            │                                      │
            ▼                                      ▼
    IRenderContextImpl                     IGraphicsDeviceImpl
            │                                      │
    MetalRenderContextImpl  ◀── 参照 ──▶  MetalGraphicsDeviceImpl
            │                                      │
            ├─ MetalPipelineCache                  ├─ CAMetalLayer / drawable
            ├─ MetalDepthStencilCache              ├─ MTLDevice / MTLCommandQueue
            └─ 現在の MTLRenderCommandEncoder      └─ フレーム資源(CB リング / セマフォ)
```

Vulkan 内部サブシステムとの対照:

| Vulkan | Metal | 備考 |
|---|---|---|
| `VulkanPipelineCache` | `MetalPipelineCache` | `MTLRenderPipelineState` を遅延生成してキャッシュ |
| `VulkanPipelineLayout` | **不要** | Metal は index で直接束ねる。DescriptorSetLayout に相当するものが無い |
| descriptor pool / set | **不要** | `setVertexBuffer:offset:atIndex:` 等で直接 |
| image layout / barrier | **不要** | エンコーダ内は Metal が自動追跡。パス跨ぎだけ §8 |
| `VulkanVMAImpl`(VMA) | **不要** | ユニファイドメモリに直接確保 |
| swapchain 手組み | **不要** | `CAMetalLayer.nextDrawable` |
| (なし) | **`MetalDepthStencilCache`** | `MTLDepthStencilState` は PSO と別オブジェクトなので専用キャッシュが要る |

---

## 2. フレームライフサイクルと GPU 同期

### 2.1 frames-in-flight

`FRAME_COUNT = 2`。Vulkan と同じ。各フレームが持つのは
「定数バッファのリング領域」と「そのフレームの `MTLCommandBuffer`」だけ
(コマンドプールも fence も Metal では要らない)。

### 2.2 同期

`dispatch_semaphore_t`(初期値 `FRAME_COUNT`)1 本で足りる。

```
BeginFrame : dispatch_semaphore_wait(sem)
             drawable = [layer nextDrawable]
             cmdBuf   = [queue commandBuffer]
Present    : [cmdBuf presentDrawable:drawable]
             [cmdBuf addCompletedHandler:^{ dispatch_semaphore_signal(sem); }]
             [cmdBuf commit]
```

`WaitIdle()`(抽象IFの既定 no-op を上書き)は最後の `MTLCommandBuffer` の
`waitUntilCompleted` で実装する。**終了時と実行時リソース破棄の両方で必要**
(`Mac移植設計.md` §8-19 / §8-20 と同じ穴を踏まないため)。

### 2.3 抽象レイヤとの接続点

抽象に `BeginFrame`/`EndFrame` は無い。Vulkan と同じく
**最初の描画コマンドが来た時点で遅延発火**する `BeginFrameIfNeeded()` を
`MetalGraphicsDeviceImpl` に持ち、`Present()` で閉じる。これで**抽象IF追加 0 本**を保つ。

### 2.4 クリップ空間と Y 軸

Metal の NDC は **D3D と同じ**(Y 下向き・Z が [0,1])。
Vulkan で必要だった負ビューポートによる Y-flip も `frontFace` の補正も**要らない**。
テクスチャ座標の原点も D3D と同じ左上。**この点は Vulkan より素直**。

> **例外が 1 つだけある**(P6 で判明): `ImGuiVK.fx` は**唯一 Vulkan NDC 前提で書かれた `.fx`**
> で、D3D 系の行列を通さず imgui 座標から直接 NDC へ写像している。Metal でそのまま同じ定数を
> 渡すと **UI が上下逆さま**になるので、`MetalImGui` 側が投影定数で吸収している(`.fx` は無改変)。

---

## 3. コマンド記録(`MetalRenderContextImpl`)

### 3.1 エンコーダの寿命管理(最重要)

Metal は **1 レンダーパス = 1 `MTLRenderCommandEncoder`** で、エンコーダを開いた後に
レンダーターゲットを差し替えられない。一方エンジンの API は D3D11 由来のイミディエイト風で、
`OMSetRenderTargets` がフレーム途中に何度も来る。

**方針**: 「現在のアタッチメント構成」を保留ステートとして持ち、
**アタッチメントが変わる操作が来たらエンコーダを閉じ、次の描画で開き直す**。

| 操作 | エンコーダへの影響 |
|---|---|
| `OMSetRenderTargets` / `OMSetMRTRenderTargets` / `OMSetRenderTargetWithDepth` / `OMSetDepthOnlyTarget[Slice]` | **閉じる**(構成が同一なら維持) |
| `ClearRenderTargetView` / `ClearDepthBuffer` / `ClearDepthMap[Slice]` | **閉じる**。次に開くときの `loadAction = Clear` + `clearColor` として予約する |
| `Dispatch` | **閉じる**(compute は `MTLComputeCommandEncoder` が要る) |
| それ以外(シェーダ/バッファ/テクスチャ/ビューポート/トポロジ) | 保留ステートを更新するだけ |

**クリアはエンコーダの `loadAction` に畳む**のが要点。Metal には「エンコーダ内で RT をクリアする」
コマンドが無いため、`ClearRenderTargetView` を独立コマンドとして実行しようとすると
空のエンコーダを 1 本開く羽目になる。予約して次のパスの `loadAction` にするのが素直で速い。

**ただし予約を RT 跨ぎで持ち越してはいけない**(P1 で判明)。`ClearRenderTargetView(index, ...)` の
`index` は「いまバインドされている構成の何番目のアタッチメントか」なので、予約したまま
`OMSet*` で構成が変わると**前の RT 宛てのクリア色が次の RT に紛れ込む**。
実際、`Application.cpp` のクリア色予約が DeferredRenderer の GBuffer 黒クリアに
上書きされて画面が黒くなる経路があった。

したがって **`FlushPendingClears()` の呼び出し元は 3 か所**にする:

1. `CopyToBackBuffer()` の blit 前
2. `Present()` のコミット前
3. **`OMSet*` 系でアタッチメント構成が変わる直前**

エンジンの Clear は D3D11 由来の**即時実行セマンティクス**なので、
「描画が無いまま RT を切り替えてもクリアだけは効く」が正しい挙動になる。
`FlushPendingClears()` は予約が無ければ即 return するので、
**描画があるパスでは Draw が開くエンコーダに畳まれ、この 3 か所はすべて no-op** になる
(二重にエンコーダは開かない)。

### 3.2 保留ステートと Draw 時 flush

Vulkan と同型。`Draw*` が来た時点で
「PSO をキャッシュから引く → エンコーダが無ければ開く → 束ね直し → 描画」を行う。

```
struct PendingGraphicsState
{
    // PSO キーになるもの
    MetalShader* vs; MetalShader* ps;
    PrimitiveTopology topology;
    BlendMode blend; DepthMode depth;
    MTLPixelFormat colorFormat[8]; uint32_t colorCount;
    MTLPixelFormat depthFormat;
    uint32_t vertexStride, instanceStride;

    // エンコーダに直接流すもの
    Viewport viewport; ScissorRect scissor; bool scissorEnabled;
    IVertexBuffer* vb[2]; IIndexBuffer* ib;
    IConstantBuffer* vsCB[16]; IConstantBuffer* psCB[16];
    IShaderResourceView* psSRV[16]; ISamplerState* psSampler[16];
    bool dirty...;
};
```

### 3.3 `IRenderContextImpl` メソッド → Metal 対応表

| 抽象メソッド | Metal 実装 |
|---|---|
| `OMSetRenderTargets` / `OMSetMRTRenderTargets` | エンコーダを閉じ、`MTLRenderPassDescriptor.colorAttachments[i]` を組み直す |
| `OMSetRenderTargetWithDepth` | 同上 + `depthAttachment` に相手 RT の深度テクスチャ |
| `OMSetDepthOnlyTarget[Slice]` | color 0 本 + `depthAttachment`(`slice` は `depthAttachment.slice`) |
| `OMSetDepthMode` / `OMSetBlendMode` | 保留ステート更新。前者は `MetalDepthStencilCache`、後者は PSO キーへ |
| `RSSetViewport` | `setViewport:`(`znear=0, zfar=1`) |
| `RSSetScissorEnabled` / `RSSetScissorRect` | `setScissorRect:`。無効時は RT 全面を指定(Metal に「無効」が無い) |
| `ClearRenderTargetView` / `ClearDepthBuffer` / `ClearDepthMap[Slice]` | §3.1 のとおり `loadAction = Clear` へ予約 |
| `IASetVertexBuffer` / `IASetVertexBufferSlot` | `setVertexBuffer:offset:atIndex:`(index は §5.2) |
| `IASetIndexBuffer` / `IASetIndexBufferGpu` | 保留。`drawIndexedPrimitives:` の引数として渡す |
| `IASetPrimitiveTopology` | `MTLPrimitiveType` へ写像。**ただし PSO 側の `inputPrimitiveTopology` は class(point/line/triangle)単位**なので両方に反映 |
| `IASetInputLayout` | VS のリフレクション結果(§9.3)から `MTLVertexDescriptor` を組む。PSO キーの一部 |
| `VSSetShader` / `PSSetShader` / `PSUnsetShader` | 保留ステート更新(PSO キー) |
| `VSSetConstantBuffer` / `PSSetConstantBuffer` | `setVertexBuffer:atIndex:slot` / `setFragmentBuffer:atIndex:slot`(§5.1) |
| `PSSetShaderResource` / `PSUnsetShaderResource` | `setFragmentTexture:atIndex:slot` |
| `PSSetSampler` | `setFragmentSamplerState:atIndex:slot` |
| `CSSetShader` / `CSUnsetShader` | compute PSO を保留 |
| `CSSet*` 各種 | `MTLComputeCommandEncoder` の `setBuffer/setTexture/setSamplerState` |
| `CSSetUnorderedAccessView` / `CSUnset...` | テクスチャ UAV は `setTexture`、バッファ UAV は `setBuffer`(§5.3) |
| `Draw` | `drawPrimitives:vertexStart:vertexCount:` |
| `DrawIndexed` (×2) | `drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:` |
| `DrawIndexedInstanced` | 同上 + `instanceCount:baseVertex:baseInstance:` |
| `DrawIndexedIndirect` | `drawIndexedPrimitives:...indirectBuffer:indirectBufferOffset:`。**引数レイアウトが D3D と異なる**(§13-3) |
| `Dispatch` | エンコーダを compute へ切り替えて `dispatchThreadgroups:threadsPerThreadgroup:` |
| `UavBarrier` | `memoryBarrierWithScope:MTLBarrierScopeBuffers`(同一エンコーダ内)。エンコーダを跨ぐなら no-op で足りる |
| `UpdateConstantBuffer` | フレームリングの該当領域へ `memcpy`(§6) |

---

## 4. パイプラインステートのキャッシュ

### 4.1 キー

```
struct MetalPipelineKey
{
    const void* vsFunction;   // id<MTLFunction>
    const void* psFunction;
    uint8_t     topologyClass;
    uint8_t     blendMode;
    uint8_t     colorCount;
    MTLPixelFormat colorFormat[8];
    MTLPixelFormat depthFormat;
    uint32_t    vertexStride;
    uint32_t    instanceStride;
};
```

`std::unordered_map<MetalPipelineKey, id<MTLRenderPipelineState>>` で索く。
**`DepthMode` はキーに入れない** — Metal では深度/ステンシルは PSO ではなく
`MTLDepthStencilState` という別オブジェクトで、エンコーダに独立に設定するため。
ビューポート/シザーも同様に PSO 外なので、PSO の組み合わせ爆発は Vulkan より小さい。

### 4.2 生成タイミング

Draw の flush 時に遅延生成。Metal の PSO 生成は実測で数 ms かかることがあるため、
**初回フレームのヒッチが問題になったら §12 の P6 で事前生成を検討**する。

### 4.3 `MetalDepthStencilCache`

`DepthMode`(3 値)→ `MTLDepthStencilState` の作り置き。起動時に 3 つ作って持つだけでよい。

---

## 5. リソースバインディング規約(最重要)

### 5.1 register → Metal index の写像

**Metal はバッファ / テクスチャ / サンプラが独立した番号空間を持つ**。
一方で、**中間生成物の SPIR-V は Vulkan の統一 binding 名前空間**である。
この 2 つの事情を両方満たす配り方をする。

| HLSL | dxc シフト(Metal 用) | SPIR-V binding | Metal |
|---|---|---|---|
| `b0..bN`(cbuffer) | `-fvk-b-shift 0 all` | 0..7 | `[[buffer(0+N)]]` |
| `t0..tN`(SRV) | `-fvk-t-shift 8 all` | 8..23 | `[[texture(8+N)]]` / バッファなら `[[buffer(8+N)]]` |
| `s0..sN`(Sampler) | `-fvk-s-shift 0 all` | 0..15 | `[[sampler(0+N)]]` |
| `u0..uN`(UAV) | `-fvk-u-shift 24 all` | 24..28 | `[[texture(24+N)]]` / バッファなら `[[buffer(24+N)]]` |

> **当初案(b/t/s/u をすべて 0 始まり)は誤りだった**(P0.5 で判明)。
> Metal 側の名前空間が別なので 0 始まりで良さそうに見えるが、**SPIR-V 上でバッファ同士が
> 衝突すると spirv-cross がバッファを別名化**し、
> `device void* spvBufferAliasSet0Binding0 [[buffer(0)]]` を作って
> `constant T*` へキャストする、**アドレス空間をまたぐ不正な MSL** を吐く。
> `ClusterCull.fx` が `StructuredBuffer` を `t0`、`cbuffer` を `b0` に置いているためこれに当たった。
>
> したがって **`b` / `t` / `u` は SPIR-V 上で重ならないように配る**。`t` と `u` は
> HLSL の型次第でテクスチャにもバッファにもなるため、どちらも `b` と分ける必要がある。
> **`s` だけは重なってよい**(サンプラはバッファと別名化しない)。

`--msl-decoration-binding` により SPIR-V の binding がそのまま Metal の index になるので、
**実行時のリフレクションも写像テーブルも要らない**。エンジンの
`PSSetShaderResource(slot, ...)` は `setFragmentTexture:atIndex:(SRV_INDEX_SHIFT + slot)` になる。

実測した index の使用状況(全 59 本):

| | 使用 | 最大 | Metal の上限 |
|---|---|---|---|
| buffer | 0,1,2,3,4,5, 8,9, 24,25 | **25** | 30(頂点バッファ 29/30 と衝突しない) |
| texture | 8〜12, 16〜19, 24 | **24** | 127 |
| sampler | 0,1 | **1** | 15 |

シフト値は `Tools/ShaderCompile/dxc_args_metal.txt` と
`aqEngine/Graphics/Metal/MetalCommon.h` の `SRV_INDEX_SHIFT` / `UAV_INDEX_SHIFT` の
**2 か所に書いてあるので、必ず一致させること**。

`spirv-cross --msl-decoration-binding` を付ければ SPIR-V の binding がそのまま
Metal の index になり、**実行時のリフレクションも写像テーブルも要らない**。
エンジンの `PSSetShaderResource(slot, ...)` の `slot` が、そのまま
`setFragmentTexture:atIndex:slot` になる。

> **Vulkan 側のシフトは変えないこと**。`dxc_args.txt` は Vulkan 用のまま残し、
> Metal は別のシフト一式を持つ(§9.1)。

### 5.2 頂点バッファのスロット

Metal では頂点バッファも `buffer(n)` 空間を共有する。cbuffer と衝突しないよう**上端から取る**:

| 用途 | Metal index |
|---|---|
| per-vertex ストリーム | **30** |
| per-instance ストリーム | **29** |

`MTLVertexDescriptor.layouts[30].stepFunction = PerVertex`、
`layouts[29].stepFunction = PerInstance` とする。

### 5.3 compute

グラフィックスと同じ写像でよい。CS は `b`/`t`/`s`/`u` がそのまま
`setBuffer`/`setTexture`/`setSamplerState` に落ちる。

---

## 6. メモリとアップロード

ユニファイドメモリなので **ステージングバッファを作らない**。

| 対象 | 方式 |
|---|---|
| 定数バッファ | `MTLStorageModeShared` のフレームリング。`UpdateConstantBuffer` は `memcpy` のみ。256 バイト整列 |
| 動的 VB / IB | 同じくリング(`FRAME_COUNT` 分) |
| 静的 VB / IB | `MTLStorageModeShared` に 1 回書いて以後読み取り専用 |
| テクスチャ | `MTLStorageModeShared` の `MTLTexture` へ `replaceRegion:` で直接書く |
| レンダーターゲット / 深度 | `MTLStorageModePrivate` |

BC 圧縮テクスチャも `replaceRegion:` でそのまま流せる(§0.2 で生成可を確認済み)。
`bytesPerRow` はブロック行単位になる点だけ注意。

---

## 7. レンダーターゲット / 深度 / スワップチェーン

- **スワップチェーン**: `CAMetalLayer` は P2 で既に作ってある(`PlatformMac` が
  `NativeWindowHandle` に `CAMetalLayer*` を入れる)。Metal バックエンドは
  **その同じハンドルを受け取って `device`/`pixelFormat` を設定するだけ**。
  Vulkan の `VK_EXT_metal_surface` 経路が無くなる分、単純になる。
- `contentsScale = 1` 固定の方針(`Mac移植設計.md` §8-13)は**そのまま引き継ぐ**。
- **`MetalRenderTarget`**: オフスクリーンは `MTLTexture`(`RenderTarget | ShaderRead | ShaderWrite`)。
  swapchain プロキシモードは `nextDrawable` のテクスチャを指す。
- **`MetalDepthMap`**: `Depth32Float` の `type2DArray`(4 スライス)。
  比較サンプラ(`compareFunction = LessEqual`)を内蔵する。Vulkan 版と同じ形。
- **`CopyToBackBuffer`**: `MTLBlitCommandEncoder` の `copyFromTexture:toTexture:`。
  フォーマットが違う場合はフルスクリーン描画へフォールバック。

---

## 8. リソースの同期

**エンコーダ内の依存は Metal が自動で追跡する**ので、Vulkan のような
image layout 追跡とバリア発行は要らない。明示が要るのは次だけ:

1. **compute → graphics のバッファ依存**: エンコーダが分かれていれば `MTLCommandBuffer`
   のレベルで順序が保証される。同一エンコーダ内は `memoryBarrierWithScope:`。
2. **`MTLHeap` / untracked リソース**: 本設計では使わないので該当なし。

---

## 9. シェーダのビルド(MSL)

### 9.1 ビルド時: `.fx` → `.spv` → `.metal`

`Tools/ShaderCompile/compile_msl.cmake`(新規)。`compile_spv.cmake` と同じ作りにする:

- `shader_entries.txt` を**そのまま再利用**する(59 エントリ。新規ファイルを作らない)
- `dxc` と `spirv-cross` を **configure 時に絶対パスで解決して焼き込む**
  (Xcode がターミナル環境を継承しない問題への対処。`compile_spv.cmake` と同じ理由)
- 引数は `Tools/ShaderCompile/dxc_args_metal.txt`(新規)に単一ソース化する。
  Vulkan 用 `dxc_args.txt` との差は**シフト 4 行だけ**
- 出力: `Game/Assets/Shader/msl/<stem>.<entry>.<stage>.metal`。
  **同じディレクトリに同名の `.spv` も残す**(§9.3 の頂点入力リフレクションが読む)。
  Vulkan 用の `Game/Assets/Shader/spv/` とはシフトが違う別物なので、**同じ場所へ出さないこと**。
  どちらも `.gitignore` 済み(生成物は追跡しない)

```
dxc -spirv -fspv-entrypoint-name=main -fvk-use-dx-layout \
    -fvk-b-shift 0 all -fvk-t-shift 0 all -fvk-s-shift 0 all -fvk-u-shift 16 all \
    -fvk-b-shift 0 all -fvk-t-shift 8 all -fvk-s-shift 0 all -fvk-u-shift 24 all \
    -E <entry> -T <stage>_6_0 -I <shaderDir> -Fo <out.spv> <src.fx>
spirv-cross --msl --msl-version 20000 --msl-decoration-binding --output <out.metal> <out.spv>
```

> `shader_entries.txt` は **CRLF + 行末インラインコメント**を含む。
> パースするときは `\r` を落とし、`#` 以降を捨てること(検証中に踏んだ)。

### 9.2 実行時: `newLibraryWithSource:`

`MetalShader::Load()` は `.metal` を読んで `[device newLibraryWithSource:options:error:]` →
`[library newFunctionWithName:@"main0"]`。エントリ名は spirv-cross が `main0` に固定する。

- 実測で 59 本 2 秒(§0.2)。**起動時間への影響は P6 の評価項目にする**
- コンパイルエラーは `NSError` の `localizedDescription` を `StartupLog` に出す。
  Vulkan 側が `.spv` 欠落時に生成コマンドをログに出しているのと同じ親切さを保つ
- 将来 Metal Toolchain を導入したら `.metallib` の事前ビルドへ差し替えられるよう、
  **読み込み経路は `MetalShader` の中に閉じる**

### 9.3 頂点入力レイアウト

**`VulkanShader::BuildInputLayout()` の実装をそのまま流用する**。
`spirv_reflect` で `.spv` の入力変数を列挙し、location 昇順に詰めて
per-vertex / per-instance を `I_` 接頭辞で分ける、という処理は API 非依存。

生成 MSL の `[[attribute(n)]]` が SPIR-V の location と一致することは §0.2 で確認済みなので、
`MTLVertexDescriptor.attributes[location]` にそのまま対応づけられる。

> 共通化のため、`BuildInputLayout` 相当を `Graphics/ShaderReflection.{h,cpp}`(新規・API 非依存)
> へ括り出すことを検討する。**ただし Vulkan 側の回帰リスクがあるので P6 では触らず、
> Metal 側にコピーして持つ**(§13-4)。

---

## 10. Objective-C++ の扱いとヘッダ規約

**素の C++ に縛るのは「外から include されるヘッダ」だけでよい**(P0 で確定)。
Vulkan バックエンドで `Graphics/Vulkan/` の外から include されているのは
`VulkanGraphicsDeviceImpl.h` と `VulkanImGui.h` の 2 本だけ(`Engine.cpp` /
`Core/Application.cpp` / `Rendering/ImGuiRenderCommand.cpp` から)。Metal も同じになる。

| ヘッダ | 規約 |
|---|---|
| `MetalGraphicsDeviceImpl.h` / `MetalImGui.h`(P6) | **素の C++**。ObjC 型を一切出さない。`Engine.cpp` 等が素の `.cpp` から include するため |
| それ以外の `Graphics/Metal/*.h` | **Objective-C++ 専用でよい**。`MetalCommon.h` を include する。誤って `.cpp` から引かれたら `MetalCommon.h` の `#ifndef __OBJC__` が `#error` で止める |

これにより、当初見込んでいた「不透明構造体の定型 +150 行」は**実質 1 クラス分で済む**。

- ObjC オブジェクトは公開ヘッダを持つクラスだけ不透明構造体にまとめる:

```cpp
// MetalGraphicsDeviceImpl.h
namespace aq { namespace graphics {
    struct MetalObjects;                       // 定義は .mm 側
    class MetalGraphicsDeviceImpl : public IGraphicsDeviceImpl { ...
        MetalObjects* objects_;
    };
}}
```

- `aq.h` は ObjC++ TU で DirectXTex を include しない(`Mac移植設計.md` §6)。
  **`Graphics/Metal/*.mm` から画像デコードに触らないこと**。
  テクスチャ生成に渡ってくるのは既にデコード済みの `ImageData`(API 非依存)なので問題ない。
- **`Mac移植設計.md` §10 のチェックポイントを改訂する**:
  「`.mm` は `Platform/Mac/`・`HID/Mac/`・`Sound/CoreAudio/` に閉じる」に
  **`Graphics/Metal/` を加える**。

---

## 11. バックエンド選択とビルド配線

| 箇所 | 変更 |
|---|---|
| `aqEngine/aq.h` | `ENGINE_GRAPHICS_METAL` を検証ロジックへ追加(定義は 1 つだけ) |
| `aqEngine/Engine.cpp` | ヘッダ include と `GraphicsDevice::Create<MetalGraphicsDeviceImpl>()` の分岐 |
| `aqEngine/Core/Application.cpp` | ImGui の Init / Shutdown / NewFrame / Render の分岐に Metal を追加 |
| `aqEngine/Rendering/ImGuiRenderCommand.cpp` | 同上 |
| `aqEngine/Graphics/RenderContext.cpp` | D3D11 ガードのみ。**変更不要**の見込み(要確認) |
| `DirectX/CMakeLists.txt` | `AQ_GRAPHICS_API` の候補に `Metal` を追加。**Metal は APPLE 限定**で、非 Apple なら FATAL_ERROR。Vulkan 用の `VULKAN_SDK` 要求は Metal でも必要(dxc / spirv-cross を使うため) |
| `DirectX/aqEngine/CMakeLists.txt` | `Graphics/Metal/` を非 Apple で除外。`-framework Metal -framework QuartzCore` は P2 で既にリンク済み |
| `DirectX/CMakePresets.json` | `macos-ninja-metal` / `macos-xcode-metal` を追加(既存の Vulkan プリセットは**残す**) |
| `Game/GraphicsApi.props` | **変更不要**(Windows の MSBuild は Metal を選べない) |

**Vulkan 構成は残す**。Metal が動くまでの比較対象として、また `Mac移植設計.md` の
到達点を壊さないために、`macos-ninja`(Vulkan)を既定のまま維持する。

---

## 12. フェーズ計画

| | 内容 | 評価 |
|---|---|---|
| **P0** | 足場。`ENGINE_GRAPHICS_METAL` の配線、CMake プリセット、空の `MetalGraphicsDeviceImpl`/`MetalRenderContextImpl`(全メソッド no-op)でビルドが通り、**黒画面で起動して終了コード 0** | ビルドが通る / 起動して閉じられる / Vulkan 構成が壊れていない |
| **P0.5** | `compile_msl.cmake` と `MetalShader`。59 本の `.metal` がビルド時に生成され、起動時に**全部コンパイルできる**(描画はまだしない) | 59/59 生成・59/59 コンパイル成功 / 起動時間の増分を実測して記録 |
| **P1** | `CAMetalLayer` から drawable を取り、`MTLRenderPassDescriptor` でクリアして Present。**単色の画面が出る** | 指定色で塗られる / validation(Metal API Validation)エラー 0 |
| **P2** | 描画パス一式。`MetalPipelineCache` / `MetalDepthStencilCache` / `MTLVertexDescriptor` / バッファ・テクスチャ・サンプラの束ね / 定数バッファのスライス化。**タイトル画面が出る** | タイトルが Vulkan 構成と同じ見た目 / BC 圧縮テクスチャが正しく出る |
| **P3** | シャドウ。depth-only パス(color 0 本 + `MetalDepthMap` のスライス)と深度マップの初期クリア | 手前が暗くならない / 影が Vulkan と同じ位置に出る |
| **P4** | compute を有効化(`SetComputeSupported(true)`)。メイン RT の HDR 化とポストプロセス一式(トーンマップ・Bloom・Hi-Z・モーションブラー) | ステージが Vulkan 構成と同じ見た目・同じ明るさ |
| **P5** | GPU 駆動のクラスタカリングと間接描画(`DrawIndexedIndirect`) | カリングが効く / 描画結果が変わらない |
| **P6** | `MetalImGui` とデバッグ UI。詰め(PSO 事前生成・起動時間・フレーム時間の比較) | ImGui が出て操作できる / 通しプレイ / 終了コード 0 |

各フェーズの完了条件に共通で **「Windows(D3D11/D3D12)と Mac の Vulkan 構成が壊れていない」** と
**「Metal API Validation エラー 0(`METAL_DEVICE_WRAPPER_TYPE=1` を付けて実行)」** を含める。

> **P2〜P4 の範囲を P1 完了時に見直した**(番号は組み替えていない)。当初は
> 「P2 = 三角形 1 枚 → P3 = テクスチャ → P4 = ステージ一式」だったが、
> **テクスチャとサンプラの生成は P0 で既に済んでいる**(`MetalResources`)ため、
> 束ねるだけなら P2 に自然に入る。逆にテクスチャを束ねずに UI を描くと
> nil テクスチャで Validation エラーになり、フェーズの完了条件を満たせない。
> そこで P2 を「描画パス一式 → タイトル画面が出る」、P3 を「深度と 3D」、
> P4 を「GBuffer と影」に切り直した。
>
> **番号を振り直さないのは意図的**。`Mac移植設計.md` でフェーズ番号を途中で
> 組み替えた結果、コミットログから順序が読めなくなった反省による。
>
> **P3〜P5 も P2 完了時にもう一度切り直した**(同じく番号は据え置き)。
> P2 の描画パスが**当初 P3/P4 に割り振っていた MRT・ディファードライティング・
> インスタンス描画までそのまま動いた**ため。実際に P2 完了時点でステージが
> 走行可能な状態で描けている。残っていたのは (a) シャドウ、(b) ポストプロセス、
> (c) GPU 駆動カリングの 3 つだったので、それぞれ P3 / P4 / P5 に割り当て直した。

### P0 の結果(2026-09-11)

- [x] **ビルドが通る**。`macos-ninja-metal` Debug。`Graphics/Metal/` の 16 ファイル(3,022 行)は**警告 0**
- [x] **起動して閉じられる**。黒いウィンドウが出て、閉じると**終了コード 0**。エラー/アサート無し
- [x] **Vulkan 構成が壊れていない**。`macos-ninja` Debug は 0 エラーでビルドでき、
      タイトル画面・ImGui・60fps まで従来どおり。Vulkan validation エラー 0 / 終了コード 0
- [ ] Windows(D3D11/D3D12/UWP)の回帰確認 — **この Mac では不可。次に Windows を触るときの宿題**

実装した範囲と、意図して先送りしたもの:

| | P0 で入れた | 先送り |
|---|---|---|
| デバイス | `MTLDevice` / `MTLCommandQueue` / `CAMetalLayer` の設定 | — |
| フレーム | `WaitIdle()`(最後にコミットしたコマンドバッファを待つ) | drawable 取得・Present(P1) |
| RT / 深度 | `MetalRenderTarget`(オフスクリーン)/ `MetalDepthMap`(D32F × 4 スライス) | swapchain プロキシの実配線(P1) |
| リソース | VB / IB / CB / テクスチャ(**BC 対応**)/ サンプラを実際に確保 | — |
| コンテキスト | 純粋仮想 37 本を override。保留ステートと `UpdateConstantBuffer` のみ中身あり | 描画(P2 以降) |
| シェーダ | スタブ(`Load` はパスを憶えて true を返すだけ) | `.metal` 生成と `newLibraryWithSource:`(P0.5) |
| ImGui | **Metal では ImGui コンテキストを作らない**(`Application.cpp` に分岐) | `MetalImGui`(P6) |

ヘッダ規約は P0 で確定した(§10)。**素の C++ に縛るのは `MetalGraphicsDeviceImpl.h` だけ**で済み、
当初見込んだ pimpl の定型は 1 クラス分に収まった。

### P6 の結果(2026-09-11)

- [x] **ImGui が出て操作できる**。メニューバー・Scene Hierarchy・Inspector・
      Rendering パネル(タブ / スライダー / 折りたたみ)すべて動作
- [x] **FPS オーバーレイが `Metal 60.0 FPS` と出る**(下記の副次修正)
- [x] **通しプレイ**。F1 でデバッグ UI をトグルし、ステージへ入って 60fps で走行
- [x] **Metal API Validation エラー 0 / 終了コード 0 / Metal 由来の警告 0**
- [x] **Vulkan 構成に回帰なし**。0 エラーでビルドし、オーバーレイは `Vulkan 60.0 FPS`、
      VK エラー 0 / 終了コード 0
- [ ] Windows の回帰確認 — **この Mac では不可**

`VulkanImGui`(249 行)と同じ構造の `MetalImGui`(364 行)。`imgui_impl_metal` は使わず、
クラシック API(`GetTexDataAsRGBA32`)で自前描画する。`ImGuiVK.fx` をそのまま使うので
**`.fx` も `shader_entries.txt` も増えていない**。

**`ImGuiVK.fx` だけは Y 反転が要る**(§2.4 の例外): §2.4 は「Metal の NDC は D3D と同じなので
Y-flip は要らない」としているが、これは**エンジンの通常シェーダ**の話。`ImGuiVK.fx` は
**唯一 Vulkan NDC 前提で書かれた `.fx`** で(コメントにも「imgui 座標 → Vulkan NDC へ直接写像」とある)、
同じ定数を渡すと**UI が上下逆さま**になる。`.fx` は無改変のまま、投影定数の Y 成分の符号と
平行移動だけで吸収した。

その他の判断:

- **PSO は `Init()` ではなく初回 `Render()` で遅延生成**する。
  `colorAttachments[0].pixelFormat` に drawable のフォーマットが要るため
  (`EnsureFullscreenBlitPipeline` と同じ手)。
- **頂点記述子は `MetalShader::GetVertexDescriptor()` を使わず自前で組む**。
  `ImDrawVert` の `col` はバッファ上 RGBA8 unorm だが、シェーダ宣言は `float4` なので
  `.spv` リフレクションでは float4 になってしまう。`VulkanImGui` が
  `VK_FORMAT_R8G8B8A8_UNORM` を明示しているのと同じ理由。
- **`CopyToBackBuffer` の本体を関数へ括り出した**。元の実装は blit 成功時と
  フォールバック失敗時に `return` で抜けており、**そのままだと ImGui が描かれないフレームが出る**。

**副次修正**: FPS オーバーレイのバックエンド名が `D3D11` / `D3D12` しか分岐を持たず、
**Vulkan も Metal も `?` と表示されていた**(P4b の評価時に見つけて保留していたもの)。
`Vulkan` / `Metal` を追加した。**Windows の Vulkan 構成にも効く。**

### P5 の結果(2026-09-11)

- [x] **GPU 駆動クラスタカリングが動く**。`ClusterCullReset` / `ClusterCull` が実際に Dispatch され、
      スレッドグループサイズも `.spv` から **1x1x1 / 64x1x1** と正しく読めている
- [x] **描画結果が変わらない**。CPU カリング経路との地面の画素比較は **平均差 1.11/255**
      (差が 24 を超えたのは 1.72% で、草とキャラの位置が別フレームである分)
- [x] **Metal API Validation エラー 0 / 終了コード 0 / Metal 由来の警告 0** / Vulkan 構成 0 エラー
- [ ] Windows の回帰確認 — **この Mac では不可**

**§13-3 の懸念(間接引数のレイアウト)は変換不要で解決した。** `ClusterCullReset.fx` が書く
レイアウトと Metal の `MTLDrawIndexedPrimitivesIndirectArguments` が**完全に同一**:

| offset | `ClusterCullReset.fx` | Metal |
|---|---|---|
| 0 | IndexCountPerInstance (uint) | `indexCount` (uint32) |
| 4 | InstanceCount (uint) | `instanceCount` (uint32) |
| 8 | StartIndexLocation (uint) | `indexStart` (uint32) |
| 12 | BaseVertexLocation (int) | `baseVertex` (**int32**) |
| 16 | StartInstanceLocation (uint) | `baseInstance` (uint32) |

`VkDrawIndexedIndirectCommand` も同じ並びなので、**3 API で同じバッファをそのまま使える**。
`indexStart` はバイトではなく**インデックス単位**で、`indexBufferOffset` とは別に加算される。

実装は 2 メソッドだけ:

- `IASetIndexBufferGpu` … compact 済み IB を保留する。`ClusterCull.fx` が
  `RWByteAddressBuffer` へ R32_UINT で書くのでインデックス型は常に 32bit。
  **CPU 側 IB とは排他**にして、どちらか一方だけが非 nullptr になるようにした。
- `DrawIndexedIndirect` … `drawIndexedPrimitives:...indirectBuffer:indirectBufferOffset:`。

**検証には一時的な変更が 2 つ必要だった**(確認後に復元済み。差分が空であることを確認):

`g_clusterCullEnabled` の既定は **false**(ImGui のデバッグ UI から切り替える設計)で、
さらに `g_clusterCullMinClusters` が **256**。AquaDash のメッシュはこの閾値に届かないため、
**既定設定ではこの経路に入らない**。両方を一時的に緩めて実際に Dispatch させ、
描画が変わらないことを確認した。
**Metal に ImGui が入る P6 以降は、デバッグ UI から切り替えて確認できる。**

### P4 の結果(2026-09-11)

- [x] **ステージが Vulkan 構成と同じ見た目・同じ明るさになった**。
      地面の画素比較は P3 の 36.3/255 から **1.8/255** へ。サンプル点は**完全一致**:

      | 位置 | Vulkan | Metal |
      |---|---|---|
      | 空 | (205,205,205) | **(205,205,205)** |
      | 道路 | (110,119,136) | **(110,119,136)** |
      | 草 | (57,83,32) | **(57,83,32)** |

      残る 1.8 は草とキャラクターの位置が別フレームである分。
- [x] **Metal API Validation エラー 0 / 終了コード 0 / Metal 由来の警告 0** / Vulkan 構成 0 エラー
- [x] compute の `threadsPerThreadgroup` は**全 7 本が `.spv` から実測**で解決
      (暫定値フォールバックの発火 0 件)
- [x] UAV 未バインドで捨てた Dispatch 0 件
- [ ] Windows の回帰確認 — **この Mac では不可**

**`CopyToBackBuffer` が P4 の核心だった。** 実装前に実機で 2 つ確かめてある:

- compute を有効にすると画面へ出るのは**トーンマップ最終 RT(`R8G8B8A8_Unorm`)**、
  drawable は `BGRA8Unorm`。
- Metal の `copyFromTexture:` は **RGBA8 → BGRA8 を「通してしまう」が生バイトコピー**で、
  **R と B が入れ替わる**(4x4 の赤テクスチャで実測。赤が青になる)。
  `RGBA16Float → BGRA8` のほうは Validation がアサートで止める。
  **前者は黙って壊れる種類の罠。**

D3D12 はスワップチェーンが `R8G8B8A8_UNORM` でトーンマップ RT と一致するため
`CopyResource` で済んでいる。Metal は `CAMetalLayer` の都合で同じ手が使えないので、
**フォーマットか寸法が一致しないときはフルスクリーン描画で変換する**ことにした。
compute 有効時は不一致が常態なので、**実質いつも描画経路**になる(§7 の記述は主従が逆だった)。

変換用のシェーダは **`.mm` に MSL を文字列で埋め込み、`newLibraryWithSource:` で
起動時に 1 度だけコンパイル**する。`.fx` を増やさずに済み、`shader_entries.txt`
(Vulkan と共用)に載せて他構成のビルドへ波及させることもない。
Y 反転は頂点側の UV 生成 1 か所(`v = (1 - y) * 0.5`)で吸収した。

その他:

- **メイン RT を `R16G16B16A16_Float` + 深度付き**へ(Vulkan / D3D12 と同構成)。§13-9 解消。
- **`SetComputeSupported(true)`**。P1 で入れた回避を撤去した。
- **`CreateStructuredBuffer` / `CreateRawBuffer` を実装**した。Metal では単なる `MTLBuffer`。
  `newBufferWithBytes:` ではなく確保 + ゼロ埋め + `memcpy` にしてある。`CreateRawBuffer` は
  D3D12 版と同様 16 バイト境界へ切り上げるので、`newBufferWithBytes:` だと
  **`initData` の終端より先を読む**ため。
- **UAV / SRV に共通基底**(`MetalUAVBase` / `MetalSRVBase`)を入れ、Metal の全ビュー実装を
  その派生に揃えた。バッファ SRV(StructuredBuffer / ByteAddressBuffer)は
  テクスチャではなく `buffer(8+n)` へ落ちるため、`GetNativeHandle()` だけでは足りない。
- **`UavBarrier` は no-op**。`memoryBarrierWithScope:` は `MTLDispatchTypeConcurrent` 専用で、
  serial のまま呼ぶと Validation がエラーにする。compute エンコーダは serial で開いており、
  同一エンコーダ内の Dispatch は順に実行される。エンコーダを跨ぐ依存は
  `MTLCommandBuffer` が順序を保証する。**つまり両方とも no-op で正しい**
  (concurrent へ変えるなら同時にバリアを入れること)。
- **`FlushPendingClears()` の呼び出し元が 4 か所目(`Dispatch` の先頭)**に増えた。
  予約クリアを持ち越したまま compute が RT へ書くと、後続の描画が開くパスの
  `loadAction = Clear` で**compute の結果ごと消える**。

### P3 の結果(2026-09-11)

- [x] **手前が暗くならない**。キャラクターも草も Vulkan と同じ明るさ関係になった
- [x] **Metal API Validation エラー 0 / 終了コード 0 / Metal 由来の警告 0**
- [x] Vulkan 構成 0 エラー
- [ ] Windows の回帰確認 — **この Mac では不可**

**P2 の時点で、当初 P3/P4 に割り振っていた MRT・ディファードライティング・
インスタンス描画・深度までそのまま動いていた**(ステージが走行可能な状態で描けていた)。
残っていた「手前が真っ暗」の原因は 1 点で、**`MetalDepthMap` の初期クリアが無く、
未初期化の深度マップが「全面が影」と判定されていた**こと。
Vulkan 版は生成時に `vkCmdClearDepthStencilImage` で 1.0(遠 = 影なし)にしている。

実装したもの:

- **深度マップの初期クリア**を `Create()` の末尾で行う。Metal はクリアにコマンドバッファが
  要るので、キューから 1 本取り、スライスごとに「color 0 本 + depthAttachment
  (`loadAction = Clear` / `clearDepth = 1.0`)」の空パスを開いて即閉じ、
  `commit` + `waitUntilCompleted`(起動時 1 回のみ)。
- **depth-only パス**。`OMSetDepthOnlyTargetSlice` で color 0 本 + `depthOnlySlice_` を記録し、
  `BuildRenderPassDescriptor()` が `depthAttachment.slice` に差す。
  `nil` を返すのは **`depthOnlyMap_` も `colorRTCount_` も無い場合だけ**にした。
- **深度ステートの誤爆を 2 段階で防いだ**。P2 の「深度アタッチメントが無ければ `Disabled` 強制」に
  `depthOnly` を OR で足したうえで、**depth-only では `pending_.depth` ではなく
  `DepthMode::ReadWrite` を強制**する。シャドウパスの直前に UI が `Disabled` を残していることがあり、
  そのまま使うと深度が 1 つも書かれず「全面が影」へ逆戻りする。
- **ビューポートのクランプ元**を depth-only では深度マップ解像度にした
  (カラー RT が無いので、従来の `colorRTs_[0]` 基準だと 0 になりビューポートが潰れる)。

**残差の測定**: 地面の画素比較は P3 前 46.2/255 → P3 後 36.3/255。
残った差は**トーンマップ未適用によるもの**と特定済み(下表)。P4 で解消する。

| 位置 | Vulkan | Metal | 比 |
|---|---|---|---|
| 空 | 205 | **255** | 1.24(素のクリア色がそのまま) |
| 道路 | (110,119,136) | (75,83,101) | 0.68 / 0.70 / 0.74 |
| 草 | (57,83,32) | (40,55,25) | 0.70 / 0.66 / 0.78 |

**照明面が一律 0.7 倍で暗く、空だけ 255 に張り付く**のは、中間調を持ち上げて
ハイライトを圧縮するトーンマップが走っていないときの典型。**構造的な破綻ではない。**

### P2 の結果(2026-09-11)

- [x] **タイトル画面が Vulkan 構成と同じ見た目で出る**。背景・雲・山・水面・タイトル文字・
      ステージサムネイル・「STAGE 01 GREEN COAST」・「PRESS SPACE」すべて描画される
      - 同じウィンドウ領域で撮った Metal / Vulkan のスクリーンショットを画素比較したところ
        **平均差 2.05/255**。差が 24 を超えた画素は 2.41% で、**動く輪郭(雲・水面)と
        文字のアンチエイリアスに集中**している(タイトルはアニメーションするので
        2 枚は別フレーム。完全一致にはならない)
- [x] **BC 圧縮テクスチャが正しく出る**(ステージサムネイルと背景が正常)
- [x] **Metal API Validation エラー 0**
- [x] **終了コード 0** / Metal 構成の警告 0 / Vulkan 構成 0 エラー
- [x] フォールバック白テクスチャの使用ログ 0、定数バッファのリング拡張 0、PSO 生成失敗 0
- [ ] Windows の回帰確認 — **この Mac では不可**

P0 の申し送り 2 件(§13-7)を解消した:

- **定数バッファをスライス化**した。Update ごとに次のスライスへ bump 確保し、
  `GetCurrentOffset()` がそのオフセットを返す。**リングが枯渇したらまず 2 倍に伸ばし
  (旧内容を memcpy して引き継ぐ)、1 CB あたり 8MB の上限に達したらログを出して
  最終スライスを使い回す**。Vulkan 版は静かに最終スライスへクランプするだけだが、
  Metal 側は原因がログから追えるようにした(意図的な差分)。
  **先頭へ巻き戻すことはしない**(前フレームのデータを壊すため)。
- **リング位置をデバイスのフレーム番号に同期**した(`GetFrameIndex()`)。自前カウンタは撤去。
  あわせて**フレーム未開始の間はカーソルを 0 に戻す**ようにした。`Present()` は drawable が
  取れなかったフレームで番号を進めないため、これが無いとウィンドウが隠れている間に
  Update だけが積み上がってリングが無駄に上限まで伸びる。

設計に無かった判断:

- **深度アタッチメントが無いパスでは `DepthMode::Disabled` を強制**する。PSO の
  `depthAttachmentPixelFormat` が Invalid なのに depth write が有効だと Metal に弾かれる。
  Vulkan は PSO 生成側で無効化しているが、**Metal は PSO が深度に関与しない**ので
  コンテキスト側の責任になる。
- **ビューポート / シザーを RT の実サイズへクランプ**する(はみ出すと Metal が弾く)。
- **未バインドのテクスチャ/サンプラ用に 1x1 白テクスチャと既定サンプラを束ねる**。
  Validation は「シェーダが宣言している引数が nil」を弾くため。タイトル画面では
  実際には 1 度も使われなかった(ログ 0 件)。
- **PSO キーのハッシュは構造体丸ごとではなくメンバ単位**。`const void*` と `uint8_t` が
  混在してパディングが入るので、Vulkan 版のようなバイト列ハッシュだと
  「等しいのにハッシュが違う」キーが生まれる。
- **PSO 生成失敗も `nullptr` としてキャッシュに記録**する(毎 Draw で作り直してログが溢れるのを防ぐ)。

### P1 の結果(2026-09-11)

- [x] **指定色で塗られる**。`Application.cpp` のクリア色 `{1,1,1,1}` で白くなる。
      一時的に `{0.15, 0.45, 0.85}` へ変えると**そのとおり青くなる**ことを確認した
      (白が「偶然の初期値」でないことの確認。確認後に元へ戻してある)
- [x] **Metal API Validation エラー 0**。`METAL_DEVICE_WRAPPER_TYPE=1` で
      `Metal API Validation Enabled` が出た状態での実行
- [x] **終了コード 0**
- [x] Metal 構成の警告 0(既存の `OceanDebugPanel.h` 1 件のみ)/ Vulkan 構成 0 エラー
- [ ] Windows の回帰確認 — **この Mac では不可**

実装した範囲: `dispatch_semaphore_t` による frames-in-flight、`nextDrawable`(**nil を返しうるので
セマフォを戻してそのフレームを捨てる**)、クリアの予約と flush、`MTLBlitCommandEncoder` による
`CopyToBackBuffer`、`presentDrawable` + `addCompletedHandler` でのセマフォ signal。
コミットした `MTLCommandBuffer` は `WaitIdle()` 用に必ず記録している。

**`SetComputeSupported(false)` を Metal の `Initialize()` で呼んでいる**(P5 まで)。
これが無いと P1 の到達目標に届かない: `Renderer::GetDisplayRTHandle()` は compute 対応時に
displayRT を**ポストプロセスチェーンの最終 RT**(Tonemap の compute 出力)にするため、
`CopyToBackBuffer` が「まだ誰も書いていない RT」を画面に出してしまう。
false にすると Renderer はシーン RT を直接表示する経路に落ちる。
D3D11 も同じ場所で FL10 判定として呼んでおり作法は一致している。
**P5 で compute を入れたら true に戻し、同時に §13-9 の HDR 化を行うこと。**

### P0.5 の結果(2026-09-11)

- [x] **59/59 生成**。`aqCompileMsl` が `.fx` → dxc → `.spv` → spirv-cross → `.metal` を通す
- [x] **59/59 コンパイル成功**。起動時に実際に読まれるのは 23 本(遅延ロード)で **0 失敗**。
      残り 36 本もオフラインで `newLibraryWithSource:` に通して確認済み
- [x] **起動時間の増分を実測**。23 本で **812ms**(1 本 ≒ 35ms)。詳細と対処案は §13-2
- [x] Vulkan 構成が壊れていない(0 エラーでビルド)
- [ ] Windows の回帰確認 — **この Mac では不可**

**当初の binding 設計が誤っていたことがここで判明した**(§5.1 / §13-1)。
「Metal は名前空間が別だから全部 0 始まりでよい」は、中間生成物の SPIR-V が
Vulkan の統一名前空間であることを見落としていた。シフトを `b:0 / t:8 / s:0 / u:24` に
配り直して解決した。**設計を実装前に実機で検証していても、1 段階の中間表現を
挟む経路はこういう見落としが残る**という例。

新規: `Tools/ShaderCompile/compile_msl.cmake`(402 行)/ `dxc_args_metal.txt`。
`MetalShader` は 59 → 387 行。`shader_entries.txt` は Vulkan と**同じものを再利用**している。

---

## 13. オープン課題

1. ~~**`ClusterCull.main.cs` が spirv-cross で壊れる**~~ → **解決(P0.5)**。
   シェーダ固有の問題ではなく、**binding シフトの設計ミス**だった。`b` と `t` を
   両方 0 始まりにしたため、`cbuffer ... : register(b0)` と
   `StructuredBuffer ... : register(t0)` が SPIR-V の同じ binding に落ち、
   spirv-cross がバッファを別名化して不正なキャストを吐いていた。
   シフトを `b:0 / t:8 / s:0 / u:24` に配り直して **59/59 がコンパイルできるようになった**(§5.1)。
   同じ形をしたシェーダは他にもあり得たので、1 本の特例対処にしなくて正解だった。
2. **起動時のシェーダコンパイルが起動時間の大半を占める**(P0.5 で実測):
   タイトル画面までに読まれるのは 59 本中 **23 本**(残りは遅延ロード)で、
   その 23 本のコンパイルに **812ms**(1 本あたり約 35ms)。
   このときの起動全体が約 1,100ms なので、**7 割以上がシェーダのコンパイル**。
   50ms を超えたものが 11 本あり、重いのはポストプロセスの compute
   (`DualBlurUpAccum` 85ms / `PBRLighting` 78ms / `MotionBlur` 67ms / `HiZReconstruct` 67ms)。
   対処は (a) `MTLBinaryArchive` でコンパイル結果をディスクにキャッシュ、
   (b) Metal Toolchain を導入して `.metallib` を事前ビルド、(c) 並列コンパイル。
   **P6 の「詰め」で決める**。
3. ~~**`DrawIndexedIndirect` の引数レイアウト**~~ → **解決(P5)**。D3D12 / Vulkan / Metal で
   **完全に同一**だったため変換は不要だった(§12 の P5 の結果に対応表)。以下は当初の懸念:
   D3D の `D3D12_DRAW_INDEXED_ARGUMENTS`
   (IndexCountPerInstance / InstanceCount / StartIndexLocation / BaseVertexLocation / StartInstanceLocation)と
   Metal の `MTLDrawIndexedPrimitivesIndirectArguments` はフィールドの並びが同じだが、
   **`baseVertex` の符号と `indexStart` の単位**を実機で確認すること。
   引数バッファは compute が書くので、**シェーダ側の書き込み順も合わせる必要がある**。
4. **`BuildInputLayout` の重複**: §9.3 のとおり P6 では Metal 側へコピーする。
   Vulkan と Metal の両方が動いたあとで `Graphics/ShaderReflection` へ括り出すか決める。
5. **`Mac移植設計.md` §8-17(頂点オフセットのずれ)が Metal にも波及する**:
   DXC が未使用の頂点入力を削るため `Model.fx` / `SimpleBox.fx` の TEXCOORD0 オフセットが
   ずれる問題は、SPIR-V 由来のリフレクションを使う以上 Metal でも**同じように出る**。
   Vulkan 側とまとめて直すこと。
6. **`PixelFormat::D24_Unorm_S8_Uint` の扱い**: Apple Silicon 非対応(§0.2)。
   `Depth32Float` へ黙って読み替えるか、`Unknown` を返して呼び出し側に気づかせるかを決める。
   現状エンジンがこの値を実際に要求しているかの確認から。
7. **P0 で入れた「後で直す」実装 2 つ**(P2 で必ず解消する):
   - **`MetalConstantBuffer` が単一スライス**。Vulkan 版は Update ごとに別スライスへ bump 確保して
     frames-in-flight のリングにしている(オブジェクトごとの world 行列が同一フレームで
     何度も Update されるため)。**このまま描画を入れると全オブジェクトが最後の行列で描かれる**。
   - **動的 VB/IB のリング位置がデバイスのフレーム番号と同期していない**。P0 では自前カウンタを
     `% FRAME_COUNT` で回しているだけなので、1 フレームに 2 回 Update されるとずれる。
     Vulkan 版と同じくデバイスのフレーム番号駆動へ差し替える。
8. **`SamplerDesc::mipLODBias` は Metal で落ちる**: `MTLSamplerDescriptor` に LOD バイアスが無い。
   現状 0 以外を使っている箇所が無いので実害は出ていないが、使い始めたら破綻する。
9. **メイン RT のフォーマットが Vulkan と違う**(P0 で判明): P0 では
   `SWAPCHAIN_PIXEL_FORMAT`(BGRA8Unorm)で作ったが、**Vulkan 版のメイン RT は HDR の
   `R16G16B16A16_SFLOAT`** で、最後に `CopyToBackBuffer` で LDR へ落としている。
   トーンマップと Bloom が絡むので、**P3〜P4 で Vulkan と同じ見た目を出す段階で HDR へ変える**こと。
   変え忘れると「白飛びしない代わりに暗部が潰れる」形でズレる。
10. **P2 から P3 への申し送り 3 件**:
   - **深度のみパス(シャドウ)が通らない**。`FlushGraphicsState()` は `ps == nullptr` /
     `colorRTCount_ == 0` で描画を捨て、`BuildRenderPassDescriptor()` も color 0 本で nil を返す。
     **P4 で「color 0 本 + depthAttachment」の経路を両方に入れること**
     (PSO キャッシュ側は `psFunction = nullptr` / `colorCount = 0` を既に許容している)。
   - **インスタンス属性を持つ VS を非インスタンス描画すると Validation エラーになりうる**。
     `MTLVertexDescriptor` に `layouts[29]` が残っているのに buffer 29 を束ねないため。
     P2 の範囲(UI)には該当シェーダが無い。**P3 でインスタンス描画を入れるときに、
     「slot1 未バインドなら記述子から 29 を落とす」か「プレースホルダを束ねる」かを決めること**。
   - **定数バッファにはフォールバックが無い**。シェーダが宣言している `b#` が未バインドだと
     Validation エラーになる。エンジンが必ずバインドする前提。出たら「256 バイトのゼロ CB」で塞ぐ。
11. **Vulkan の `FormatByteSize` に潜在バグがある**(P2 の頂点レイアウト移植中に発見。
   **Metal 固有ではなく Vulkan/Windows にも効く**): `VulkanShader.cpp:136-146` の
   `FormatByteSize()` は **32bit 系以外を 0 で返す**。呼び出し側(同 397-399 行)は
   その値を offset に足しているだけなので、**16bit 系の頂点入力を使うと offset が進まず、
   以降の属性がすべて同じ位置を指す**。しかも**何も言わずに壊れる**。
   現行 59 本のシェーダに 16bit 入力が無いので顕在化していないだけで、
   パック済み UV(`R16G16_SFLOAT`)などを入れた瞬間に踏む。
   Metal 側(`MetalShader::BuildVertexDescriptor`)は 16bit まで拡張し、
   **未知フォーマットはログを出して属性をスキップする**ようにした。
   **Vulkan 側も同じ直し方をすること**(別途)。
12. ~~**Metal API Validation の有効化方法**~~ → **解決**。実行時に
   **`METAL_DEVICE_WRAPPER_TYPE=1`** を付けると、起動直後に
   `Metal API Validation Enabled` が出て有効になる(実機で確認)。
   Xcode はスキームの Diagnostics から GUI で切り替えられる。
   手順は `Tools/SetupCMake/README.md` §5.2.0 に記載した。
   **既定では無効**なので、付け忘れると「エラーが出ていない」のか
   「検証していない」のか区別がつかない。各フェーズの評価では必ず付けて起動すること。

---

## 14. 影響範囲まとめ

**新規(`aqEngine/Graphics/Metal/`)** — 見込み約 3,200 行 / 18 ファイル

`MetalCommon.h` / `MetalGraphicsDeviceImpl.{h,mm}` / `MetalRenderContextImpl.{h,mm}` /
`MetalPipelineCache.{h,mm}` / `MetalDepthStencilCache.{h,mm}` / `MetalShader.{h,mm}` /
`MetalBuffers.{h,mm}` / `MetalResources.{h,mm}` / `MetalRenderTarget.{h,mm}` /
`MetalDepthMap.{h,mm}` / `MetalImGui.{h,mm}`

**新規(ビルド)**: `Tools/ShaderCompile/compile_msl.cmake` / `Tools/ShaderCompile/dxc_args_metal.txt`

**変更**: `aq.h` / `Engine.cpp` / `Core/Application.cpp` / `Rendering/ImGuiRenderCommand.cpp` /
`CMakeLists.txt`(ルート・aqEngine) / `CMakePresets.json` / `設計書/Mac移植設計.md`(§7 と §10)

**変更不要**: `GraphicsDevice.{h,cpp}` / `GraphicsTypes.h` / 全 `I*.h` / `RenderContext.cpp` /
`.fx` 全 40 本 / `shader_entries.txt` / `Game/` 配下すべて / `.vcxproj` 一式

---

## 15. チェックポイント

- [ ] 抽象IF(`IGraphicsDeviceImpl` / `IRenderContextImpl` / `I*.h`)への**追加が 0 本**
- [ ] `.fx` が無改変で、`shader_entries.txt` も再利用されている
- [ ] `id<MTL*>` / `CAMetalLayer` / `NS*` が `Graphics/Metal/` の**外に現れない**
- [ ] **外から include されるヘッダ**(`MetalGraphicsDeviceImpl.h` / `MetalImGui.h`)が素の C++ で、
      素の `.cpp` から include してもコンパイルできる(§10)
- [ ] `.mm` が `Platform/Mac/`・`HID/Mac/`・`Sound/CoreAudio/`・`Graphics/Metal/`・
      `ExtAudioFileDecoder.mm`・`MacMain.mm` に閉じている
- [ ] Vulkan 構成(`macos-ninja`)と Windows(D3D11/D3D12)が壊れていない
- [ ] Metal API Validation エラー 0 / 終了コード 0
- [ ] 見た目が Windows・Mac Vulkan と一致する
- [ ] 設計と実装の食い違いを本書へ反映し、`対象コミット` を更新した
