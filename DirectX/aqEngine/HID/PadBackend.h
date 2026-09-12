#pragma once

// ============================================================
//  パッドバックエンドの選択(SoundBackend.h と同じ流儀)。
//    Win32(デスクトップ)  : XInput + DualSense(HID 直読み)の合成
//    UWP(Xbox / PC-UWP)  : Windows.Gaming.Input(WinRTGamepadBackend)
//    Mac                   : 入力なし(Null)。GameController.framework 実装
//                            (GameControllerPadBackend)は P4 で追加する
//    Android               : 入力なし(Null)。仮想パッド(タッチ)と物理コントローラを
//                            同一視する実装は P4 で追加する
//                            (設計書/Android移植設計.md)
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
#elif defined(AQ_PLATFORM_MAC)
#include "HID/Mac/GameControllerPadBackend.h"

namespace aq
{
	namespace hid
	{
		// Mac: GameController.framework。Xbox / DualShock / DualSense を OS が
		// 同じプロファイルへ正規化するので HID 直読みは要らない(設計書 §3.2)。
		using DefaultPadBackend = GameControllerPadBackend;
	}
}
#elif defined(AQ_PLATFORM_ANDROID)
#include "HID/NullPadBackend.h"

namespace aq
{
	namespace hid
	{
		// Android: P0 の時点では入力なし。P4 で入力ソースを問わない仮想パッド
		// (タッチの仮想スティック / 物理コントローラのどちらも同じ IPadBackend として
		//  見せる合成バックエンド)へ差し替える。ActionMap 側は無改修で切替できる。
		// TODO(P4): CompositePadBackend(仮想パッド + AndroidPadBackend)へ差し替える。
		using DefaultPadBackend = NullPadBackend;
	}
}
#else
#error "DefaultPadBackend: 未対応のプラットフォームです"
#endif
