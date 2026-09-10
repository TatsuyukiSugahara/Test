#pragma once
// DirectInput は Win32 デスクトップ専用。UWP / Mac では NullKeyboardBackend が担当する。
#if defined(AQ_PLATFORM_WIN32)
#if !defined(DIRECTINPUT_VERSION)
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include "HID/IKeyboardBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * DirectInput によるキーボードバックエンド(Win32)
		 * 旧 KeyBoard が直接持っていた DirectInput 依存(デバイス生成・協調レベル設定・
		 * 状態取得・再 Acquire)を移設し、DIK → KeyBoardType の変換もここで行う。
		 */
		class DirectInputKeyboardBackend : public IKeyboardBackend
		{
		private:
			LPDIRECTINPUT8       input_  = nullptr;
			LPDIRECTINPUTDEVICE8 device_ = nullptr;


		public:
			DirectInputKeyboardBackend() = default;
			~DirectInputKeyboardBackend() override;


		public:
			bool Initialize(aq::graphics::NativeWindowHandle window) override;
			void Poll(KeyboardState& out) override;
		};
	}
}
#endif // AQ_PLATFORM_WIN32
