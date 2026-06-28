# Vulkan バックエンド 設計ドキュメント

対象: `aqEngine/Graphics/Vulkan/`
前提: 既存の抽象レイヤ（Bridge パターン）を変更せず、Vulkan を `IGraphicsDeviceImpl` / `IRenderContextImpl` の実装として追加する。
本書は **設計のみ**。実装コードは含まない。
姉妹文書: [D3D12Backend設計.md](../D3D12/D3D12Backend設計.md)（同一構造の先行実装。Vulkan は本書の多くを D3D12 実装から平行移植する）。

---

## 0. 設計の指針

- **抽象レイヤ（`IGraphicsDeviceImpl` / `IRenderContextImpl` / `I*Buffer` / `IShader` 等）は変更しない。** 呼び出し側（`UIRenderPipeline`、各 `*Command`、`Engine`）は一切書き換えずに Vulkan へ切り替えられる状態をゴールとする。D3D12 移行で抽象IF追加が `0` 本で済んだ実績があり、Vulkan も同様を目指す。
- DX11 の **イミディエイト** モデルと Vulkan の **コマンドバッファ記録＋VkPipeline** モデルの差は、すべて `VulkanRenderContextImpl` 内部に閉じ込める。差分構造は D3D12 とほぼ同型（保留ステート＋flush）。
- まず「クリア画面が出る」→「メッシュ1枚」→「UI」→「フルパイプライン」と段階的に積む（§12 フェーズ計画）。

### 0.1 確定した方針（設計レビュー結果）

- **Vulkan 1.3 + dynamic rendering**（`VK_KHR_dynamic_rendering`／1.3 コア）。`VkRenderPass`/`VkFramebuffer` オブジェクトは作らない。
- **シェーダは DXC 実行時コンパイル**（HLSL `.fx` → SPIR-V）。`.fx` は改変しない。register は DXC の `-fvk-*-shift` で機械的に Vulkan binding へ写像。
- **Y-flip は負ビューポート高さ**で吸収（HLSL/行列は不変）。
- **メモリは VMA**（Vulkan Memory Allocator）。

### 0.2 PoC 実証結果（2026-06-27、Vulkan SDK 1.4.350.0 / DXC 1.8.2502 実機検証）

設計 §12 推奨の PoC を実行し、**最大の不確実性（register→binding 写像）を実ツールで実証済み**:

- `GBufferLit.fx` を `.fx` 無改変で `dxc -spirv` ＋ shift（`b:0 t:16 s:32 u:48`、全て `all` ステージ）でコンパイル → SPIR-V 生成成功。spirv-dis で binding を確認:
  - `albedoTex(t0)→Binding 16`, `t1→17`, `t2→18`, `t3→19`, `samp(s0)→Binding 32`, `MaterialCB(b2)→Binding 2` ── **§5.1 の写像規約と完全一致**。
  - HLSL `SamplerState` は **separate sampler**（combined ではない）として出力 → descriptor type は sampled image（t#）と sampler（s#）を分離（§3.2 の方針通り）。
- VS 入力レイアウトのリフレクション: `POSITION→Location 0, NORMAL→1, TEXCOORD0→2, TANGENT→3` → `VkVertexInputAttributeDescription` に直写可能（§10）。
- **全シェーダ回帰スイープ: 実エントリを持つ 30 本すべて SPIR-V 生成成功**（残り 5 本 Common/Lighting/PBRFunctions/ShadowSampling/Tonemap はインクルード専用ヘルパ）。
- ツール所在（このマシン）: SDK=`C:\VulkanSDK\1.4.350.0`、`Bin\dxc.exe`（SPIR-V 対応）、`Bin\spirv-dis.exe`、`Bin\glslangValidator.exe`、`Lib\vulkan-1.lib`、`Include\vulkan\`。
- **注意: VMA は SDK 1.4 に非同梱**（`vk_mem_alloc.h` 無し）。GPUOpen から別途ベンダリングする（ヘッダオンリ）。SPIRV-Reflect は `Source\SPIRV-Reflect\spirv_reflect.{h,c}` にソースあり → ThirdParty へ取り込み。

---

## 1. アーキテクチャ全体像

```
GraphicsDevice (抽象, 変更なし)
   └─ impl_ : IGraphicsDeviceImpl
        ├─ D3D11GraphicsDeviceImpl   (既存)
        ├─ D3D12GraphicsDeviceImpl   (既存)
        └─ VulkanGraphicsDeviceImpl  (新規) ← Instance/Device/Queue/Swapchain/VMA 所有

RenderContext (抽象, 変更なし)
   └─ impl_ : IRenderContextImpl
        ├─ D3D11RenderContextImpl    (既存)
        ├─ D3D12RenderContextImpl    (既存)
        └─ VulkanRenderContextImpl   (新規) ← VkCommandBuffer 記録 + Pipeline/DescriptorSet 解決
