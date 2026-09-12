#pragma once
// Android 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_ANDROID)
#include "HID/ITouchBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * AInputEvent 由来のタッチ入力(設計書/Android移植設計.md の入力節)。
		 *
		 * 実体は `AndroidInputSink` にあり、ここは読み出すだけ
		 * (Mac の `CocoaMouseBackend` と同じ薄さ)。`AMotionEvent` の座標は
		 * ウィンドウ内ピクセルで届くので、`TouchPoint` の契約
		 * (クライアント左上原点・ピクセル)へ変換は要らない。
		 *
		 * スレッド: `InputManager::Update` から**ゲームスレッド 1 本**で呼ばれる。
		 */
		class AndroidTouchBackend : public ITouchBackend
		{
		public:
			void Poll(TouchState& out) override;
		};
	}
}
#endif // AQ_PLATFORM_ANDROID
