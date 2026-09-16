---
name: mac-visual-verification
description: Mac で描画結果を自動確認するときの手順 — ウィンドウ ID 指定の撮影と CGEvent による合成入力
metadata: 
  node_type: memory
  type: project
  originSessionId: 67329cf3-7e0e-4cab-b8c5-681ec8644ca3
  modified: 2026-09-11T08:20:33.465Z
---

Mac で「実際にどう描かれたか」を確認するときの手順。2026-09-11 の Metal 移植と
スカイキューブの検証で固めた。

**撮影は領域指定ではなくウィンドウ ID 指定にする。**
```
screencapture -x -o -l<windowid> out.png
```
**他アプリ(通知など)に覆われても中身が撮れる**。領域指定(`-R`)だと前面のアプリが
写り込み、他人のチャット画面などを撮ってしまう事故も起きる。
ウィンドウ ID は `CGWindowListCopyWindowInfo` で pid から引く小さなツールを書けばよい。

**合成入力は CGEvent を直接投げる。**
`osascript` の `System Events` の `click at` は**アクセシビリティ経由**なので、
自前描画のアプリ(ImGui やゲーム画面)には実クリックが届かない。
`CGEventCreateMouseEvent` / `CGEventCreateKeyboardEvent` + `CGEventPost` を使う。
キーコードは Carbon の `kVK_*`(W=13 / Space=49 / F1=122 / Escape=53)。

**落とし穴:**
- **スクリーンショットがフレームを追い越す。** キーを送った直後に撮ると変化前が写る。
  1 秒程度待つこと(F1 のトグルで一度誤診した)。
- **他アプリにフォーカスを奪われるとキーがそちらへ行く。** 撮影は覆われても平気だが、
  入力は前面でないと届かない。送信直前に前面化し、長い待ちを挟まない。
- **Metal API Validation は既定で無効。** `METAL_DEVICE_WRAPPER_TYPE=1` を付ける
  ([[metal-backend-gotchas]])。

**起動は「生バイナリ + cwd=DirectX/Game」。** `build/macos-ninja-metal/bin/Release/Game.app` は
`aqBundleApp` を通していない限り **Assets を同梱していない**ので、`open -n Game.app` すると
アセットが 1 つも見つからず**真っ黒なウィンドウ**になる(エラーも出ないので気づきにくい)。
```
cd DirectX/Game && nohup <build>/bin/Release/Game.app/Contents/MacOS/Game &
```
で相対パスの Assets が解決する。逆に同梱済みの `.app` を使うときの罠は
[[mac-port-phase-status]] の `aqBundleApp` の項。

**コース途中を見たいときは `spawn.distance` を一時的に書き換える。** AquaDash は最高速でも
ループ 2 まで 30 秒近く走る必要があり、合成入力で押しっぱなしにするより、
`Stage01.stage.json` の `spawn.distance` を目的地の少し手前(例: 2200)にして起動し、
確認後に戻すほうが速くて確実。ロード時ベイク(地形・草・ミニマップ)も正しくやり直される。

**キーの「押しっぱなし」は合成できない。** `evt key <code> down` を 1 回投げても、
ゲーム側の `IsPressed` 系は反応しない(トリガー判定の Space は 1 回で通る)。
keyDown を**送り続ける**必要がある。

**連打は 1 プロセスから行うこと。** シェルのループで `evt` をプロセス起動して連打する方式は
取りこぼしが多く、走行系の確認がほとんど成立しない(2026-09-12 に何度も空振りした)。
**`~/.claude/tools/evthold <keycode> <秒> [間隔ms]`**(単一プロセスから `usleep` 間隔で
keyDown を送り続け、最後に keyUp)に替えてから安定した。ツール一式は
`~/.claude/tools/`(`evt` / `evthold` / `evthold.c` / `winid`)に置いてある。
```
osascript -e 'tell application "System Events" to set frontmost of (first process whose unix id is PID) to true'
sleep 2; ~/.claude/tools/evthold 13 8 &      # W を 8 秒押しっぱなし
```
**ただし「押した瞬間」を狙う確認は依然として難しい。** 1 秒未満の窓
(AquaDash のトリック失敗判定など)は合成入力では安定して再現できない。

**走り切らないと見られない画面は、データ側で近道する。** `spawn.distance` を一時的に
動かすのが最も速い。ゴール直後(`goal.distance` + 1)に置けば入力なしでリザルトへ飛べるし、
コース途中のギミックの手前(50m 程度)に置けば短い走行で到達できる。確認後に必ず戻すこと。

**見た目の比較は画素で数値化する。** 「同じに見える」で終わらせず、同じ領域を撮って
平均差を出す。Metal と Vulkan の比較では平均差 1.8/255 まで詰められた。
**アニメーションするシーンは 2 枚が別フレームになる**ので完全一致にはならない点に注意。
