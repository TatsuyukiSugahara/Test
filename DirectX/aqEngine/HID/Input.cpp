#include "aq.h"
#include "Input.h"
#include "Engine.h"
#pragma comment(lib, "xinput.lib")


namespace aq
{
	namespace hid
	{
		// ==========================================
		// KeyBoard
		// ==========================================

		KeyBoard::~KeyBoard()
		{
			if (device_)
			{
				device_->Unacquire();
				device_->Release();
				device_ = nullptr;
			}
		}


		HRESULT KeyBoard::Initialize(LPDIRECTINPUT8 input)
		{
			if (FAILED(input->CreateDevice(GUID_SysKeyboard, &device_, nullptr)))
				return S_FALSE;
			if (FAILED(device_->SetDataFormat(&c_dfDIKeyboard)))
				return S_FALSE;
			if (FAILED(device_->SetCooperativeLevel(engine::Engine::Get().GetHWND(), DISCL_NONEXCLUSIVE | DISCL_FOREGROUND)))
				return S_FALSE;
			device_->Acquire();
			return S_OK;
		}


		void KeyBoard::Update(float dt)
		{
			aq::memory::Copy(old_, now_, sizeof(old_));

			HRESULT hr = device_->GetDeviceState(sizeof(now_), &now_);
			if (FAILED(hr))
			{
				device_->Acquire();
				if (FAILED(device_->GetDeviceState(sizeof(now_), &now_)))
					aq::memory::Clear(now_, sizeof(now_));
			}

			for (uint32_t i = 0; i < KEY_COUNT; ++i)
			{
				if (now_[i] & 0x80)
					holdTimers_[i] += dt;
				else
					holdTimers_[i] = 0.0f;
			}
		}


		bool KeyBoard::IsTriggered(KeyBoardType key) const
		{
			const uint32_t k = static_cast<uint32_t>(key);
			return !(old_[k] & 0x80) && (now_[k] & 0x80);
		}


		bool KeyBoard::IsPressed(KeyBoardType key) const
		{
			return (now_[static_cast<uint32_t>(key)] & 0x80) != 0;
		}


		bool KeyBoard::IsReleased(KeyBoardType key) const
		{
			const uint32_t k = static_cast<uint32_t>(key);
			return (old_[k] & 0x80) && !(now_[k] & 0x80);
		}


		bool KeyBoard::IsLongPress(KeyBoardType key, float thresholdSec) const
		{
			return holdTimers_[static_cast<uint32_t>(key)] >= thresholdSec;
		}


		// ==========================================
		// Mouse
		// ==========================================

		Mouse::~Mouse()
		{
			if (device_)
			{
				device_->Unacquire();
				device_->Release();
				device_ = nullptr;
			}
		}


		HRESULT Mouse::Initialize(LPDIRECTINPUT8 input)
		{
			if (FAILED(input->CreateDevice(GUID_SysMouse, &device_, nullptr)))
				return S_FALSE;
			if (FAILED(device_->SetDataFormat(&c_dfDIMouse2)))
				return S_FALSE;

			if (FAILED(device_->SetCooperativeLevel(engine::Engine::Get().GetHWND(), DISCL_NONEXCLUSIVE | DISCL_FOREGROUND)))
				return S_FALSE;

			DIPROPDWORD diprop;
			diprop.diph.dwSize       = sizeof(DIPROPDWORD);
			diprop.diph.dwHeaderSize = sizeof(DIPROPHEADER);
			diprop.diph.dwObj        = 0;
			diprop.diph.dwHow        = DIPH_DEVICE;
			diprop.dwData            = DIPROPAXISMODE_REL;
			if (FAILED(device_->SetProperty(DIPROP_AXISMODE, &diprop.diph)))
				return S_FALSE;

			device_->Acquire();
			return S_OK;
		}


		void Mouse::Update(float dt)
		{
			old_ = now_;
			aq::memory::Clear(&now_, sizeof(DIMOUSESTATE2));

			HRESULT hr = device_->GetDeviceState(sizeof(DIMOUSESTATE2), &now_);
			if (FAILED(hr))
			{
				device_->Acquire();
				if (FAILED(device_->GetDeviceState(sizeof(DIMOUSESTATE2), &now_)))
					aq::memory::Clear(&now_, sizeof(DIMOUSESTATE2));
			}

			for (uint32_t i = 0; i < 3; ++i)
			{
				if (now_.rgbButtons[i] & 0x80)
					holdTimers_[i] += dt;
				else
					holdTimers_[i] = 0.0f;
			}
		}


		bool Mouse::IsTriggered(MouseButton btn) const
		{
			const uint32_t i = static_cast<uint32_t>(btn);
			return !(old_.rgbButtons[i] & 0x80) && (now_.rgbButtons[i] & 0x80);
		}


		bool Mouse::IsPressed(MouseButton btn) const
		{
			return (now_.rgbButtons[static_cast<uint32_t>(btn)] & 0x80) != 0;
		}


