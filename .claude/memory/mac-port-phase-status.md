---
name: mac-port-phase-status
description: Mac 移植の到達点。道A(Vulkan)も道B(ネイティブ Metal)も動く。残るは .app 配布と Windows 回帰
metadata: 
  node_type: memory
  type: project
  originSessionId: 67329cf3-7e0e-4cab-b8c5-681ec8644ca3
  modified: 2026-09-11T06:27:25.390Z
---

一次資料は `DirectX/設計書/Mac移植設計.md` 冒頭の到達点と
`DirectX/設計書/MetalBackend設計.md` の §12。

- **道A(Vulkan + MoltenVK)**: P0〜P4b 完了(2026-09-10〜11)。
  タイトル〜STAGE CLEAR まで通しでプレイでき、ImGui の入力も繋がっている
- **道B(ネイティブ Metal)**: **P0〜P6 完了**(2026-09-11、コミット `21cb46d`〜`7036ee5`)。
  タイトル〜ステージが Vulkan と**同じ見た目**(画素比較で平均差 1.8/255)、
  compute・シャドウ・GPU 駆動カリング・ImGui まで動作。Validation エラー 0
- **P5(`.app` 配布)も完了**。`aqBundleApp` ターゲットで Assets と(Vulkan なら)
  ランタイムを同梱し、ソースツリー外へコピーして環境変数なしで起動できる
- **残り**: Windows の回帰確認([[windows-regression-pending]])
- **実機未確認**: パッド(コントローラが無い)、サウンドの耳での確認

ビルドは 2 構成が併存する:
- `macos-ninja` … Vulkan(既定)
- `macos-ninja-metal` … ネイティブ Metal。**Metal でも `VULKAN_SDK` が要る**
  (描画には使わないが `dxc` と `spirv-cross` の入手元)

**`aqBundleApp` の副作用に注意**: 一度実行すると `.app` に `Contents/Resources` が残り、
`MacMain.mm` がそこへ chdir する。以降の**開発実行が古い同梱アセットを見る**ようになり、
シェーダを足しても「見つからない」で落ちる(実際に踏んだ)。当面はバンドルの
`Contents/Resources` を消して回避する。**根本対処はパッケージ出力を `bin/` の `.app` とは
別ディレクトリに出すこと**(未着手)。

**How to apply:** 続きをやるときは各設計書の §12 / 到達点 → オープン課題 → 該当フェーズの
評価欄、の順に読む。実機の見た目確認は [[mac-visual-verification]]。ビルドは [[mac-build-environment]]。
Metal 固有の罠は [[metal-backend-gotchas]]。宿題は [[windows-regression-pending]]。