```

Vulkan で新たに必要となる内部サブシステム（すべて `VulkanGraphicsDeviceImpl` が所有、または近傍に配置）。D3D12 の対応物と 1:1。

| Vulkan サブシステム | D3D12 の対応物 | 役割 | 章 |
|---|---|---|---|
| `VulkanFrameContext`（フレームリングバッファ） | `D3D12FrameContext` | frames-in-flight 管理（CommandPool/Fence/Semaphore/リング） | §2 |
| `VulkanDescriptorManager` | `D3D12DescriptorHeapManager` | DescriptorPool からの per-frame DescriptorSet 確保 | §6 |
| `VulkanUploadAllocator` | `D3D12UploadAllocator` | CB・動的VB/IB の HOST_VISIBLE リング確保（VMA、永続 map） | §7 |
| `VulkanPipelineCache` | `D3D12PipelineStateCache` | (VS,PS,IL,Blend,Depth,RTフォーマット) → `VkPipeline` | §4 |
| `VulkanPipelineLayoutRegistry` | `D3D12RootSignatureRegistry` | `VkDescriptorSetLayout` ＋ `VkPipelineLayout`（gfx/compute） | §5 |
| `VulkanBarrierTracker` | `D3D12ResourceStateTracker` | image layout / access mask 追跡とバリア発行 | §9 |

---

## 2. フレームライフサイクルと GPU 同期

### 2.1 frames-in-flight

- スワップチェーン枚数 `RENDER_TARGET_COUNT = 2`（ヘッダ既定に合わせる）。フレームインフライトも 2（`FRAME_COUNT = 2`）。
- フレームごとに以下を1セット持つ `VulkanFrameContext` をリング化する:
  - `VkCommandPool` ＋ そこから確保した `VkCommandBuffer`（フレーム単位で Reset）
  - アップロードリング領域（CB/動的VB/IB 用、§7）
  - DescriptorPool のフレーム区画（§6）
  - `VkFence`（このフレームの submit 完了待ち）、`imageAvailable` / `renderFinished` の `VkSemaphore`

### 2.2 同期

- **GPU↔CPU**: フレーム頭で当該 `VulkanFrameContext` の `VkFence` を `vkWaitForFences` → `vkResetFences`。到達後に CommandPool / アップロードリング / DescriptorPool 区画を Reset。
- **Acquire/Present**: `vkAcquireNextImageKHR`（`imageAvailable` セマフォ）→ 記録 → `vkQueueSubmit`（wait=`imageAvailable`, signal=`renderFinished`, fence=フレームフェンス）→ `vkQueuePresentKHR`（wait=`renderFinished`）。
- `currentFrame = (currentFrame + 1) % FRAME_COUNT`。
- ※ 初期ブリングアップは D3D12 P2 と同様「Present 毎に full wait」する同期実行から始めてよい（リング巻き戻し再利用が単純化）。安定後に二重バッファ化。

### 2.3 抽象レイヤとの接続点（重要・D3D12 で実証済み）

現状の抽象には明示的な `BeginFrame/EndFrame` が無い。`SetupRenderContext()` は **起動時 1 回** しか呼ばれない（D3D12 実装で確認済みの前提）。したがって:

- フレーム境界は **`RenderContext` の最初の記録呼び出しで `BeginFrameIfNeeded()` を遅延発火**（`vkAcquireNextImageKHR`・CommandBuffer begin・swapchain image を COLOR_ATTACHMENT へ遷移・クリア・初期 viewport）。
- `Present()` が CommandBuffer end / submit / present / フェンス確定を行う。
- **抽象IFへの追加は不要**（D3D12 と同じ結論）。

---

## 3. コマンドバッファ記録（`VulkanRenderContextImpl`）

`VulkanRenderContextImpl` は現フレームの `VkCommandBuffer*` を 1 本保持し、`IRenderContextImpl` の各メソッドを記録呼び出しへ変換する。Vulkan も D3D12 同様、描画コール直前に **VkPipeline と descriptor をまとめて確定**する。「保留ステート（pending state）」を内部に溜め、`Draw* / Dispatch` で flush する設計（D3D12 の `PendingGraphicsState` をそのまま流用）。

### 3.1 保留ステート（D3D12 と同型）

```
struct PendingGraphicsState
{
    IShader*           vs, *ps;             // → Pipeline キー
    入力レイアウト識別子               // → Pipeline キー（VS 由来）
    BlendMode, DepthMode               // → Pipeline キー
    PrimitiveTopology                  // → Pipeline キー
    RT/DS フォーマット集合              // → Pipeline キー（dynamic rendering の attachment format）
    VkBuffer VBV, IBV                  // vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
    バインド済み UBO/SRV/Sampler        // descriptor set 書き込み
    bool dirtyPipeline, dirtyDescriptors;
};
```

### 3.2 `IRenderContextImpl` メソッド → Vulkan 対応表

| 抽象メソッド | Vulkan 実装方針 |
|---|---|
| `OMSetRenderTargets` / `OMSetMRTRenderTargets` | `VkRenderingAttachmentInfo` 配列を保留。直前まで `vkCmdBeginRendering` 中なら `vkCmdEndRendering` してから組み直す。RTフォーマットを Pipeline キーへ記録。対象 image を `COLOR_ATTACHMENT_OPTIMAL` へバリア。 |
| `OMSetRenderTargetWithDepth` | カラー attachment ＋ 深度 attachment を `VkRenderingInfo` に設定。 |
| `OMSetDepthMode` | `DepthMode` を Pipeline キーへ保留（VkPipeline 内包のため即時不可）。 |
| `OMSetBlendMode` | `BlendMode` を Pipeline キーへ保留。 |
| `RSSetViewport` | `vkCmdSetViewport`。**height を負・y をビューポート下端**に設定（§2.4 Y-flip）。dynamic state。 |
| `RSSetScissorRect` / `RSSetScissorEnabled` | `vkCmdSetScissor`。Scissor 無効時は RT 全面の矩形を設定。dynamic state。 |
| `ClearRenderTargetView` / `ClearDepthBuffer` | dynamic rendering では attachment の `loadOp=CLEAR`＋`clearValue` で表現するのが基本。レンダリング途中のクリアは `vkCmdClearAttachments`。 |
| `IASetVertexBuffer` | `vkCmdBindVertexBuffers`（offset/stride は VertexData/SkinnedVertexData 由来）。 |
| `IASetIndexBuffer` | `GetFormat()` を見て `VK_INDEX_TYPE_UINT16 / UINT32` で `vkCmdBindIndexBuffer`。**抽象IFはフォーマット保持済みのため変更不要。** |
| `IASetPrimitiveTopology` | Pipeline キーの topology に反映（VkPipeline 内包）。`VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY` を使えば動的化も可。 |
| `IASetInputLayout` | 入力レイアウト識別子を保留（Pipeline キー）。実体の `VkVertexInputAttributeDescription` は VS の SPIRV リフレクションから生成・キャッシュ。 |
| `VSSetShader` / `PSSetShader` | `pendingVS_/pendingPS_` に保持。Pipeline キー更新。 |
| `VSSetConstantBuffer(slot)` / `PSSetConstantBuffer(slot)` | UBO descriptor へ書き込み（slot→binding は §5 register-shift 規約）。多用なら push descriptor も検討。 |
| `PSSetShaderResource(slot)` | combined image sampler / sampled image を per-frame set へ書き込み。対象 image を `SHADER_READ_ONLY_OPTIMAL` へバリア。 |
| `PSSetSampler(slot)` | サンプラーは少数のため静的に作り置き（§5）。実体は descriptor 書き込み時に参照。 |
| `CSSetShader` 他 CS 系 | compute 用 PipelineLayout＋compute VkPipeline（別キャッシュ）。SSBO/storage image。 |
| `Draw` / `DrawIndexed(count)` / `DrawIndexed(count, start)` | flush（Pipeline 解決→`vkCmdBindPipeline`、descriptor 確定→`vkCmdBindDescriptorSets`）後 `vkCmdDraw` / `vkCmdDrawIndexed`。 |
| `Dispatch` | compute Pipeline/descriptor 確定後 `vkCmdDispatch`。 |
| `UpdateConstantBuffer` | アップロードリング（HOST_VISIBLE 永続 map）に memcpy。UBO descriptor がそのスライスを指す（§7）。 |
| `OMSetDepthOnlyTarget(Slice)` / `ClearDepthMap(Slice)` | シャドウ用。`VkRenderingInfo` を深度 attachment のみで構成。配列スライスは `VkImageView`（layered）を slice 指定で生成。 |
| `IASetIndexBufferGpu` / `DrawIndexedIndirect` | `vkCmdBindIndexBuffer`（storage buffer を index source）/ `vkCmdDrawIndexedIndirect`。 |
| `UavBarrier` | `vkCmdPipelineBarrier2` ＋ `VkMemoryBarrier2`（SHADER_WRITE→SHADER_READ）。 |
| `PSUnsetShaderResource` / `PSUnsetShader` 等 | Vulkan は明示アンバインド不要。基本 no-op、次用途バリアで解決（D3D12 と同じ）。 |

### 2.4 Y-flip（クリップ空間一致）

Vulkan NDC は Y が下向き（D3D12/D3D11 は Y 上向き）、深度レンジは 0..1（D3D12 と同じ）。HLSL シェーダと射影行列を一切変えずに座標系を合わせるため、`vkCmdSetViewport` で **`height` を負値、`y` をビューポート下端**に設定する（`VK_KHR_maintenance1`、1.1 コア）。フロントフェイス巻き方向（CCW/CW）は負ビューポートで反転するため、ラスタライザの `frontFace` を D3D 既定に合わせて選ぶ。

---

## 4. パイプライン（`VkPipeline`）キャッシュ

DX11 の個別ステート設定／D3D12 の `ID3D12PipelineState` に相当。Vulkan は `VkGraphicsPipelineCreateInfo`（dynamic rendering 用に `VkPipelineRenderingCreateInfo` を pNext チェーン）で 1 つの `VkPipeline` に集約。

### 4.1 キー（D3D12 とほぼ同一）

```
struct PipelineKey
{
    void*             vsModule;      // VkShaderModule / IShader の同一性
    void*             psModule;
    uint8_t           inputLayoutId; // VertexData / SkinnedVertexData / UIVertex …
    BlendMode         blend;
    DepthMode         depth;
    PrimitiveTopology topo;
    uint8_t           rtCount;
    VkFormat          rtFormats[8];  // dynamic rendering の color attachment format
    VkFormat          dsFormat;
};
```
`std::unordered_map<PipelineKey, VkPipeline>`（memcmp ハッシュ）。`VkPipelineCache` をディスク永続化すれば再起動後の生成コストも削減可。

### 4.2 生成タイミング

- **遅延生成**: `Draw*` の flush 時にキーを組み、未ヒットなら `vkCreateGraphicsPipelines`。
- ラスタライザ/ブレンド/デプスの `Vk*StateCreateInfo` は `BlendMode`/`DepthMode` から変換するヘルパで生成（D3D12 の RS/BS/DSS 変換表を踏襲）。viewport/scissor/（任意で topology）は **dynamic state** にして PSO 爆発を抑える。

---

## 5. パイプラインレイアウト & descriptor レイアウト（`VulkanPipelineLayoutRegistry`）

D3D12 の静的ルートシグネチャに相当。エンジンのバインドモデル（スロット番号）に合わせた **固定 DescriptorSetLayout** を用意する。

### 5.1 register → binding 写像規約（最重要）

DXC `-spirv` のレジスタシフトで HLSL の `b#/t#/s#/u#` を Vulkan binding へ機械写像する。推奨レイアウト:

