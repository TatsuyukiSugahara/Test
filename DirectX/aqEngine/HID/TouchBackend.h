#pragma once

// ============================================================
//  タッチバックエンドの選択(PadBackend.h と同じ流儀)。
//    Win32 / UWP / Mac : タッチなし(Null)
//    Android           : AndroidTouchBackend(AInputEvent 由来)
//    iOS               : タッチなし(Null)。P0 の骨格なので下の #else へ落ちる。
//                        TODO(P3): iOSTouchBackend を足してここへ分岐を書く
//                        (AqMetalView の touchesBegan/Moved/Ended/Cancelled が
//                        入力シンクへ流し、バックエンドはそれを読むだけ。
//                        Android / Mac と同じ構造。設計書/iOS移植設計.md §5.2)
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
