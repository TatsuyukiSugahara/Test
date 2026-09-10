#include "aq.h"
#include "Input.h"
#include "HID/KeyboardMouseBackend.h"
#include "HID/PadBackend.h"


namespace aq
{
	namespace hid
	{
		// ==========================================
		// KeyBoard
		// ==========================================

		void KeyBoard::Update(float dt)
		{
			old_ = now_;

			if (backend_)
			{
				backend_->Poll(now_);
			}
			else
			{
				now_ = {};
			}

			for (uint32_t i = 0; i < KEY_COUNT; ++i)
			{
				if (now_.keys[i] & 0x80)
					holdTimers_[i] += dt;
				else
					holdTimers_[i] = 0.0f;
			}
		}


		bool KeyBoard::IsTriggered(KeyBoardType key) const
		{
			const uint32_t k = static_cast<uint32_t>(key);
			return !(old_.keys[k] & 0x80) && (now_.keys[k] & 0x80);
		}


		bool KeyBoard::IsPressed(KeyBoardType key) const
		{
			return (now_.keys[static_cast<uint32_t>(key)] & 0x80) != 0;
		}


		bool KeyBoard::IsReleased(KeyBoardType key) const
		{
			const uint32_t k = static_cast<uint32_t>(key);
			return (old_.keys[k] & 0x80) && !(now_.keys[k] & 0x80);
		}


		bool KeyBoard::IsLongPress(KeyBoardType key, float thresholdSec) const
		{
			return holdTimers_[static_cast<uint32_t>(key)] >= thresholdSec;
		}


		// ==========================================
		// Mouse
		// ==========================================

		void Mouse::Update(float dt)
		{
			old_ = now_;

			if (backend_)
			{
				backend_->Poll(now_);
			}
			else
			{
				now_ = {};
			}

			for (uint32_t i = 0; i < 3; ++i)
			{
				if (now_.buttons[i] & 0x80)
					holdTimers_[i] += dt;
				else
					holdTimers_[i] = 0.0f;
			}
		}


		bool Mouse::IsTriggered(MouseButton btn) const
		{
			const uint32_t i = static_cast<uint32_t>(btn);
			return !(old_.buttons[i] & 0x80) && (now_.buttons[i] & 0x80);
		}


		bool Mouse::IsPressed(MouseButton btn) const
		{
			return (now_.buttons[static_cast<uint32_t>(btn)] & 0x80) != 0;
		}


		bool Mouse::IsReleased(MouseButton btn) const
		{
			const uint32_t i = static_cast<uint32_t>(btn);
			return (old_.buttons[i] & 0x80) && !(now_.buttons[i] & 0x80);
		}


		bool Mouse::IsLongPress(MouseButton btn, float thresholdSec) const
		{
			return holdTimers_[static_cast<uint32_t>(btn)] >= thresholdSec;
		}


		float Mouse::GetAxis(MouseAxis axis) const
		{
			switch (axis)
			{
			case MouseAxis::DeltaX:     return static_cast<float>(now_.dx);
			case MouseAxis::DeltaY:     return static_cast<float>(now_.dy);
			case MouseAxis::WheelDelta: return static_cast<float>(now_.wheel);
			default:                    return 0.0f;
			}
		}


		math::Vector2 Mouse::GetCursorPos() const
		{
			// カーソル座標はバックエンドが Poll 時にクライアント座標へ変換して入れている。
			return math::Vector2(now_.cursorX, now_.cursorY);
		}


		// ==========================================
		// Pad
		// 生のデバイス取得・振動は IPadBackend(既定: XInput)に委譲し、
		// ここでは正規化状態(PadState)に対する判定とタイマー管理のみを行う。
		// ==========================================

		void Pad::Update(float dt)
		{
			old_ = now_;
			if (backend_)
			{
				backend_->Poll(index_, now_);
			}
			else
			{
				now_ = {};
			}

			// 出力の適用はここ (メインスレッド) だけで行う。
			// 未接続時も呼んで、再接続したときに送り直せるようにしておく。
			ApplyOutput(dt);

			if (!now_.connected)
			{
				old_ = {};
				aq::memory::Clear(holdTimers_, sizeof(holdTimers_));
				return;
			}

			for (uint32_t i = 0; i < BTN_COUNT; ++i)
			{
				if (now_.buttons[i])
					holdTimers_[i] += dt;
				else
					holdTimers_[i] = 0.0f;
			}
		}


		bool Pad::IsTriggered(PadButton btn) const
		{
			const uint32_t idx = static_cast<uint32_t>(btn);
			return !old_.buttons[idx] && now_.buttons[idx];
		}


		bool Pad::IsPressed(PadButton btn) const
		{
			return now_.buttons[static_cast<uint32_t>(btn)];
		}


		bool Pad::IsReleased(PadButton btn) const
		{
			const uint32_t idx = static_cast<uint32_t>(btn);
			return old_.buttons[idx] && !now_.buttons[idx];
		}


		bool Pad::IsLongPress(PadButton btn, float thresholdSec) const
		{
			return holdTimers_[static_cast<uint32_t>(btn)] >= thresholdSec;
		}


