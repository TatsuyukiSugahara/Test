# Metal バックエンド設計

> 対象コミット: 21cb46d / 最終更新: 2026-09-11

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
| **P2** | `MetalPipelineCache` + `MetalBuffers` + `MTLVertexDescriptor`。**三角形 1 枚**、続いて定数バッファ付きの箱 | 三角形が出る / 行列変換が Windows と一致 |
| **P3** | `MetalResources`(テクスチャ・サンプラ)と `MetalDepthStencilCache`。**テクスチャ付きモデルが深度付きで出る** | BC 圧縮テクスチャが正しく出る / 深度が効く |
| **P4** | `MetalRenderTarget` / `MetalDepthMap` / エンコーダ寿命管理の本実装。**ステージが一式描ける**(GBuffer・ライティング・影・地形・インスタンス描画) | タイトル → ステージが Vulkan 構成と同じ見た目 |
| **P5** | compute(クラスタカリング・Bloom・Hi-Z・モーションブラー)。間接描画 | 路面/草/コインが出る / ポストプロセスが効く |
| **P6** | `MetalImGui` とデバッグ UI。詰め(PSO 事前生成・起動時間・フレーム時間の比較) | ImGui が出て操作できる / 通しプレイ / 終了コード 0 |

各フェーズの完了条件に共通で **「Windows(D3D11/D3D12)と Mac の Vulkan 構成が壊れていない」** を含める。

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
3. **`DrawIndexedIndirect` の引数レイアウト**: D3D の `D3D12_DRAW_INDEXED_ARGUMENTS`
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
10. **Metal API Validation の有効化方法**: Xcode スキームなら GUI で設定できるが、
   Ninja ビルドの実行では `METAL_DEVICE_WRAPPER_TYPE=1` 等の環境変数が要る。
   P1 までに確定し、`Tools/SetupCMake/README.md` へ書く。

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
