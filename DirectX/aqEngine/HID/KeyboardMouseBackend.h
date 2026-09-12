#pragma once

// ============================================================
//  キーボード / マウスバックエンドの選択(PadBackend.h と同じ流儀)。
//    Win32(デスクトップ)  : DirectInput(DirectInputKeyboardBackend / DirectInputMouseBackend)
//    UWP(Xbox / PC-UWP)  : 入力なし(Null)。実入力は Phase 4 の GameInput で対応
//    Mac                   : 入力なし(Null)。Cocoa 実装(CocoaKeyboardBackend /
//                            CocoaMouseBackend)は P4 で追加する
//    Android               : 入力なし(Null)。物理キーボード/マウスは対象外で、
//                            操作はタッチとパッドが担う(設計書/Android移植設計.md)
// ============================================================

#if defined(AQ_PLATFORM_WIN32)
#include "HID/Win32/DirectInputKeyboardBackend.h"
#include "HID/Win32/DirectInputMouseBackend.h"

namespace aq
{
	namespace hid
	{
		// Win32 / デスクトップ: DirectInput によるキーボード / マウス。
		// IKeyboardBackend / IMouseBackend 抽象により KeyBoard/Mouse 側は無改修で切替できる。
		using DefaultKeyboardBackend = DirectInputKeyboardBackend;
		using DefaultMouseBackend    = DirectInputMouseBackend;
	}
}
#elif defined(AQ_PLATFORM_UWP)
#include "HID/NullKeyboardBackend.h"
#include "HID/NullMouseBackend.h"

namespace aq
{
	namespace hid
	{
		// UWP(Xbox / PC-UWP): DirectInput が使えないため入力なし。
		using DefaultKeyboardBackend = NullKeyboardBackend;
		using DefaultMouseBackend    = NullMouseBackend;
	}
}
#elif defined(AQ_PLATFORM_MAC)
#include "HID/Mac/CocoaKeyboardBackend.h"
#include "HID/Mac/CocoaMouseBackend.h"

namespace aq
{
	namespace hid
	{
		// Mac: NSEvent を PlatformMac::PumpEvents が CocoaInputSink へ流し、
		// バックエンドはそれを読むだけ(設計書/Mac移植設計.md §3.2)。
		using DefaultKeyboardBackend = CocoaKeyboardBackend;
		using DefaultMouseBackend    = CocoaMouseBackend;
	}
}
#elif defined(AQ_PLATFORM_ANDROID)
#include "HID/NullKeyboardBackend.h"
#include "HID/NullMouseBackend.h"

namespace aq
{
	namespace hid
	{
		// Android: 物理キーボード / マウスは想定しない。UI のタップは ITouchBackend、
		// ゲーム操作は DefaultPadBackend(仮想パッド or 物理コントローラ)が担う。
		// Null のままで Input 側は無改修で成立する。
		using DefaultKeyboardBackend = NullKeyboardBackend;
		using DefaultMouseBackend    = NullMouseBackend;
	}
}
#else
#error "DefaultKeyboardBackend / DefaultMouseBackend: 未対応のプラットフォームです"
#endif
