---
name: usability-port-phase-status
description: 「学生でも使いやすいエンジン」改善の到達点 — P1〜P4 完了。未着手は P1-C2(Android/UWP 同梱)だけ。Windows 回帰は 2026-09-15 に実施済み
metadata: 
  node_type: memory
  type: project
  originSessionId: ed6fc3e7-bc21-4c7b-8dae-e929bb96c8db
  modified: 2026-09-16T01:50:57.244Z
---

`DirectX/設計書/使いやすさ改善設計.md` が一次資料(課題側は `使いやすさ課題.md`)。
前提は確定済み: 利用者 = **他の学生が実際にゲームを作る**、対象 = **全 5 プラットフォーム**。

到達点(2026-09-15 時点):

- **P1-A 完了** — パス解決を `Resource/AssetPath.{h,cpp}` へ一元化。重複 12 個を 1 個に。解決失敗は `[asset]` ログに候補一覧が出る
- **P1-B 完了** — `Game/Assets/Shader` → `aqEngine/Assets/Shader`、`SkyCube.dds` → `aqEngine/Assets/Sky/DefaultSkyCube.dds`。実機目視まで完了(2026-09-15 / Mac / Metal)
- **P1-C0 完了** — コンテンツ基点は番兵 `aqEngine/Assets` が実在するときだけ採用し、無ければ上方探索へ落とす(`beba187`)
- **P1-C1 完了**(`0a06b6a`)— `package_app.cmake` に `AQ_PKG_ENGINE_ASSETS_DIR` を足し、macOS / iOS のバンドルへエンジンアセットを同梱。macOS はソースツリー外で単体起動しステージまで確認済み
- **P1-C2 未着手** — Android(`package_apk.ps1`)と UWP(`GameUWP.vcxproj`)。どちらも確認に Windows 機が要る。**UWP の `MaterialDef.h` は既存同梱がそのまま効く**(移設後の深さが一致する)ので追加作業は不要
- **P2 完了** — P2-A: 既定バンク登録をエンジンへ(`65214b8`)。Sky の逃がしと GameFlow の遅延プリロードが同時に不要化。P2-B: `SetupStandardRenderers()` + `RendererPreset`(`f3413e8`)。**Game/Application/Application.cpp は 321 行 → 233 行**。P2-C(System 依存の宣言化)は**見送り**(可変長 `AddSystem<T,Deps...>` が既にあり、循環検出も揃っているため取り分が小さい)
- **P3 完了** — P3-A: ゲームルート名を `InitializeParameter::gameRootName` で可変に(`dbfca09`)。P3-B/C: `DirectX/Sample/`(箱 1 個・41 行)と `Sample/README.md`(3 歩の入門)(`d3d0ed2`)
- **P4-A 完了**(`eeb21e0`)— Metal の生成失敗を `metal::ReportCreationFailure()` で報告。**同じ文言は 1 度しか出さない**(既存の `MetalPipelineCache` のログが startup_timing.log を 19MB まで育てていた)
- **P4-B 完了**(`3fa26a2`)— `D3D11Shader` の CWD 差し替えを `ID3DInclude` 自前実装へ。確認中に `Shader::Load` の `static char` ソースバッファがワーカースレッド間で混ざる既存バグも直した。**`VulkanShader.cpp:240` に同じ CWD 差し替えが残っている**(DXC の `IDxcIncludeHandler` なので別作業。設計書に項目は無い)
- **Windows 回帰確認 済み**(2026-09-15)— D3D11 / D3D12 / Vulkan の 3 構成でビルド・起動。ステージまで目視したのは D3D11 だけ。`Sample` も Windows で箱が出るところまで確認(`a535155`)

**P4 の当初 4 項目のうち 3 つは着手時点で既に済んでいた。** 大文字小文字の候補提示は P1-A、
Metal の GPU 機能クエリは iOS 移植、バンク未登録のメッセージ化は P2-A。
**着手前に必ず現状を確認すること**(設計書の「既知の課題」も古くなっていた)。

**How to apply:** シェーダの `.spv` / `.metal` / `.metallib` は `.gitignore` 対象なので
**`git mv` では動かない**。`.fx` を移設したら 4 経路すべて再生成すること
(`macos-ninja` → `aqCompileSpv`、`macos-ninja-metal` / `ios-ninja` / `ios-xcode` → `aqCompileMsl`)。
`.metallib` が 61 でなく 62 個になるのは、`shader_entries.txt` に載らない手書きの
`FullscreenBlit.metal` が別経路で焼かれるため(欠陥ではない)。

**Android / UWP はパッケージするとまだ起動しない**(エンジンアセットが同梱されない)。
開発実行は P1-C0 の上方探索フォールバックで動く。Windows はソースツリー参照なので影響なし。
Mac / iOS は P1-C1 で解消済み。

**持ち越しの正本は設計書 §6(2026-09-16 新設)。** 10 件。レビューで新たに分かったのは
(a) FBX の 4 経路が候補全滅時に `LogUnresolvedAssetPath` を呼ばない、
(b) 圧縮音声(`CompressedDecoder` / ストリームの `Open`)は `AssetPath` を通さず CWD 相対のまま。
Mac の `.app` が `SetupBundleEnvironment` で CWD を `Contents/Resources/Game` へ移すのは (b) のため。

**2026-09-15 に Windows 機へ触れたのに P1-C2(Android / UWP のパッケージ同梱)は行わなかった。**
次に Windows 機に触れるときの最優先。`package_apk.ps1` / `GameUWP.vcxproj` に `aqEngine/Assets` の参照はまだ無い(2026-09-16 確認)。

**ドキュメントに載せるコードは通しで動かしてから載せる。** P3-C の初稿は
存在しない `EntityContext::ForEach` を書き、クォータニオンの `rotation` を
オイラー角として扱っていた。どちらも仮組みして初めて分かった。

関連: [[mac-build-environment]] / [[ios-device-workflow]] / [[findprojectroot-contentroot-gap]]