		bool Mouse::IsReleased(MouseButton btn) const
		{
			const uint32_t i = static_cast<uint32_t>(btn);
			return (old_.rgbButtons[i] & 0x80) && !(now_.rgbButtons[i] & 0x80);
		}


		bool Mouse::IsLongPress(MouseButton btn, float thresholdSec) const
		{
			return holdTimers_[static_cast<uint32_t>(btn)] >= thresholdSec;
		}


		float Mouse::GetAxis(MouseAxis axis) const
		{
			switch (axis)
			{
			case MouseAxis::DeltaX:     return static_cast<float>(now_.lX);
			case MouseAxis::DeltaY:     return static_cast<float>(now_.lY);
			case MouseAxis::WheelDelta: return static_cast<float>(now_.lZ);
			default:                    return 0.0f;
			}
		}


		math::Vector2 Mouse::GetCursorPos() const
		{
			POINT pos;
			::GetCursorPos(&pos);
			ScreenToClient(engine::Engine::Get().GetHWND(), &pos);
			return math::Vector2(static_cast<float>(pos.x), static_cast<float>(pos.y));
		}


		// ==========================================
		// Pad
		// ==========================================

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


		void Pad::Update(float dt)
		{
			old_ = now_;
			connected_ = (XInputGetState(index_, &now_) == ERROR_SUCCESS);

			if (!connected_)
			{
				now_  = {};
				old_  = {};
				aq::memory::Clear(holdTimers_, sizeof(holdTimers_));
				return;
			}

			for (uint32_t i = 0; i < BTN_COUNT; ++i)
			{
				if (GetButtonState(now_, static_cast<PadButton>(i)))
					holdTimers_[i] += dt;
				else
					holdTimers_[i] = 0.0f;
			}
		}


		bool Pad::IsTriggered(PadButton btn) const
		{
			return !GetButtonState(old_, btn) && GetButtonState(now_, btn);
		}


		bool Pad::IsPressed(PadButton btn) const
		{
			return GetButtonState(now_, btn);
		}


		bool Pad::IsReleased(PadButton btn) const
		{
			return GetButtonState(old_, btn) && !GetButtonState(now_, btn);
		}


		bool Pad::IsLongPress(PadButton btn, float thresholdSec) const
		{
			return holdTimers_[static_cast<uint32_t>(btn)] >= thresholdSec;
		}


