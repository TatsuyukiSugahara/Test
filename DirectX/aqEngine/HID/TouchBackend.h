#pragma once

// ============================================================
//  タッチバックエンドの選択(PadBackend.h と同じ流儀)。
//    Win32 / UWP / Mac : タッチなし(Null)
//    Android           : AndroidTouchBackend(AInputEvent 由来)
//    iOS               : iOSTouchBackend(UIKit の touches*:withEvent: 由来)
// ============================================================

#if defined(AQ_PLATFORM_ANDROID)
#include "HID/Android/AndroidTouchBackend.h"

namespace aq
{
	namespace hid
	{
		// Android: PlatformAndroid が AInputEvent を AndroidInputSink へ流し、
		// バックエンドはそれを読むだけ(Mac の CocoaInputSink と同じ構造)。
		using DefaultTouchBackend = AndroidTouchBackend;
	}
}
#elif defined(AQ_PLATFORM_IOS)
#include "HID/iOS/iOSTouchBackend.h"

namespace aq
{
	namespace hid
	{
		// iOS: AqMetalView の touches*:withEvent: が iOSInputSink へ流し、
		// バックエンドはそれを読むだけ(Android と同じ構造。設計書/iOS移植設計.md §5.2)。
		using DefaultTouchBackend = iOSTouchBackend;
	}
}
#else
#include "HID/NullTouchBackend.h"

namespace aq
{
	namespace hid
	{
		// タッチを持たないプラットフォーム。常に「触れていない」を返す。
		using DefaultTouchBackend = NullTouchBackend;
	}
}
#endif
