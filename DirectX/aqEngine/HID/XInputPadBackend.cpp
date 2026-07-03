#include "aq.h"
// XInput はデスクトップ向け。UWP(Xbox)ではパッドは Phase 4(GameInput)で対応するため空 TU。
#if !defined(AQ_PLATFORM_UWP)
#include "HID/XInputPadBackend.h"
#pragma comment(lib, "xinput.lib")

namespace aq
{
	namespace hid
	{
		// PadButton → XInput ボタンビット対応表（LT/RT はトリガー値で判定するため 0）
		static const WORD kXInputButtonMap[] =
		{
			XINPUT_GAMEPAD_A,
			XINPUT_GAMEPAD_B,
			XINPUT_GAMEPAD_X,
			XINPUT_GAMEPAD_Y,
			XINPUT_GAMEPAD_LEFT_SHOULDER,
			XINPUT_GAMEPAD_RIGHT_SHOULDER,
			0,    // LT: bLeftTrigger  > 128 で判定
			0,    // RT: bRightTrigger > 128 で判定
			XINPUT_GAMEPAD_DPAD_UP,
			XINPUT_GAMEPAD_DPAD_DOWN,
			XINPUT_GAMEPAD_DPAD_LEFT,
			XINPUT_GAMEPAD_DPAD_RIGHT,
			XINPUT_GAMEPAD_START,
			XINPUT_GAMEPAD_BACK,
			XINPUT_GAMEPAD_LEFT_THUMB,
			XINPUT_GAMEPAD_RIGHT_THUMB,
		};


		void XInputPadBackend::Poll(uint32_t index, PadState& out)
		{
			out = {};

			XINPUT_STATE state{};
			out.connected = (XInputGetState(index, &state) == ERROR_SUCCESS);
			if (!out.connected)
			{
				return;
			}

			for (uint32_t i = 0; i < PadState::BUTTON_COUNT; ++i)
			{
				out.buttons[i] = GetButtonState(state, static_cast<PadButton>(i));
			}

			out.axes[static_cast<uint32_t>(PadAxis::LX)] = NormalizeAxis(state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
			out.axes[static_cast<uint32_t>(PadAxis::LY)] = NormalizeAxis(state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
			out.axes[static_cast<uint32_t>(PadAxis::RX)] = NormalizeAxis(state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
			out.axes[static_cast<uint32_t>(PadAxis::RY)] = NormalizeAxis(state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
			out.axes[static_cast<uint32_t>(PadAxis::LTrigger)] = state.Gamepad.bLeftTrigger  / 255.0f;
			out.axes[static_cast<uint32_t>(PadAxis::RTrigger)] = state.Gamepad.bRightTrigger / 255.0f;
		}


		void XInputPadBackend::SetVibration(uint32_t index, float left, float right)
		{
			const float l = left  < 0.0f ? 0.0f : (left  > 1.0f ? 1.0f : left);
			const float r = right < 0.0f ? 0.0f : (right > 1.0f ? 1.0f : right);
			XINPUT_VIBRATION vib;
			vib.wLeftMotorSpeed  = static_cast<WORD>(l * 65535.0f);
			vib.wRightMotorSpeed = static_cast<WORD>(r * 65535.0f);
			XInputSetState(index, &vib);
		}


		bool XInputPadBackend::GetButtonState(const XINPUT_STATE& state, PadButton btn)
		{
			if (btn == PadButton::LT) return state.Gamepad.bLeftTrigger  > 128;
			if (btn == PadButton::RT) return state.Gamepad.bRightTrigger > 128;

			const uint32_t idx = static_cast<uint32_t>(btn);
			if (idx < ArraySize(kXInputButtonMap) && kXInputButtonMap[idx] != 0)
				return (state.Gamepad.wButtons & kXInputButtonMap[idx]) != 0;
			return false;
		}


		float XInputPadBackend::NormalizeAxis(SHORT value, SHORT deadZone)
		{
			if (value > -deadZone && value < deadZone) return 0.0f;
			const float fMax      = 32767.0f;
			const float fDeadZone = static_cast<float>(deadZone);
			const float fVal      = static_cast<float>(value);
			float result;
			if (value > 0)
				result = (fVal - fDeadZone) / (fMax - fDeadZone);
			else
				result = (fVal + fDeadZone) / (fMax - fDeadZone);
			// SHORT_MIN (-32768) 側でわずかに -1 を下回るため [-1, 1] にクランプ
			return result < -1.0f ? -1.0f : (result > 1.0f ? 1.0f : result);
		}
	}
}
#endif // !AQ_PLATFORM_UWP