| HLSL | Vulkan | DXC フラグ例 |
|---|---|---|
| `b0..b5`（CBV） | set=0, binding = 0 + register | `-fvk-b-shift 0 0` |
| `t0..t11`（SRV テクスチャ／構造化） | set=0, binding = 16 + register | `-fvk-t-shift 16 0` |
| `s0..s1`（Sampler） | set=0, binding = 32 + register | `-fvk-s-shift 32 0` |
| `u0..`（UAV） | set=0, binding = 48 + register | `-fvk-u-shift 48 0` |

- 単一 set（set=0）に集約し、binding 空間をレジスタ種別ごとにシフトで分離する。**`.fx` の register 宣言は不変**。
- DescriptorSetLayout は「実際に使う最大レンジ」で固定生成（D3D12 の t0..t11 連続テーブルと同じ思想）。未使用 binding は `PARTIALLY_BOUND`（`VK_EXT_descriptor_indexing`、1.2 コア）または null descriptor 相当で埋める。
- **b0 の VS/PS 二義性**（UI は PS で b0、メッシュは VS で b0）: D3D12 は ALL 可視で吸収したが、Vulkan は SPIRV リフレクションで **実使用ステージの `stageFlags`** を立てて DescriptorSetLayout を構成する方が素直。用途別に最大 2〜3 種の layout を持ち、PipelineLayout を使い分ける。

