#pragma once
#include "HID/IPadBackend.h"

namespace aq
{
	namespace hid
	{
		// UWP(Xbox / PC-UWP)向けパッドバックエンド。
		// Windows.Gaming.Input.Gamepad(WinRT・UWP 標準のゲームパッド API)を使う。
		// ※GDK の GameInput とは別物。UWP アプリコンテナで利用可能。
		// XInput は UWP で xinput.lib 非互換のことがあるため、こちらを使う。
		class WinRTGamepadBackend : public IPadBackend
		{
		public:
			void Poll(uint32_t index, PadState& out) override;
			void SetVibration(uint32_t index, float left, float right) override;
		};
	}
}
