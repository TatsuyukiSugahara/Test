#pragma once
// DirectInput は Win32 デスクトップ専用。UWP / Mac では NullMouseBackend が担当する。
#if defined(AQ_PLATFORM_WIN32)
#if !defined(DIRECTINPUT_VERSION)
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include "HID/IMouseBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * DirectInput によるマウスバックエンド(Win32)
		 * 旧 Mouse が直接持っていた DirectInput 依存(デバイス生成・相対軸モード設定・
		 * 状態取得・再 Acquire)と、カーソル座標の取得(::GetCursorPos + ScreenToClient)を移設。
		 */
		class DirectInputMouseBackend : public IMouseBackend
		{
		private:
			LPDIRECTINPUT8       input_  = nullptr;
			LPDIRECTINPUTDEVICE8 device_ = nullptr;

			/** カーソル座標をクライアント座標へ変換するためのウィンドウ */
			HWND                 window_ = nullptr;


		public:
			DirectInputMouseBackend() = default;
			~DirectInputMouseBackend() override;


		public:
			bool Initialize(aq::graphics::NativeWindowHandle window) override;
			void Poll(MouseState& out) override;
		};
	}
}
#endif // AQ_PLATFORM_WIN32
