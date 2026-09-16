---
name: render-pipeline-phase-status
description: レンダーパイプラインのパス列化(案 A)の到達点 — P1・P2 完了(279141a)。次は P3(分割画面の確認と BuildCommandListViews 削除)
metadata: 
  node_type: memory
  type: project
  originSessionId: 38a783de-3acb-4440-a83e-740ed3a5818e
  modified: 2026-09-16T09:08:35.106Z
---

一次資料は `DirectX/設計書/レンダーパイプライン設計.md`。案の比較は Artifact「レンダーパイプライン構成案」
(https://claude.ai/artifact/361XGM83EoLzG94y6mpCzA)。**2026-09-16 に案 A(パス列方式)で決定、P1 完了(`c60d345`)。**

到達点:

- **P1 完了** — `Rendering/Pipeline/` に `IRenderPass` / `PassResources` / `PipelineBuilder` / `RenderPipeline` / `PipelinePresets` と 12 パス。
  `Renderer` は委譲だけ(345 → 106 行)。画素比較で変更前後の差はタイトル 0.015、ステージ 0.13〜0.46(/255)。Windows ビルドは未確認
- **P2 完了**(`279141a` + Sample `030b35a`)— 検証を失敗に、ポスト 3 パス分割(`*Effect` に改名して所有)、
  `InsertAfter/Before/Replace/Remove`、`FullscreenComputeCommand`、`Mobile()` / `ForCurrentPlatform()` / `RendererPreset::pipeline`、
  `Application::BuildStandardPipeline`、`Vignette.fx` + Sample `VignettePass` + README 4 歩目。iOS シミュレータで 6 パス除外を確認。
  Windows ビルドは P1 / P2 とも未確認
- P3 = 分割画面の目視確認 + `Renderer::BuildCommandListViews` 削除(`BuildViews` 自体は P1 で実装済み)
- P4 = ミニマップを 2 本目のパイプラインへ(Game)。P5 = 輪郭線パスで実証 + 旧 API 削除

**How to apply:**
- 画素比較の手順: 起動 9 秒でタイトル、前面化 → `evthold 49 0.3` → 12 秒でステージを `screencapture -l<winid>`。
  比較は scratchpad の `imgdiff.py`(PIL 無し、`sips` で BMP 化)。基準は平均 2/255。草の揺れとタイマーで 0.5 程度は出る
- パスを書くときの契約は `IRenderPass.h` の冒頭コメント。Scene に描くパスは自分で RT をバインドする(前のパスに依存しない)
- 掲示板のキーは `MakePassKey("...")`(FNV-1a)。`aqHash32` ではない
- `HiZPass::Setup` 失敗はパイプライン全体の失敗(旧実装は Hi-Z だけ無効化)。環境で弾くなら `IsSupported` 側で
- 実装の委譲はユーザー指示で **Sonnet**(feature-dev-cycle の既定 Opus から切替済み)

関連: [[usability-port-phase-status]] / [[mac-visual-verification]]
