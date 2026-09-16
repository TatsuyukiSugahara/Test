---
name: mac-gui-app-activation
description: Mac で「ウィンドウは出ているのにキーボードイベントが来ない」ときに最初に見る 2 箇所
metadata:
  type: project
---

自前イベントループ(`NSApp run` を使わず `nextEventMatchingMask` を回す)の Mac アプリで
**ウィンドウは表示されるのにキー入力が届かない**とき、原因はほぼこの 2 つ。
2026-09-10 の P2.5 で両方踏んだ。

1. **`.app` の `CFBundleIdentifier` が空**。CMake の `MACOSX_BUNDLE` が生成する既定
   Info.plist は識別子もアプリ名も空文字列になる。識別子が空のバンドルは macOS から
   通常のアプリとして扱われず、**ウィンドウがキーウィンドウにならない**ので
   キーイベントがアプリのキューに入らない。`MACOSX_BUNDLE_GUI_IDENTIFIER` を設定する。
2. **アプリがアクティブになっていない**。`makeKeyAndOrderFront:` だけでは足りず、
   `[NSApp activate]`(macOS 14 未満は `activateIgnoringOtherApps:`)が要る。

**How to apply:** `[NSApp isActive]` と `[window isKeyWindow]` を print して切り分ける。
**その際 stdout は必ず行バッファにするか正常終了させること** — `kill -9` で落とすと
バッファが失われ、「イベントが 1 件も来ていない」ように見える偽の結論になる(実際に一度
これで誤診した)。合成キー入力は `osascript -e 'tell application "System Events" to key code 49'`
(`keystroke " "` は別のキーコードになることがある)。押しっぱなしにしたいときは
`key down` / `key up` を分ける。関連: [[mac-port-phase-status]]
