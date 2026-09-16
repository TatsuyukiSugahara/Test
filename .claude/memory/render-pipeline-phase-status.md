---
name: render-pipeline-phase-status
description: レンダーパイプラインのパス列化(案 A)— P1〜P5 完了(e6409b3)。残るは Mac / iOS / Android での P3〜P5 のビルド確認
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
  `Renderer` は委譲だけ(345 → 106 行)。画素比較で変更前後の差はタイトル 0.015、ステージ 0.13〜0.46(/255)
- **P2 完了**(`279141a` + Sample `030b35a`)— 検証を失敗に、ポスト 3 パス分割(`*Effect` に改名して所有)、
  `InsertAfter/Before/Replace/Remove`、`FullscreenComputeCommand`、`Mobile()` / `ForCurrentPlatform()` / `RendererPreset::pipeline`、
  `Application::BuildStandardPipeline`、`Vignette.fx` + Sample `VignettePass` + README 4 歩目。iOS シミュレータで 6 パス除外を確認
- **P3 完了**(`7145416`。設計書は `8114491`)— `Renderer::BuildCommandListViews` と `Renderer::ViewRect` 別名を削除し、
  `Application` は `GetPipeline()->BuildViews()` を直接呼ぶ。**Windows(D3D11 / D3D12 / Vulkan)の Debug x64 で
  P1〜P3 のビルドを確認**(0 エラー / 警告 54 件)。分割の確認は AquaDash に一時プローブを仮組みして実施し、確認後に revert
- **P4 完了**(`b067de7`。設計書は `cfe7b63`)— ミニマップの俯瞰ベイクを、ゲームが `PipelineBuilder` で組む
  2 本目の `RenderPipeline`(`GBufferPass > DeferredLightingPass > ForwardPass`)へ。`OffscreenScenePass` は削除し、
  `MakeNeutralShadowCBData()` は `Rendering/Shadow/ShadowData.h` の inline 関数へ移した。
  置き換え前後でミニマップ矩形の画素は完全一致(平均 0.000 / 最大 0)
- **P5 完了**(`e6409b3`。設計書は `5c8ed94` / `a815d86`)— `OutlinePass`(エンジンの**任意**パス。`Standard()` には
  入れない)+ `Outline.fx`。worldPos から作ったカメラ距離の隣接差でエッジを拾い、色 / 濃さ / しきい値 / 太さは `Set*` で指定。
  AquaDash は `InsertBefore<UIPass>` で挿す。`Renderer` の互換アクセサ(`GetShadowRenderer` 等)を削除し、
  呼び出し側は `Find<ShadowPass>()` / `Find<GBufferPass>()` へ。01_レンダリング設計.md §3 は本書へのリンクに
- **全フェーズ完了。残作業 = Mac / iOS / Android での P3〜P5 のビルド確認**(Windows 3 構成は確認済み)

**How to apply:**
- **`Depth` キーからは深度を読めない**(実体は GBuffer0 のハンドルで、SRV は albedo)。深度が要るパスは
  `WorldPos`(GBuffer2)からカメラ距離を作る(Hi-Z と同じ)
- **`Setup` 失敗 = パイプライン全体の失敗**。任意入力が無いだけのときは何も登録せず `true` を返し、`Build` で早期 return する
- **Windows の Vulkan は `.spv` が無ければ実行時 DXC にフォールバック**するので、シェーダを足しても Windows は追加作業不要。
  `.spv` / `.msl` は git 管理外(Mac / Android / iOS のビルド手順が生成する)
- **2 本目のパイプラインは掲示板ごと別**(`PassResources` は `RenderPipeline` が 1 枚ずつ所有)。`Scene` / `GBuffer0` が
  同名でも干渉しない。`[pipeline] 確定:` が 2 行出るのが正常
- **`DeferredRenderer` の寸法は `GBufferPass::Setup` 任せ**。未 `Create` の `shared_ptr` を渡せば `Build(w,h)` の寸法で作る
  (`PipelinePresets::Standard` だけが `Engine::GetRenderWidth/Height` で先に `Create` している)
- **オフスクリーンの RT / クリア / ビューポートは呼び出し側が積む**(メインパスと同じ作法)。Submit は `displayRT = INVALID`
- **分割画面を呼ぶゲームコードは無い**(`74f4cf8` で一人プレイ専用に。`SetSplitViews` はエンジンに残るだけ)。
  確認するときは AquaDash の `OnUpdate` に「Num2=左右 2 分割 / Num1=解除 / Num3=全画面 1 ビュー」を仮組みし、
  2 ビューへ**同じメインカメラ**を渡す(左右が同じ絵になるので全面クリアの二重積みがすぐ見える)。
  `BuildViews(viewCount=1)` を通すには `Application.cpp` のゲート `splitViews_.size() >= 2` を一時的に `>= 1` にする
- **Windows でキー入力を注入するときは scan code**(`keybd_event(0, sc, KEYEVENTF_SCANCODE, 0)`)。
  ゲーム入力は DirectInput 経由なので VK だけの注入は届かない。F1(ImGui のデバッグ UI トグル)だけは Win32 メッセージ側なので届く。
  画素比較は BMP で撮って scratchpad の `imgdiff.py`(PIL 無し)。**経路の差を見るときは切り替え直後の 2 枚で比べる**
  (タイトルの点滅文字だけで時間差 3 秒なら 0.3/255 出る)
- 画素比較の手順: 起動 9 秒でタイトル、前面化 → `evthold 49 0.3` → 12 秒でステージを `screencapture -l<winid>`。
  比較は scratchpad の `imgdiff.py`(PIL 無し、`sips` で BMP 化)。基準は平均 2/255。草の揺れとタイマーで 0.5 程度は出る
- パスを書くときの契約は `IRenderPass.h` の冒頭コメント。Scene に描くパスは自分で RT をバインドする(前のパスに依存しない)
- 掲示板のキーは `MakePassKey("...")`(FNV-1a)。`aqHash32` ではない
- `HiZPass::Setup` 失敗はパイプライン全体の失敗(旧実装は Hi-Z だけ無効化)。環境で弾くなら `IsSupported` 側で
- 実装の委譲はユーザー指示で **Sonnet**(feature-dev-cycle の既定 Opus から切替済み)

関連: [[usability-port-phase-status]] / [[mac-visual-verification]]
