#pragma once
#include <Xinput.h>
#include "HID/IPadBackend.h"

namespace aq
{
	namespace hid
	{
		// XInput によるパッドバックエンド(Win32)。
		// 旧 Pad が直接持っていた XInput 依存(状態取得・ボタン対応表・デッドゾーン正規化・振動)を移設。
		class XInputPadBackend : public IPadBackend
		{
		public:
			void Poll(uint32_t index, PadState& out) override;
			void SetVibration(uint32_t index, float left, float right) override;

		private:
			static bool  GetButtonState(const XINPUT_STATE& state, PadButton btn);
			static float NormalizeAxis (SHORT value, SHORT deadZone);
		};
	}
}
