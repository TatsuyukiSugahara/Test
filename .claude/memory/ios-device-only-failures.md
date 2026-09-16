---
name: ios-device-only-failures
description: iOS 実機でしか出ない致命的な失敗 3 つ。シミュレータで全部通っていても実機では 1 フレームも出なかった
metadata:
  type: project
---

**「シミュレータで動いた」は実機の保証にまったくならない。** P0〜P5a をシミュレータで
全部通した状態で実機に入れたら、**最初の 1 フレームも出なかった**(2026-09-14)。
3 つとも SIGBUS で、どれも致命的だった。一次資料は `iOS移植設計.md` の §4.6 / §6 / P5b。

**1. 実行時 MSL コンパイル(`newLibraryWithSource:`)が使えない**
- 3 行の最小シェーダでも SIGBUS。**ソース内容とは無関係**。
- → `.metallib` の事前ビルドが**必須**。`xcodebuild -downloadComponent MetalToolchain`(688MB)が前提。
- spirv-cross は全シェーダのエントリを `main0` で出すので**1 本にまとめられない**。
  SDK ごとに別物なので `msl-ios/<sdk>/` に分ける。

**2. RemoteIO はインターリーブを受け付けない**
- `AudioUnitInitialize` の中の `AudioConverterNewWithOptions` で SIGBUS。
  **サンプルレートもチャンネル数も一致させても再現する。**
- → `kAudioFormatFlagIsNonInterleaved` を立て、コールバックで L/R へ配る。

**3. ★ エンジンのメモリトラッカーが Apple のライブラリを壊す**
- `Memory/GlobalNew.cpp` がグローバル `operator new` を差し替えており、
  **Apple の dyld はフラット名前空間なのでシステムフレームワークの確保もこれを通る**。
- Debug の `HeapAllocator` は `TrackHeader` を前置して**ずれたポインタを返す**ので、
  相手が `free()` した瞬間に壊れる。逆方向は `TRACK_MAGIC` で弾けるが**この方向は弾けない**。
- 実機では `AudioUnitInitialize` → `caulk` で `SIGBUS (BUS_ADRALN)`。
- **切り分け方**: Release では起きない / Debug でも `GlobalNew.cpp` を外すと起きない。
  → ヘッダが犯人と分かる。over-aligned 版 `operator new` を足しても直らない。
- → iOS では Debug でもヘッダを付けない(`HeapAllocator.h`)。
  **構造自体は残っているので Android / Windows でも同種の事故は起こりうる**(§8-14)。

**切り分けの道具**: 実機のクラッシュレポートは Mac へ同期されず `devicectl` にも取得手段が無い。
**`sigaltstack` + `SIGBUS`/`SIGSEGV` ハンドラで `backtrace_symbols_fd` を stderr へ出す**のが
いちばん速い(`iOSMain.mm` に一時的に仕込む)。`StartupMark` を挟んで二分探索するのも有効。

**How to apply:** 非 Windows 移植では「実機でだけ壊れる層」が必ずある前提で計画すること。
関連: [[ios-port-phase-status]] [[ios-device-workflow]] [[ios-simulator-device-gaps]]
