#pragma once

// ============================================================
//  キーボード / マウスバックエンドの選択(PadBackend.h と同じ流儀)。
//    Win32(デスクトップ)  : DirectInput(DirectInputKeyboardBackend / DirectInputMouseBackend)
//    UWP(Xbox / PC-UWP)  : 入力なし(Null)。実入力は Phase 4 の GameInput で対応
//    Mac                   : Cocoa 実装(CocoaKeyboardBackend / CocoaMouseBackend)
//    Android               : キーボードは Null。マウスは TouchMouseBackend(タッチをポインタとして
//                            供給する。物理マウスを想定するという意味ではない)
//    iOS                   : Android と同じ(キーボードは Null / マウスは TouchMouseBackend)
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
#include "HID/TouchMouseBackend.h"

namespace aq
{
	namespace hid
	{
		// Android: 物理キーボードは想定しないので Null。
		//
		// マウスは TouchMouseBackend を入れる。**物理マウスを想定するという意味ではなく**、
		// タッチを既存のポインタ経路(UIInputSystem / ImGui)へ載せるための合成である。
		// こうしておくと UI 側は無改修で、ImGui の抑制(SuppressMouse)もそのまま効く
		// (設計書/Android移植設計.md §P7)。ゲーム操作は DefaultPadBackend(仮想パッド or
		// 物理コントローラ)が引き続き担う。
		using DefaultKeyboardBackend = NullKeyboardBackend;
		using DefaultMouseBackend    = TouchMouseBackend;
	}
}
#elif defined(AQ_PLATFORM_IOS)
#include "HID/NullKeyboardBackend.h"
#include "HID/TouchMouseBackend.h"

namespace aq
{
	namespace hid
	{
		// iOS: ハードウェアキーボードは想定しないので Null(設計書/iOS移植設計.md §5.4)。
		//
		// マウスは Android と同じく TouchMouseBackend を入れる。**物理マウスを想定する
		// という意味ではなく**、iOSTouchBackend が取り込んだタッチを既存のポインタ経路
		// (UIInputSystem / ImGui)へ載せるための合成である。ゲーム操作は
		// DefaultPadBackend(仮想パッド or 物理コントローラ)が引き続き担う。
		using DefaultKeyboardBackend = NullKeyboardBackend;
		using DefaultMouseBackend    = TouchMouseBackend;
	}
}
#else
#error "DefaultKeyboardBackend / DefaultMouseBackend: 未対応のプラットフォームです"
#endif


// ============================================================
//  マウスバックエンドの生成(PadBackend.h の CreateDefaultPadBackend と同じ流儀)。
//  Android / iOS だけはタッチから合成するため取り込み済みの TouchState を要る。
//  呼び出し側(InputManager)に #if を持ち込まないため、組み立てをここへ寄せる。
// ============================================================
#include <memory>
#include "HID/ITouchBackend.h"

namespace aq
{
	namespace hid
	{
#if defined(AQ_PLATFORM_ANDROID) || defined(AQ_PLATFORM_IOS)
		inline std::unique_ptr<IMouseBackend> CreateDefaultMouseBackend(const TouchState* pointerTouch)
		{
			// 渡すのは「パッドが使っていない指」だけの集合。仮想パッドのスティックを
			// 倒しながら裏の UI を誤クリックしないよう、InputManager が選り分けている。
			return std::make_unique<TouchMouseBackend>(pointerTouch);
		}
#else
		inline std::unique_ptr<IMouseBackend> CreateDefaultMouseBackend(const TouchState* /*pointerTouch*/)
		{
			// タッチを持たないプラットフォームは実デバイスをそのまま使う。
			return std::make_unique<DefaultMouseBackend>();
		}
#endif
	}
}
