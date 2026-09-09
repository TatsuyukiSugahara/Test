#include "aq.h"
// UWP / Mac では XInput / HID 直読みが使えないため空 TU。
#if defined(AQ_PLATFORM_WIN32)
#include "HID/Win32PadBackend.h"

namespace aq
{
	namespace hid
	{
		void Win32PadBackend::Poll(uint32_t index, PadState& out)
		{
			out = {};
			if (index >= SLOT_COUNT) { return; }

			// XInput を先に見る。同じスロットに XInput パッドが居るなら DualSense は使わない
			// (どちらが担当かを一意にするための単純規則)。
			xinput_.Poll(index, out);
			if (out.connected)
			{
				owners_[index] = Owner::XInput;
				return;
			}

			// DualSense 側は Poll の中で HID の読みポンプと出力送信も行うため、
			// 未接続スロットでも毎フレーム呼んで再列挙のきっかけを与える。
			dualSense_.Poll(index, out);
			owners_[index] = out.connected ? Owner::DualSense : Owner::None;
		}


		void Win32PadBackend::SetVibration(uint32_t index, float left, float right)
		{
			if (index >= SLOT_COUNT) { return; }

			switch (owners_[index])
			{
			case Owner::XInput:    xinput_.SetVibration   (index, left, right); break;
			case Owner::DualSense: dualSense_.SetVibration(index, left, right); break;
			default: break;
			}
		}


		void Win32PadBackend::SetTriggerResistance(uint32_t index, PadAxis trigger, float startPos, float strength)
		{
			if (index >= SLOT_COUNT) { return; }

			// XInput 側は既定の no-op。担当が居ないスロットでは何もしない。
			switch (owners_[index])
			{
			case Owner::XInput:    xinput_.SetTriggerResistance   (index, trigger, startPos, strength); break;
			case Owner::DualSense: dualSense_.SetTriggerResistance(index, trigger, startPos, strength); break;
			default: break;
			}
		}
	}
}
#endif // AQ_PLATFORM_WIN32