		float Pad::GetAxis(PadAxis axis) const
		{
			if (!now_.connected) return 0.0f;
			return now_.axes[static_cast<uint32_t>(axis)];
		}


		void Pad::Vibrate(float left, float right)
		{
			if (!now_.connected || !backend_) return;
			backend_->SetVibration(index_, left, right);
		}


		void Pad::Rumble(float left, float right, float durationSec)
		{
			// ワーカースレッドから呼ばれる想定。値を積むだけで、送信も残時間の減衰も
			// メインスレッドの Update に任せる (ここでタイマーを持たない)。
			rumbleLeft_ .store(math::Clamp01(left));
			rumbleRight_.store(math::Clamp01(right));
			rumbleRemainSec_.store(durationSec > 0.0f ? durationSec : 0.0f);
		}


		void Pad::SetTriggerResistance(PadAxis trigger, float startPos, float strength)
		{
			if (trigger != PadAxis::LTrigger && trigger != PadAxis::RTrigger) return;

			const uint32_t i = (trigger == PadAxis::LTrigger) ? 0 : 1;
			triggerStartPos_[i].store(math::Clamp01(startPos));
			triggerStrength_[i].store(math::Clamp01(strength));
		}


		void Pad::ApplyOutput(float dt)
		{
			if (!backend_) return;

			// 未接続なら送信済みの記録を捨てる。再接続時に必ず送り直される。
			if (!now_.connected)
			{
				appliedRumbleLeft_  = 0.0f;
				appliedRumbleRight_ = 0.0f;
				aq::memory::Clear(appliedStartPos_, sizeof(appliedStartPos_));
				aq::memory::Clear(appliedStrength_, sizeof(appliedStrength_));
				return;
			}

			// 振動の残時間。0 になったら自動で止める。
			// 減衰中にワーカーが Rumble を呼ぶと 1 フレーム分ずれるが、体感差は無いので許容する。
			float remain = rumbleRemainSec_.load();
			if (remain > 0.0f)
			{
				remain -= dt;
				if (remain <= 0.0f)
				{
					remain = 0.0f;
					rumbleLeft_ .store(0.0f);
					rumbleRight_.store(0.0f);
				}
				rumbleRemainSec_.store(remain);
			}

			const float left  = rumbleLeft_ .load();
			const float right = rumbleRight_.load();
			if (left != appliedRumbleLeft_ || right != appliedRumbleRight_)
			{
				backend_->SetVibration(index_, left, right);
				appliedRumbleLeft_  = left;
				appliedRumbleRight_ = right;
			}

			for (uint32_t i = 0; i < TRIGGER_COUNT; ++i)
			{
				const float startPos = triggerStartPos_[i].load();
				const float strength = triggerStrength_[i].load();
				if (startPos == appliedStartPos_[i] && strength == appliedStrength_[i]) continue;

				backend_->SetTriggerResistance(index_, (i == 0) ? PadAxis::LTrigger : PadAxis::RTrigger, startPos, strength);
				appliedStartPos_[i] = startPos;
				appliedStrength_[i] = strength;
			}
		}


		// ==========================================
		// InputManager
		// ==========================================

		InputManager* InputManager::sInstance_ = nullptr;


		InputManager::InputManager()
			: lastTime_(Clock::now())
		{
			// パッドバックエンドはプラットフォームで選択(Win32=XInput / UWP=Windows.Gaming.Input)。
			// キーボード / マウスの Setup 成否に依らず動くよう、ここで生成しておく。
			for (uint32_t i = 0; i < MAX_PAD_COUNT; ++i)
			{
				pads_[i].SetIndex(i);
			}
			padBackend_ = std::make_unique<DefaultPadBackend>();
			for (uint32_t i = 0; i < MAX_PAD_COUNT; ++i)
			{
				pads_[i].SetBackend(padBackend_.get());
			}
		}


		InputManager::~InputManager()
		{
			// 参照する側 (KeyBoard / Mouse) を先に壊してからバックエンドを解放する
			keyBoard_.reset();
			mouse_.reset();
			keyboardBackend_.reset();
			mouseBackend_.reset();
		}


		bool InputManager::Setup()
		{
			// キーボード / マウスのバックエンドはプラットフォームで選択
			// (Win32=DirectInput / UWP・Mac=Null)。協調レベル設定に使うウィンドウは
			// プラットフォーム非依存ハンドルで渡す。
			const aq::graphics::NativeWindowHandle window = aq::Engine::Get().GetNativeWindowHandle();

			keyboardBackend_ = std::make_unique<DefaultKeyboardBackend>();
			keyBoard_        = std::make_unique<KeyBoard>();
			keyBoard_->SetBackend(keyboardBackend_.get());
			if (!keyboardBackend_->Initialize(window)) return false;

			mouseBackend_ = std::make_unique<DefaultMouseBackend>();
			mouse_        = std::make_unique<Mouse>();
			mouse_->SetBackend(mouseBackend_.get());
			if (!mouseBackend_->Initialize(window)) return false;

			return true;
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