### 5.2 compute 用

CS の UBO/SRV/UAV/Sampler 用に別 DescriptorSetLayout＋PipelineLayout。`Dispatch` 経路専用（クラスタカリング・HiZ・Bloom 等が使用）。

---

## 6. descriptor 管理（`VulkanDescriptorManager`）

| 対象 | 方針 |
|---|---|
| DescriptorPool | フレームごとに区画を持つ（または十分大きい単一プール）。フレーム頭で `vkResetDescriptorPool`。 |
| per-Draw set | Draw 毎に `vkAllocateDescriptorSets` → `vkUpdateDescriptorSets` で UBO/texture/sampler を書き込み → `vkCmdBindDescriptorSets`。 |
| Sampler | 種類が少数（Clamp/Wrap・比較サンプラー）。**作り置き**して descriptor 書き込み時に参照（D3D12 静的サンプラー s0/s1 相当）。 |

- プールサイズは「1 フレーム最大 Draw 数 × set あたり descriptor 数」で見積もる（UI バッチ＋メッシュ＋ライティングが支配的、D3D12 のリング見積りと同じ）。
- 高頻度更新の UBO は **push descriptor**（`VK_KHR_push_descriptor`）で `vkUpdateDescriptorSets` を省く最適化も選択肢。

---

## 7. アップロード/メモリアロケータ（`VulkanUploadAllocator` ＋ VMA）

