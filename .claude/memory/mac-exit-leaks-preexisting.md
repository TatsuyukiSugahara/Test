---
name: mac-exit-leaks-preexisting
description: Mac の終了時リーク約 7100 件は Android 移植より前から在る既存の問題。回帰と誤診しないこと
metadata:
  type: project
---

Debug ビルドの Mac は終了時に **7117 件 / 822KB** のリークを報告する
(`[MemoryTracker] ========== N leak(s) detected ==========`)。

**これは回帰ではない。**2026-09-14 に worktree で移植前のコミット `d1e7f6a` を
ビルドして実測したところ **7070 件 / 921,731 バイト**あった。Android 移植の
`ShutdownMemory()` 導入(コミット `fc1f2b0`)は報告位置を正しくしただけで、
リークそのものを増やしてはいない。

**Android は `No leaks detected` になる。**つまりプラットフォーム差ではなく
**バックエンド差(Metal 側の解放漏れ)の疑いが濃い**。第一の容疑は
[[resource-data-void-ptr]](delete する側が static_cast しないとデストラクタが
走らない構造)。

**How to apply:** Mac で大量のリーク報告を見ても「今回の変更で壊した」と早合点しない。
比較するなら worktree で移植前のコミットを立てて件数を突き合わせる
(`git worktree add <dir> <commit>` → `cmake --preset macos-ninja-metal` →
Debug ビルド → 起動して閉じるボタンで終了)。
**Esc では終了しない。閉じるボタンを押す**必要があり、合成入力なら
`osascript -e 'tell application "System Events" to tell (first process whose unix id is PID) to click button 1 of window 1'`
(事前に `set frontmost` しておかないと window が取れない)。
調査そのものは iOS / Android 移植とは別の `<Engine>` 作業として切る。
