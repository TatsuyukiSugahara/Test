#include "aq.h"
// UWP / Mac では DirectInput が使えないため空 TU。
#if defined(AQ_PLATFORM_WIN32)
#include "HID/Win32/DirectInputMouseBackend.h"


namespace aq
{
	namespace hid
	{
		DirectInputMouseBackend::~DirectInputMouseBackend()
		{
			if (device_)
			{
				device_->Unacquire();
				device_->Release();
				device_ = nullptr;
			}
			if (input_)
			{
				input_->Release();
				input_ = nullptr;
			}
		}


		bool DirectInputMouseBackend::Initialize(aq::graphics::NativeWindowHandle window)
		{
			window_ = static_cast<HWND>(window.handle);

			if (FAILED(DirectInput8Create(
				GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
				IID_IDirectInput8, reinterpret_cast<VOID**>(&input_), nullptr)))
				return false;

			if (FAILED(input_->CreateDevice(GUID_SysMouse, &device_, nullptr)))
				return false;
			if (FAILED(device_->SetDataFormat(&c_dfDIMouse2)))
				return false;

			if (FAILED(device_->SetCooperativeLevel(window_, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND)))
				return false;

			DIPROPDWORD diprop;
			diprop.diph.dwSize       = sizeof(DIPROPDWORD);
			diprop.diph.dwHeaderSize = sizeof(DIPROPHEADER);
			diprop.diph.dwObj        = 0;
			diprop.diph.dwHow        = DIPH_DEVICE;
			diprop.dwData            = DIPROPAXISMODE_REL;
			if (FAILED(device_->SetProperty(DIPROP_AXISMODE, &diprop.diph)))
				return false;

			device_->Acquire();
			return true;
		}


		void DirectInputMouseBackend::Poll(MouseState& out)
		{
			out = {};

			if (device_)
			{
				// 取得に失敗したらフォーカス喪失とみなして再 Acquire し、
				// それでも駄目なら入力なし(全ゼロ)として扱う。
				DIMOUSESTATE2 state;
				aq::memory::Clear(&state, sizeof(DIMOUSESTATE2));

				if (FAILED(device_->GetDeviceState(sizeof(DIMOUSESTATE2), &state)))
				{
					device_->Acquire();
					if (FAILED(device_->GetDeviceState(sizeof(DIMOUSESTATE2), &state)))
						aq::memory::Clear(&state, sizeof(DIMOUSESTATE2));
				}

				out.dx    = static_cast<int32_t>(state.lX);
				out.dy    = static_cast<int32_t>(state.lY);
				out.wheel = static_cast<int32_t>(state.lZ);
				for (uint32_t i = 0; i < MouseState::BUTTON_COUNT; ++i)
				{
					out.buttons[i] = state.rgbButtons[i];
				}
			}

			// カーソル位置は DirectInput の相対軸では取れないため、デバイス状態とは
			// 独立にウィンドウのクライアント座標として取得する。
			POINT pos;
			::GetCursorPos(&pos);
			ScreenToClient(window_, &pos);
			out.cursorX = static_cast<float>(pos.x);
			out.cursorY = static_cast<float>(pos.y);
		}
	}
}
#endif // AQ_PLATFORM_WIN32