DX11 の `UpdateSubresource` / DYNAMIC（Map/Discard）、D3D12 の UploadAllocator を Vulkan で再現する。割り当ては **VMA** に委譲。

- **定数バッファ（`CreateConstantBuffer` / `UpdateConstantBuffer`）**: `minUniformBufferOffsetAlignment` 整列で HOST_VISIBLE リングからスライス確保 → memcpy → UBO descriptor がスライスを指す。毎フレーム書き換え前提で HOST_VISIBLE 常駐。
- **動的VB/IB（`CreateDynamicVertexBuffer` / `CreateDynamicIndexBuffer`）**: HOST_VISIBLE 永続 map バッファに直接 memcpy（UI が依存する経路）。
- **静的VB/IB/テクスチャ**: DEVICE_LOCAL に本体を作り、HOST_VISIBLE ステージング経由で `vkCmdCopyBuffer` / `vkCmdCopyBufferToImage`、その後 layout/access バリア。
- VMA がメモリタイプ選択・サブアロケーション・defragmentation を担当（D3D12 のヒープ手管理を肩代わり）。

---

## 8. レンダーターゲット / 深度 / スワップチェーン

- **サーフェス/スワップチェーン**: `VK_KHR_surface` ＋ `VK_KHR_win32_surface`（`vkCreateWin32SurfaceKHR`、`NativeWindowHandle.handle` = HWND）。`vkCreateSwapchainKHR`、`FIFO` または `MAILBOX` present mode。
- **`IRenderTarget` 実装（`VulkanRenderTarget`）**: color `VkImage`＋`VkImageView`（＋ SRV 用 sampled view）。オフスクリーン/メイン（swapchain image）を統一的に扱う。`GetRenderTarget`/`CreateOffscreenRenderTarget` を実装。
- **`IDepthMap` 実装（`VulkanDepthMap`）**: depth `VkImage`＋DSV 相当 view（＋ sampled view）。配列対応（シャドウ用 layered view、slice 指定 `VkImageView`）。
- **`CopyToBackBuffer`**: 確定 RT → swapchain image へ `vkCmdBlitImage`（フォーマット差/スケール対応）または `vkCmdCopyImage`。前後で layout 遷移。
- dynamic rendering 採用のため `VkRenderPass`/`VkFramebuffer` は不要。各パスは `vkCmdBeginRendering`/`vkCmdEndRendering` で囲む。

---

## 9. リソースステート追跡とバリア（`VulkanBarrierTracker`）

- 各 `VkImage`/`VkBuffer` に「現在の layout / access mask / stage」を持たせ、`OMSetRenderTargets`（→`COLOR_ATTACHMENT_OPTIMAL`）、`PSSetShaderResource`（→`SHADER_READ_ONLY_OPTIMAL`）、`CopyToBackBuffer`（→`TRANSFER_SRC/DST`）、`Present`（→`PRESENT_SRC_KHR`）で必要な `vkCmdPipelineBarrier2`（`VK_KHR_synchronization2`／1.3 コア）を自動発行。
- D3D12 の ResourceStateTracker をほぼ移植。DX11 の暗黙ハザード管理（`PSUnsetShaderResource`）は **layout/メモリバリアに翻訳**。`PSUnset*` は基本 no-op、次に RT/書き込みとして使う時にバリアで解決。

---

## 10. シェーダーコンパイル / 入力レイアウト

- 既存 `.fx`（HLSL）を **DXC（`dxcompiler.dll`）** で `-spirv` ターゲットにコンパイルし SPIR-V を得る。プロファイルは `vs_6_x/ps_6_x/cs_6_x`。entry=`main`（現状と同じ）。register シフトは §5.1。
- `IShader::GetByteCode()` は SPIR-V ワード列を返すだけで、**抽象IFは変更不要**。`VulkanShader` が SPIR-V から `vkCreateShaderModule` で `VkShaderModule` を生成・保持。
- 入力レイアウトは **SPIRV-Reflect** で VS の入力変数をリフレクションし `VkVertexInputAttributeDescription`/`VkVertexInputBindingDescription` を生成（D3D12 の `BuildInputLayout` 相当）。`inputLayoutId`（VertexData / SkinnedVertexData / UIVertex …）で Pipeline キー化。
- DXC/SPIRV-Reflect は実行時ロード（dll）。将来オフライン事前 SPIR-V 化（ビルド時 dxc）へ移行可能な抽象に保つ。

