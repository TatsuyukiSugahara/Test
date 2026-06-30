#pragma once

// ============================================================
//  パッドバックエンドの選択(SoundBackend.h と同じ流儀)。
//    Win32 / Xbox(UWP 道A) : XInput(XInput 1.4 は UWP でも利用可)
//    将来                  : GameInput / Windows.Gaming.Input(Phase 4)
// ============================================================

#include "HID/XInputPadBackend.h"

namespace aq
{
	namespace hid
	{
		// Phase 4 で UWP 向けに GameInputPadBackend へ差し替え可能。
		// IPadBackend 抽象により Pad/InputManager 側は無改修で移行できる。
		using DefaultPadBackend = XInputPadBackend;
	}
}
