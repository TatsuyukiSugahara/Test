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
#include "HID/Android/AndroidPadBackend.h"
#include "HID/CompositePadBackend.h"
#include "HID/VirtualPadBackend.h"

namespace aq
{
	namespace hid
	{
		// Android: 物理コントローラと仮想パッド(タッチ)を束ねて 1 つのパッドに見せる。
		// 上位(ActionMap / ゲーム側)は入力ソースを区別しない。
		// 組み立てには TouchState が要るので、生成は下の CreateDefaultPadBackend で行う。
		using DefaultPadBackend = CompositePadBackend;
	}
}
#else
#error "DefaultPadBackend: 未対応のプラットフォームです"
#endif


// ============================================================
//  既定パッドの生成
//
//  Android だけは「物理 + 仮想パッド」の組み立てが必要で、仮想パッドは
//  取り込み済みの TouchState を要求する。呼び出し側(InputManager)に
//  プラットフォーム分岐を持ち込まないため、生成をここへ寄せる。
// ============================================================
#include <memory>
#include "HID/ITouchBackend.h"

namespace aq
{
	namespace hid
	{
#if defined(AQ_PLATFORM_ANDROID)
		inline std::unique_ptr<IPadBackend> CreateDefaultPadBackend(const TouchState* touch)
		{
			// 追加した順に合成する。ボタンは OR、軸は絶対値の大きい方が採られるので、
			// 物理コントローラを繋いでいる間もタッチが邪魔をしない。
			auto composite = std::make_unique<CompositePadBackend>();
			composite->Add(std::make_unique<AndroidPadBackend>());
			composite->Add(std::make_unique<VirtualPadBackend>(touch));
			return composite;
		}
#else
		inline std::unique_ptr<IPadBackend> CreateDefaultPadBackend(const TouchState* /*touch*/)
		{
			// タッチを持たないプラットフォームは合成する相手がいないので素で生成する。
			return std::make_unique<DefaultPadBackend>();
		}
#endif
	}
}