---

## 11. バックエンド選択とビルド配線

- **選択**: `Engine.cpp` のデバイス生成を `#ifdef ENGINE_GRAPHICS_VULKAN` で三分岐（D3D11/D3D12/Vulkan）。`aq.h:9` の `// #define ENGINE_GRAPHICS_VULKAN` を有効化して切り替え。
- **aq.h**: `ENGINE_GRAPHICS_VULKAN` ブロックを D3D11/D3D12 と並列に追加（`<vulkan/vulkan.h>` 等、`vulkan-1.lib` リンク）。Windows 専用シンボル（DirectInput 等）は維持。
- **依存追加**:
  - Vulkan SDK（`vulkan-1.lib`、ヘッダ）。
  - **VMA**（Vulkan Memory Allocator、ヘッダオンリ、1 cpp で実装定義）。
  - **DXC**（`dxcompiler.lib` / `dxcompiler.dll` / `dxil.dll`、HLSL→SPIR-V）。
  - **SPIRV-Reflect**（小規模、ソース同梱可、入力レイアウト/descriptor リフレクション）。
- **vcxproj**: 新規 `Vulkan/*.cpp` を `<ClCompile>` 追加。インクルード/ライブラリパスに Vulkan SDK を追加。
- **imgui**: `imgui_impl_dx11`/`_dx12` → `imgui_impl_vulkan` へ差し替え（`Application.cpp` の `dynamic_cast` 部分を #ifdef 三分岐）。D3D12 同様、初期は no-op で後回し可（§12 P4）。
- **静的アクセサ**: D3D11/D3D12 が `GetStaticDevice()` をリソース生成で参照するパターンを踏襲し、`VulkanGraphicsDeviceImpl::GetStaticDevice()`（`VkDevice`＋VMA allocator＋転送キュー）を用意。

---

## 12. フェーズ計画（段階的実装。D3D12 を踏襲）

| Phase | 内容 | 到達点 | 状態 |
|---|---|---|---|
| **P0** | Instance/Device/Queue/Surface/Swapchain/CommandBuffer/Fence/Semaphore/Present（VMA は P1a へ後送り＝P0 では不要） | 画面クリア色が出る | ✅ 実装済（D3D12/Vulkan 両構成で **Game.exe ビルド/リンク検証済**・実機描画未検証） |
| **P0.5** | D3D11/D3D12 ソースの `#ifdef` ガードに Vulkan 分岐追加＋バックエンド選択配線 | define 切替でビルド成立 | ✅ 実装済（aq.h/Engine.cpp/Application.cpp 三分岐・vcxproj 配線・両構成緑） |
| **P1a** | VB/IB/CB 生成（VMA）＋DXC で SPIR-V 生成＋SPIRV-Reflect 入力レイアウト | リソース生成層が揃う | ✅ 実装済（両構成で **Game.exe ビルド/リンク検証済**・実機未検証。register→binding は §0.2 実証済） |
| **P1b** | PipelineLayout＋VkPipeline キャッシュ＋RenderContext 記録（保留＋flush）＋dynamic rendering＋フレーム begin/end | 三角形・静的メッシュが出る | ✅ 実装済（両構成で **Game.exe ビルド/リンク検証済**・実機未。単一カラーRTのみ、深度/MRT/テクスチャは P2/P3） |
| **P2** | テクスチャ＋サンプラー（descriptor set／pool リング） | テクスチャ付きメッシュ・**UI** が出る | ✅ 実装済（両構成で **Game.exe ビルド/リンク検証済**・実機未。VulkanTexture/Sampler＋SRV/Sampler descriptor 書込） |
| **P3** | オフスクリーンRT＋深度＋デプスモード／ブレンド／シザー＋layout バリア追跡 | Deferred/ポストプロセス相当 | ✅ 実装済（両構成で **Game.exe ビルド/リンク検証済**・実機未。MRT＋深度＋offscreen RT＋VulkanDepthMap＋CopyToBackBuffer＋バリア追跡） |
| **P4** | compute（Dispatch/SSBO/storage image）＋シャドウ（layered depth）＋imgui vulkan | フルパイプライン | ✅ compute＋シャドウ深度＋**imgui 自前実装**まで実機動作（海＋キャラ＋ライティング＋デバッグUI・検証エラー0）。GPU 駆動カリング buffer のみ未（reflection 駆動 descriptor が必要な最適化・描画は無効でも正常） |

各フェーズ後に D3D11/D3D12/Vulkan を切替ビルドして回帰確認。

