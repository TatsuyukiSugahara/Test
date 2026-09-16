---
name: metal-backend-gotchas
description: Metal バックエンドで踏んだ「黙って壊れる」罠。blit のチャンネル入れ替え、threadsPerThreadgroup、binding シフト、Validation が既定オフ
metadata: 
  node_type: memory
  type: project
  originSessionId: 67329cf3-7e0e-4cab-b8c5-681ec8644ca3
  modified: 2026-09-11T06:27:14.866Z
---

`aqEngine/Graphics/Metal/` を 2026-09-11 に P0〜P6 で作った(設計書 `MetalBackend設計.md`)。
**Vulkan / D3D12 の感覚で書くと黙って壊れる**ものだけを挙げる。

1. **blit はフォーマットが違っても通るが生バイトコピー**。`copyFromTexture:` は
   `RGBA8Unorm → BGRA8Unorm` を**通してしまい、R と B が入れ替わる**(赤が青になる)。
   `RGBA16Float → BGRA8` のほうは Validation がアサートで止める。
   **前者は何も言わずに色が壊れる。** 不一致時はフルスクリーン描画で変換すること。

2. **`threadsPerThreadgroup` は PSO から取れない**。`threadExecutionWidth` /
   `maxTotalThreadsPerThreadgroup` から推測すると値がずれる。
   **`.spv` の `OpExecutionMode LocalSize` を `spirv_reflect` で読む**のが唯一確実。
   決め打ちにすると HLSL の `[numthreads]` と食い違って結果が静かに壊れる。

3. **binding シフトは Metal の名前空間が別でも 0 始まりに揃えてはいけない**。
   中間生成物の **SPIR-V は Vulkan の統一 binding 名前空間**なので、そこでバッファ同士が
   衝突すると spirv-cross が `device void* spvBufferAliasSet0Binding0` を作り、
   アドレス空間をまたぐ不正な MSL を吐く。**`b` / `t` / `u` は重ねないこと**
   (現行は b:0 / t:8 / s:0 / u:24)。`s` だけは重なってよい。

4. **Metal API Validation は既定で無効**。実行時に **`METAL_DEVICE_WRAPPER_TYPE=1`** を
   付けると `Metal API Validation Enabled` が出る。Vulkan は SDK を入れると layer が
   既定で効くので、同じ感覚でいると**「エラーが無い」のか「検証していない」のか区別が付かない**。

5. **`UavBarrier` は no-op が正解**。`memoryBarrierWithScope:` は
   `MTLDispatchTypeConcurrent` 専用で、serial のまま呼ぶと Validation がエラーにする。
   serial エンコーダなら同一エンコーダ内は順に実行され、跨ぎは `MTLCommandBuffer` が保証する。

6. **深度は PSO に含まれない**。`MTLDepthStencilState` は別オブジェクトなので、
   「深度アタッチメントが無いのに depth write 有効」を**コンテキスト側で**弾く必要がある
   (Vulkan は PSO 生成側で無効化している)。

7. **`ImGuiVK.fx` は唯一 Vulkan NDC 前提の `.fx`**。Metal の NDC は D3D と同じなので
   通常シェーダに Y-flip は要らないが、これだけは投影定数で反転を吸収しないと
   **UI が上下逆さま**になる。

関連: [[mac-port-phase-status]] / [[mac-build-environment]]
