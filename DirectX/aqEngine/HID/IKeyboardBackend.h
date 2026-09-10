#pragma once
#include <cstdint>
#include "Graphics/GraphicsTypes.h"   // NativeWindowHandle

namespace aq
{
	namespace hid
	{
		// キーボードのキー。値はバックエンド非依存(KeyboardState::keys のインデックスとして使う)。
		// DIK_ 等のプラットフォーム固有スキャンコードには依存しない。実際の対応付けは
		// 各バックエンド(DirectInputKeyboardBackend / CocoaKeyboardBackend)の変換表が持つ。
		enum class KeyBoardType : uint32_t
		{
			Left, Right, Up, Down,
			W, A, S, D, G,
			Space, Enter, Escape,
			Num1, Num2, Num3, Num4,
		};

		// キーボード状態。keys は KeyBoardType 順で、押下中は 0x80 が立つ(DirectInput 互換)。
		// 配列長は現行実装(DirectInput の 256 バイトバッファ)に合わせて 256 を保つ。
		struct KeyboardState
		{
			static constexpr uint32_t KEY_COUNT = 256;

			uint8_t keys[KEY_COUNT]{};   // KeyBoardType 順 (押下 = 0x80)
		};

		// キーボード入力のプラットフォーム抽象。
		// 生のデバイス取得だけを隠蔽し、トリガー/長押し等の判定ロジックは KeyBoard 側に残す。
		// 実装: DirectInputKeyboardBackend(Win32)、NullKeyboardBackend(UWP / Mac の P2〜P3)、
		//       将来 CocoaKeyboardBackend(Mac, P4)。
		class IKeyboardBackend
		{
		public:
			virtual ~IKeyboardBackend() = default;

			// デバイスを初期化する。window は協調レベル設定などに使う。成功で true。
			virtual bool Initialize(aq::graphics::NativeWindowHandle window) = 0;

			// キーボードをポーリングし、状態を out に書き込む(未取得なら全ゼロ)。
			virtual void Poll(KeyboardState& out) = 0;
		};
	}
}