> **P0/P0.5 実装メモ（2026-06-28）**: 新規 `Graphics/Vulkan/`（VulkanCommon.h / VulkanGraphicsDeviceImpl.{h,cpp} / VulkanRenderContextImpl.{h,cpp}）。Vulkan 1.3 + dynamic rendering でスワップチェーン画像を `vkCmdBeginRendering(loadOp=CLEAR)` クリア。同期は `VkSubmitInfo2`/`vkQueueSubmit2`、レイアウト遷移は `vkCmdPipelineBarrier2`（sync2）。リソースファクトリは P0 では nullptr スタブ、`GetMainRenderTarget` は assert（実シーン描画は P1b 以降）。imgui は Vulkan で無効化（`Application.cpp` の init で `backendOk=false`＋assert 抑止）。
> - **ビルド配線**: Vulkan の `.cpp` は先頭 `#include "aq.h"`（PCH）＋本体を `#ifdef ENGINE_GRAPHICS_VULKAN` でガード（非Vulkan構成は空TU＝SDK不要）。include=`$(VULKAN_SDK)\Include`（Engine.vcxproj）、lib=`$(VULKAN_SDK)\Lib`（Game/DirectX.vcxproj）、`vulkan-1.lib` は aq.h の `#pragma comment(lib)`（Vulkan時のみ）。msbuild は `$env:VULKAN_SDK` 必須・**PowerShell で実行**（Git Bash の `/p:` は壊れる）。

> **P1a 実装メモ（2026-06-28）**: 新規 VulkanBuffers.{h,cpp}／VulkanShader.{h,cpp}／VulkanVMAImpl.cpp。
> - **バッファ**: VMA で HOST_VISIBLE 永続 Map（D3D12 P1 と同じ「アップロードヒープ常駐」思想）。動的 VB/IB と全 CB は 1 バッファ内に `FRAME_COUNT` 領域をリング（`GetCurrentOffset()`＝frameIndex×領域サイズ）、静的は 1 領域。UBO は 256 整列。
> - **シェーダ**: DXC（`IDxcCompiler3`）で `.fx` を実行時 SPIR-V 化。引数に `-spirv -fspv-entrypoint-name=main -E <HLSL entry> -T vs/ps/cs_6_0 -fvk-{b:0,t:16,s:32,u:48}-shift all -I <shaderDir>`。`#include` は `-I`＋CWD 切替で解決。VkPipeline の `pName` は固定 `"main"`。VS 入力レイアウトは SPIRV-Reflect で location 順 packed offset（CPU の VertexData/SkinnedVertexData と一致前提、D3D12 の APPEND_ALIGNED と同思想）。`built_in==-1` で SV_* を除外。
> - **ベンダリング**: VMA（GPUOpen master 単一ヘッダ）＋ SPIRV-Reflect（SDK Source からコピー・include ツリー同梱で自己完結）を `ThirdParty/{vma,spirv_reflect}/` に配置。`spirv_reflect.c` は vcxproj で `PrecompiledHeader=NotUsing`。`VMA_IMPLEMENTATION` は VulkanVMAImpl.cpp の 1 箇所のみ。
> - **要注意（実走時）**: `dxcompiler.dll` がランタイムに必要。Vulkan SDK の Bin が PATH に無い環境では Game.exe 隣にコピーする post-build が要る（P1b 実走で確認）。

> **P1b 実装メモ（2026-06-28）**: 新規 VulkanPipelineLayout.{h,cpp}／VulkanPipelineCache.{h,cpp}／VulkanRenderTarget.{h,cpp}＋VulkanRenderContextImpl 全面書き換え。
> - **PipelineLayout**: 単一 set=0。UBO `b0..b5→binding 0..5`、SRV `t0..t11→16..27`、Sampler `s0..s1→32..33`（§5.1）。`stageFlags=ALL_GRAPHICS`（b0 の VS/PS 二義性を吸収）。未使用 binding は「シェーダが静的に参照しなければ未書き込みで可」。
> - **PipelineCache**: キー（vs/ps module・topo・blend・depth・rtFormats[8]・dsFormat）を memcmp/FNV で索引。`VkPipelineRenderingCreateInfo` を pNext に繋いで dynamic rendering 用に生成。viewport/scissor は `VK_DYNAMIC_STATE_*`、cullMode=NONE（P1b 安全側、winding 検証後 BACK へ）、frontFace=CW（負ビューポート Y-flip と D3D 既定に整合）、頂点入力は VS の SPIRV-Reflect 由来。
> - **RenderContext flush**: D3D12 と同じ「保留ステート→Draw 時 flush」。`OMSetRenderTargets` で `EndRenderingIfActive`、`ClearRenderTargetView` で `vkCmdBeginRendering(loadOp=CLEAR)`、`Draw*` で PSO 解決→`vkCmdBindPipeline`→viewport（Y-flip 負 height）/scissor→VB バインド→ディスクリプタ（現状 UBO のみ書込）→`vkCmdDraw(Indexed)`。`device.Present()` 冒頭で `activeContext_->EndRenderingIfActive()` し rendering を閉じてから present 遷移。
> - **RenderTarget**: Phase 1b は swapchain proxy のみ（`GetView/GetImage` を device の現在 swapchain へ解決）。`GetRenderTargetSRV/UAV` は Phase 2 まで assert。
> - **Device 拡張**: `FrameResources` に `VkDescriptorPool` を追加（`BeginFrameIfNeeded` で `vkResetDescriptorPool`）。`mainRT_`（proxy）・`activeContext_`・swapchain/pool アクセサを追加。
> - **制約（実シーン未到達）**: 単一カラー RT のみ。MRT・深度・オフスクリーン RT は Phase 3、テクスチャ/サンプラのバインドは Phase 2。エンジンの Deferred 経路は P3 まで動かない。

