#pragma once

// ============================================================
//  パッドバックエンドの選択(SoundBackend.h と同じ流儀)。
//    Win32 / デスクトップ  : XInput
//    UWP(Xbox / PC-UWP)  : Windows.Gaming.Input(WinRTGamepadBackend)
// ============================================================

#if !defined(AQ_PLATFORM_UWP)
#include "HID/XInputPadBackend.h"

namespace aq
{
	namespace hid
	{
		// Win32 / デスクトップ: XInput。
		// IPadBackend 抽象により Pad/InputManager 側は無改修で切替できる。
		using DefaultPadBackend = XInputPadBackend;
	}
}
#else
#include "HID/WinRTGamepadBackend.h"

namespace aq
{
	namespace hid
	{
		// UWP(Xbox / PC-UWP): Windows.Gaming.Input によるゲームパッド。
		using DefaultPadBackend = WinRTGamepadBackend;
	}
}
#endif
