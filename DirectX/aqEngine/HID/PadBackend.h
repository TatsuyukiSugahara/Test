#pragma once

// ============================================================
//  パッドバックエンドの選択(SoundBackend.h と同じ流儀)。
//    Win32(デスクトップ)  : XInput + DualSense(HID 直読み)の合成
//    UWP(Xbox / PC-UWP)  : Windows.Gaming.Input(WinRTGamepadBackend)
//    Mac                   : GameController.framework(P2 で追加)
// ============================================================

#if defined(AQ_PLATFORM_WIN32)
#include "HID/Win32PadBackend.h"

namespace aq
{
	namespace hid
	{
		// Win32 / デスクトップ: XInput 優先 + 空きスロットへ DualSense。
		// IPadBackend 抽象により Pad/InputManager 側は無改修で切替できる。
		using DefaultPadBackend = Win32PadBackend;
	}
}
#elif defined(AQ_PLATFORM_UWP)
#include "HID/WinRTGamepadBackend.h"

namespace aq
{
	namespace hid
	{
		// UWP(Xbox / PC-UWP): Windows.Gaming.Input によるゲームパッド。
		using DefaultPadBackend = WinRTGamepadBackend;
	}
}
#else
// Mac(GameControllerPadBackend)は P2 で追加する。
#error "DefaultPadBackend: 未対応のプラットフォームです"
#endif
