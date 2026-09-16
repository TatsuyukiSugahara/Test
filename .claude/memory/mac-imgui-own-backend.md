---
name: mac-imgui-own-backend
description: Mac の ImGui は imgui_impl_osx ではなく自前の MacImGui。io.AppFocusLost は false だけ送ると入力が永久に死ぬ
metadata: 
  node_type: memory
  type: project
  originSessionId: 67329cf3-7e0e-4cab-b8c5-681ec8644ca3
  modified: 2026-09-11T02:13:48.023Z
---

Mac の ImGui プラットフォームバックエンドは **`aqEngine/Platform/Mac/MacImGui.{h,mm}`(自前)**。
`imgui_impl_osx` は**同梱していない**(2026-09-11 / P4b で決定)。

**Why:** `PlatformMac::PumpEvents` → `DispatchInputEvent` が既に NSEvent を一次受けしている。
`imgui_impl_osx` は `NSView` へ `addLocalMonitorForEventsMatchingMask` でモニタを張るので
入り口が 2 本になる。自前なら 1 本のまま。Mac のフォントアトラスは ASCII のみで
`imgui_impl_osx` の目玉である IME も使えない。

**非自明な落とし穴 — `io.AppFocusLost` は立ちっぱなしのフラグ**:
`imgui.cpp` の `UpdateInputEvents` 末尾は、このフラグが真の**間ずっと毎フレーム**
`ClearInputKeys()` / `ClearInputMouse()` を呼ぶ。つまり `AddFocusEvent(false)` を送って
**`true` を返さないと、以後 ImGui の入力が永久に捨てられる**。
`windowDidResignKey` と `windowDidBecomeKey` の**両方**から
`MacImGui::OnFocusChanged` を呼ぶこと。

**How to apply:** キー写像は `MacImGui.mm` の `KEY_MAP`(`kVK_*` → `ImGuiKey`)。
`aq::hid::CocoaInputSink` の `KEY_MAP` とは**別物**(写像先の enum が違うので共有しない)。
ゲーム入力との排他は `Application::Update` の `WantCapture*` → `InputManager::Suppress*`
一点だけで効くので、同じ NSEvent を ImGui とシンクの両方へ渡してよい。
クリップボードの口は 1.91.1 で `ImGuiIO` から `ImGuiPlatformIO::Platform_*ClipboardTextFn`
へ移っている。関連: [[mac-port-phase-status]] / [[mac-gui-app-activation]]