		float Pad::GetAxis(PadAxis axis) const
		{
			if (!connected_) return 0.0f;
			switch (axis)
			{
			case PadAxis::LX:       return NormalizeAxis(now_.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
			case PadAxis::LY:       return NormalizeAxis(now_.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
			case PadAxis::RX:       return NormalizeAxis(now_.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
			case PadAxis::RY:       return NormalizeAxis(now_.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
			case PadAxis::LTrigger: return now_.Gamepad.bLeftTrigger  / 255.0f;
			case PadAxis::RTrigger: return now_.Gamepad.bRightTrigger / 255.0f;
			default:                return 0.0f;
			}
		}


		bool Pad::GetButtonState(const XINPUT_STATE& state, PadButton btn) const
		{
			if (btn == PadButton::LT) return state.Gamepad.bLeftTrigger  > 128;
			if (btn == PadButton::RT) return state.Gamepad.bRightTrigger > 128;

			const uint32_t idx = static_cast<uint32_t>(btn);
			if (idx < ArraySize(kXInputButtonMap) && kXInputButtonMap[idx] != 0)
				return (state.Gamepad.wButtons & kXInputButtonMap[idx]) != 0;
			return false;
		}


		float Pad::NormalizeAxis(SHORT value, SHORT deadZone) const
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


		void Pad::Vibrate(float left, float right)
		{
			if (!connected_) return;
			const float l = left  < 0.0f ? 0.0f : (left  > 1.0f ? 1.0f : left);
			const float r = right < 0.0f ? 0.0f : (right > 1.0f ? 1.0f : right);
			XINPUT_VIBRATION vib;
			vib.wLeftMotorSpeed  = static_cast<WORD>(l * 65535.0f);
			vib.wRightMotorSpeed = static_cast<WORD>(r * 65535.0f);
			XInputSetState(index_, &vib);
		}


		// ==========================================
		// InputManager
		// ==========================================

		InputManager* InputManager::sInstance_ = nullptr;


		InputManager::InputManager()
			: lastTime_(Clock::now())
		{
			for (uint32_t i = 0; i < MAX_PAD_COUNT; ++i)
				pads_[i].SetIndex(i);
		}


		InputManager::~InputManager()
		{
			// デバイスを先に解放してから DirectInput オブジェクトを解放する
			keyBoard_.reset();
			mouse_.reset();
			if (input_)
			{
				input_->Release();
				input_ = nullptr;
			}
		}


		HRESULT InputManager::Setup()
		{
			if (FAILED(DirectInput8Create(
				GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
				IID_IDirectInput8, reinterpret_cast<VOID**>(&input_), nullptr)))
				return S_FALSE;

			keyBoard_ = std::make_unique<KeyBoard>();
			if (FAILED(keyBoard_->Initialize(input_))) return S_FALSE;

			mouse_ = std::make_unique<Mouse>();
			if (FAILED(mouse_->Initialize(input_))) return S_FALSE;

			return S_OK;
		}


		void InputManager::Update()
		{
			const auto  now = Clock::now();
			const float dt  = std::chrono::duration<float>(now - lastTime_).count();
			lastTime_ = now;

			if (keyBoard_) keyBoard_->Update(dt);
			if (mouse_)    mouse_->Update(dt);
			for (auto& pad : pads_) pad.Update(dt);
		}


		Pad* InputManager::GetPad(uint32_t index)
		{
			EngineAssert(index < MAX_PAD_COUNT);
			if (index >= MAX_PAD_COUNT) return nullptr;
			return &pads_[index];
		}


		void InputManager::Vibrate(uint32_t padIndex, float left, float right)
		{
			Pad* pad = GetPad(padIndex);
			if (pad) pad->Vibrate(left, right);
		}


		void InputManager::StopVibration(uint32_t padIndex)
		{
			Pad* pad = GetPad(padIndex);
			if (pad) pad->StopVibration();
		}


		// ==========================================
		// Free function wrappers
		// ==========================================

		bool IsKeyTriggered(KeyBoardType key)
		{
			if (InputManager::Get().IsKeyboardSuppressed()) return false;
			const auto* kb = InputManager::Get().GetKeyBoardPtr();
			return kb && kb->IsTriggered(key);
		}
		bool IsKeyPressed(KeyBoardType key)
		{
			if (InputManager::Get().IsKeyboardSuppressed()) return false;
			const auto* kb = InputManager::Get().GetKeyBoardPtr();
			return kb && kb->IsPressed(key);
		}
		bool IsKeyReleased(KeyBoardType key)
		{
			if (InputManager::Get().IsKeyboardSuppressed()) return false;
			const auto* kb = InputManager::Get().GetKeyBoardPtr();
			return kb && kb->IsReleased(key);
		}
		bool IsKeyLongPress(KeyBoardType key, float thresholdSec)
		{
			if (InputManager::Get().IsKeyboardSuppressed()) return false;
			const auto* kb = InputManager::Get().GetKeyBoardPtr();
			return kb && kb->IsLongPress(key, thresholdSec);
		}

		bool IsMouseTriggered(MouseButton btn)
		{
			if (InputManager::Get().IsMouseSuppressed()) return false;
			const auto* m = InputManager::Get().GetMousePtr();
			return m && m->IsTriggered(btn);
		}
		bool IsMousePressed(MouseButton btn)
		{
			if (InputManager::Get().IsMouseSuppressed()) return false;
			const auto* m = InputManager::Get().GetMousePtr();
			return m && m->IsPressed(btn);
		}
		bool IsMouseReleased(MouseButton btn)
		{
			if (InputManager::Get().IsMouseSuppressed()) return false;
			const auto* m = InputManager::Get().GetMousePtr();
			return m && m->IsReleased(btn);
		}
		bool IsMouseLongPress(MouseButton btn, float thresholdSec)
		{
			if (InputManager::Get().IsMouseSuppressed()) return false;
			const auto* m = InputManager::Get().GetMousePtr();
			return m && m->IsLongPress(btn, thresholdSec);
		}
		float GetMouseAxis(MouseAxis axis)
		{
			if (InputManager::Get().IsMouseSuppressed()) return 0.0f;
			const auto* m = InputManager::Get().GetMousePtr();
			return m ? m->GetAxis(axis) : 0.0f;
		}
		math::Vector2 GetMouseCursorPos()
		{
			if (InputManager::Get().IsMouseSuppressed()) return {};
			const auto* m = InputManager::Get().GetMousePtr();
			return m ? m->GetCursorPos() : math::Vector2{};
		}

		bool IsPadTriggered(uint32_t padIndex, PadButton btn)
		{
			const Pad* pad = InputManager::Get().GetPad(padIndex);
			return pad && pad->IsTriggered(btn);
		}
		bool IsPadPressed(uint32_t padIndex, PadButton btn)
		{
			const Pad* pad = InputManager::Get().GetPad(padIndex);
			return pad && pad->IsPressed(btn);
		}
		bool IsPadReleased(uint32_t padIndex, PadButton btn)
		{
			const Pad* pad = InputManager::Get().GetPad(padIndex);
			return pad && pad->IsReleased(btn);
		}
		bool IsPadLongPress(uint32_t padIndex, PadButton btn, float thresholdSec)
		{
			const Pad* pad = InputManager::Get().GetPad(padIndex);
			return pad && pad->IsLongPress(btn, thresholdSec);
		}
		float GetPadAxis(uint32_t padIndex, PadAxis axis)
		{
			const Pad* pad = InputManager::Get().GetPad(padIndex);
			return pad ? pad->GetAxis(axis) : 0.0f;
		}
		bool IsPadConnected(uint32_t padIndex)
		{
			const Pad* pad = InputManager::Get().GetPad(padIndex);
			return pad && pad->IsConnected();
		}
	}
}
