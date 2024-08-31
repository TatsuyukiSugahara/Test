#include "../EnginePreCompile.h"
#include "Input.h"
#include "../Engine.h"


namespace engine
{
	namespace hid
	{
		KeyBoard::KeyBoard()
		{
			engine::memory::Clear(now_, sizeof(now_));
			engine::memory::Clear(old_, sizeof(old_));
		}


		KeyBoard::~KeyBoard()
		{
		}


		HRESULT KeyBoard::Initialize(LPDIRECTINPUT8 input)
		{
			// �f�o�C�X����
			if (FAILED(input->CreateDevice(GUID_SysKeyboard, &device_, nullptr))) {
				return S_FALSE;
			}

			// �󂯎��\���̂̃t�H�[�}�b�g��ݒ�
			if (FAILED(device_->SetDataFormat(&c_dfDIKeyboard))) {
				return S_FALSE;
			}

			// �f�o�C�X�ւ̃A�N�Z�X�����擾
			device_->Acquire();

			return S_OK;
		}

		void KeyBoard::Update()
		{
			// �O�t���[���̏����R�s�[
			engine::memory::Copy(old_, now_, sizeof(old_));
			// �L�[�{�[�h�{�������󂯎��
			device_->GetDeviceState(sizeof(now_), &now_);
		}


		bool KeyBoard::IsTrigger(const uint32_t key) const
		{
			return (!(old_[key] & 0x80) && (now_[key] & 0x80));
		}


		bool KeyBoard::IsPressed(const uint32_t key) const
		{
			return (now_[key] & 0x80) > 0;
		}




		/*******************************************/


		Mouse::Mouse()
		{
		}


		Mouse::~Mouse()
		{
		}


		HRESULT Mouse::Initialize(LPDIRECTINPUT8 input, HWND hWnd)
		{
			//�f�o�C�X�쐬
			if (FAILED(input->CreateDevice(GUID_SysMouse, &device_, NULL))) {
				return S_FALSE;
			}

			//�󂯎��\���̂̃t�H�[�}�b�g��ݒ�
			if (FAILED(device_->SetDataFormat(&c_dfDIMouse2)))
			{
				return S_FALSE;
			}

			// ���̓f�o�C�X�ւ̃A�N�Z�X�����擾
			device_->Acquire();

			device_->SetCooperativeLevel(hWnd, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);


			// �f�o�C�X�̐ݒ�    
			DIPROPDWORD diprop;
			diprop.diph.dwSize = sizeof(DIPROPDWORD);
			diprop.diph.dwHeaderSize = sizeof(DIPROPHEADER);
			diprop.diph.dwObj = 0;
			diprop.diph.dwHow = DIPH_DEVICE;
			// ���Βl���[�h�Őݒ�i��Βl��DIPROPAXISMODE_ABS�j
			//���Βl�͑O�t���[������̑��Βl(�ړ��ʁH)
			//��Βl�͑��Βl�̗݌v
			diprop.dwData = DIPROPAXISMODE_REL;
			device_->SetProperty(DIPROP_AXISMODE, &diprop.diph);

			// ���͐���J�n
			device_->Acquire();

			return S_OK;
		}

		void Mouse::Update()
		{
			engine::memory::Clear(&mouseState_, sizeof(DIMOUSESTATE2));
			if (FAILED(device_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_))) {
				// ���s�����Ƃ��͂�����x
				device_->Acquire();
				device_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_);
			}
		}


		int32_t Mouse::GetValue(MouseType type) const
		{
			int32_t value = 0;
			switch (type)
			{
				case CLICK_L:		value = (mouseState_.rgbButtons[0] & 0x80); break;
				case CLICK_R:		value = (mouseState_.rgbButtons[1] & 0x80); break;
				case CLICK_WHEEL:	value = (mouseState_.rgbButtons[2] & 0x80); break;
				case X:				value = mouseState_.lX;						break;
				case Y:				value = mouseState_.lY;						break;
				case ROLL_WHEEL:	value = mouseState_.lZ;						break;
			}
			return value;
		}


		engine::math::Vector2 Mouse::GetCursorPositionOnScreen() const
		{
			POINT pos;
			GetCursorPos(&pos);
			return engine::math::Vector2(static_cast<float>(pos.x), static_cast<float>(pos.y));
		}


		engine::math::Vector2 Mouse::GetCursorPositionOnWindow(HWND hwnd) const
		{
			POINT pos;
			GetCursorPos(&pos);
			ScreenToClient(hwnd, &pos);
			return engine::math::Vector2(static_cast<float>(pos.x), static_cast<float>(pos.y));
		}




		/*******************************************/


		InputManager* InputManager::sInstance_ = nullptr;


		InputManager::InputManager()
			: keyBoard_(nullptr)
		{
		}


		InputManager::~InputManager()
		{
			if (keyBoard_) {
				delete keyBoard_;
				keyBoard_ = nullptr;
			}
		}


		HRESULT InputManager::Setup()
		{
			if (FAILED(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&input_, nullptr))) {
				return S_FALSE;
			}

			// �L�[�{�[�h������
			keyBoard_ = new KeyBoard();
			if (FAILED(keyBoard_->Initialize(input_))) {
				return S_FALSE;
			}

			// �}�E�X������
			mouse_ = new Mouse();
			if (FAILED(mouse_->Initialize(input_, engine::Engine::Get().GetHWND()))) {
				return S_FALSE;
			}

			return S_OK;
		}


		void InputManager::Update()
		{
			keyBoard_->Update();
		}
	}
}