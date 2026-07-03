#include "aq.h"
// UWP(Xbox)専用。Windows.Gaming.Input によるゲームパッド入力。
// デスクトップ構成では空 TU(XInputPadBackend を使う)。
#if defined(AQ_PLATFORM_UWP)
#include "HID/WinRTGamepadBackend.h"
// C3779 回避: IVectorView<T>::Size()/GetAt() の consume 定義(auto 戻り)を先に取り込む。
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <algorithm>
#include <cmath>

namespace aq
{
	namespace hid
	{
		namespace
		{
			using namespace winrt::Windows::Gaming::Input;

			// スティックのデッドゾーン適用 + 再スケール([-1,1] を保つ)。
			// XInput 既定(約 0.24)に合わせて操作感を揃える。
			float ApplyDeadzone(double v, double dz)
			{
				const double a = std::abs(v);
				if (a <= dz) return 0.0f;
				const double s = (a - dz) / (1.0 - dz);
				return static_cast<float>(v < 0.0 ? -s : s);
			}

			bool Has(GamepadButtons buttons, GamepadButtons flag)
			{
				return (buttons & flag) != GamepadButtons::None;
			}
		}


		void WinRTGamepadBackend::Poll(uint32_t index, PadState& out)
		{
			out = PadState{};

			auto pads = Gamepad::Gamepads();
			if (index >= pads.Size())
			{
				out.connected = false;
				return;
			}

			const Gamepad pad = pads.GetAt(index);
			const GamepadReading r = pad.GetCurrentReading();
			out.connected = true;

			using B = GamepadButtons;
			auto set = [&](PadButton b, bool v) { out.buttons[static_cast<uint32_t>(b)] = v; };
			set(PadButton::A,      Has(r.Buttons, B::A));
			set(PadButton::B,      Has(r.Buttons, B::B));
			set(PadButton::X,      Has(r.Buttons, B::X));
			set(PadButton::Y,      Has(r.Buttons, B::Y));
			set(PadButton::LB,     Has(r.Buttons, B::LeftShoulder));
			set(PadButton::RB,     Has(r.Buttons, B::RightShoulder));
			set(PadButton::DUp,    Has(r.Buttons, B::DPadUp));
			set(PadButton::DDown,  Has(r.Buttons, B::DPadDown));
			set(PadButton::DLeft,  Has(r.Buttons, B::DPadLeft));
			set(PadButton::DRight, Has(r.Buttons, B::DPadRight));
			set(PadButton::Start,  Has(r.Buttons, B::Menu));
			set(PadButton::Back,   Has(r.Buttons, B::View));
			set(PadButton::LStick, Has(r.Buttons, B::LeftThumbstick));
			set(PadButton::RStick, Has(r.Buttons, B::RightThumbstick));

			constexpr double kStickDeadzone = 0.24;  // XInput 既定相当
			auto axis = [&](PadAxis a, float v) { out.axes[static_cast<uint32_t>(a)] = v; };
			axis(PadAxis::LX, ApplyDeadzone(r.LeftThumbstickX,  kStickDeadzone));
			axis(PadAxis::LY, ApplyDeadzone(r.LeftThumbstickY,  kStickDeadzone));
			axis(PadAxis::RX, ApplyDeadzone(r.RightThumbstickX, kStickDeadzone));
			axis(PadAxis::RY, ApplyDeadzone(r.RightThumbstickY, kStickDeadzone));
			axis(PadAxis::LTrigger, static_cast<float>(r.LeftTrigger));   // [0,1]
			axis(PadAxis::RTrigger, static_cast<float>(r.RightTrigger));

			// トリガーをボタンとしても判定(閾値 0.5)
			set(PadButton::LT, r.LeftTrigger  > 0.5);
			set(PadButton::RT, r.RightTrigger > 0.5);
		}


		void WinRTGamepadBackend::SetVibration(uint32_t index, float left, float right)
		{
			auto pads = Gamepad::Gamepads();
			if (index >= pads.Size()) return;

			const Gamepad pad = pads.GetAt(index);
			GamepadVibration v{};
			v.LeftMotor  = std::clamp(left,  0.0f, 1.0f);
			v.RightMotor = std::clamp(right, 0.0f, 1.0f);
			pad.Vibration(v);
		}
	}
}
#endif // AQ_PLATFORM_UWP
