---
name: frame-index-modulo-pitfall
description: 「フレームが変わったか」を FRAME_COUNT の剰余で判定してはいけない — リングが回収されず描画が壊れる
metadata: 
  node_type: memory
  type: feedback
  originSessionId: ed6c6330-e53a-4e41-b0cb-ca75dea2f118
  modified: 2026-09-12T05:58:15.058Z
---

このエンジンには **frames-in-flight のリングが 2 系統**あり、どちらも周期 2 になっている。

- `metal::FRAME_COUNT` / Vulkan の `FRAME_COUNT` = 2(GPU のリング位置)
- `RenderThread::FRAMES_IN_FLIGHT` = 2(`AQ_RENDER_PIPELINED` 未定義時。定義時は 4)

そのため、**あるフレームスロットが持つリソースは常に同じ剰余値を見る**。
`frameIndex != lastFrameIndex` のような剰余の等値比較は**永久に偽**になり、
「フレームが変わった」を一度も検出できない。

2026-09-12 にこれで踏んだ不具合: 定数バッファのカーソルがリセットされず、タイトル画面で
放置すると約 28 秒でリングが上限に達し、全描画が最後の値で潰れて**画面が真っ黒**になった。
Metal・Vulkan の両方に同じ欠陥があった(Vulkan はリングを伸ばさず潰すので症状が違うだけ)。

**Why:** 剰余はリング上の「位置」であって「世代」ではない。位置の等値は世代の等値を意味しない。

**How to apply:** 「フレームが変わったか」の判定には **Present ごとに増える単調増加の通し番号**
(`MetalGraphicsDeviceImpl::GetFrameSerial()` / `VulkanGraphicsDeviceImpl::GetStaticFrameSerial()`)
を使う。剰余の frameIndex はバッファ内オフセットの計算にだけ使う。
リセットして安全なのは、frames-in-flight がセマフォ/フェンスで FRAME_COUNT に制限されており、
通し番号が変われば FRAME_COUNT 前のフレームが完了しているため。
関連: [[metal-backend-gotchas]]
