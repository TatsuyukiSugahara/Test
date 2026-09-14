#pragma once
// iOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_IOS)
#include "HID/ITouchBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * UIKit 由来のタッチ入力(設計書/iOS移植設計.md §5.2)。
		 *
		 * 実体は `iOSInputSink` にあり、ここは読み出すだけ
		 * (Android の `AndroidTouchBackend` と同じ薄さ)。`[UITouch locationInView:]` の座標は
		 * ビュー左上原点のポイントで届き、`contentsScale = 1.0` 固定(§3.5)なので
		 * 1 ポイント = 1 ピクセル。`TouchPoint` の契約(クライアント左上原点・ピクセル)へ
		 * 変換は要らない。
		 *
		 * スレッド: `InputManager::Update` から**メインスレッド 1 本**で呼ばれる
		 * (フレーム駆動が `CADisplayLink`。`iOSInputSink` の説明を参照)。
		 */
		class iOSTouchBackend : public ITouchBackend
		{
		public:
			void Poll(TouchState& out) override;
		};
	}
}
#endif // AQ_PLATFORM_IOS