> **PoC 推奨（P1a 着手前に 1 本）**: `register` を複数種使う 1 本のシェーダ（例 `GBufferLit.fx` か `UISprite.fx`）を DXC `-spirv`＋shift でコンパイル→SPIRV-Reflect→`VkPipeline` 生成まで往復検証し、§5.1 の binding 写像規約を確定させる。ここが Vulkan 化最大の不確実性（113 箇所の register）。

---

## 13. オープン課題 / 要確認

1. **register→binding 写像の最終確定**: §5.1 を PoC で実証。特に `t#` に「テクスチャ」と「構造化バッファ(SRV)」が混在する場合の binding 種別（sampled image vs storage buffer）の切り分け。SPIRV-Reflect の descriptor type で判定する。
2. **b0 の VS/PS 二義性**: 単一 set で `stageFlags` を実使用ステージに合わせるか、用途別 DescriptorSetLayout にするか。リフレクション結果から自動構成する方針を P1b で確定。
3. **descriptor pool サイズ見積り**: 1 フレーム最大 Draw 数 × set あたり descriptor 数。UI バッチの DrawRange 数が支配的（D3D12 リング見積りを流用）。
4. **dynamic rendering 中の RT 切替コスト**: `OMSet*RenderTargets` 毎に `vkCmdEndRendering`/`vkCmdBeginRendering` が入る。パス境界が多い箇所（Deferred→Lighting→ポスプロ）でバッチ最適化が要るか計測。
5. **`UpdateConstantBuffer` の同一フレーム複数更新**: UI の CircleGauge/SdfText が DrawRange 毎更新。UBO はリング確保で都度新スライスにする（D3D12 と同じ結論）。
6. **キュー構成**: グラフィクス／present／転送を単一キューで始めるか分けるか。初期は単一グラフィクスキュー（present 兼）で十分。
7. **抽象IFへの追加は最小限に**: 現状の見積りでは追加 `0` 本（D3D12 実績）。`ReadbackOffscreenR32`/GPU 駆動 IF も既存のまま実装可能。

---

## 14. 影響範囲まとめ（実装フェーズでの新規/変更ファイル）

**新規（`Graphics/Vulkan/`、D3D12 と同名規則）**
- `VulkanGraphicsDeviceImpl.{h,cpp}`（lifecycle/factory、Instance/Device/Swapchain/VMA）
- `VulkanRenderContextImpl.{h,cpp}`（記録・保留ステート・flush）
- `VulkanBuffers.{h,cpp}`（VB/IB/CB＋動的、VMA）
- `VulkanResources.{h,cpp}`（SRV/UAV/Sampler/Texture）
- `VulkanRenderTarget.{h,cpp}` / `VulkanDepthMap.{h,cpp}`
- `VulkanPipelineCache.{h,cpp}` / `VulkanPipelineLayout.{h,cpp}`
- `VulkanDescriptor.{h,cpp}` / `VulkanUploadAllocator.{h,cpp}` / `VulkanFrameContext.{h,cpp}`
- `VulkanBarrierTracker.{h,cpp}`
- `VulkanShader.{h,cpp}`（DXC コンパイル＋SPIRV-Reflect）
- `VulkanCommon.h`（共通 include / VK_CHECK マクロ / 列挙変換ヘルパ）

**変更**
- `aq.h`（`ENGINE_GRAPHICS_VULKAN` ブロック追加）
- `Engine.cpp`（バックエンド選択 #ifdef 三分岐）
- `Application.cpp`（imgui/dynamic_cast の #ifdef 三分岐）
- `aqEngine/Engine.vcxproj` / `Game/*.vcxproj`（ClCompile 追加・Vulkan SDK/lib リンク・include path）

**変更不要（抽象越しに Vulkan へ追従）**
- `UIRenderPipeline` / `UIBatchRenderCommand` / 各 `*Command` / `GraphicsDevice` / `RenderContext`
- 各 `.fx` シェーダ（DXC `-fvk-*-shift` で register 不変のままコンパイル）
