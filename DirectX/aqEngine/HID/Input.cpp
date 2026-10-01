#include "aq.h"
#include "Input.h"
#include "HID/KeyboardMouseBackend.h"
#include "HID/TouchBackend.h"
#include "HID/PadBackend.h"


namespace aq
{
	namespace hid
	{
		namespace
		{
			/**
			 * 「送信済み」の記録に入れて、次の ApplyOutput で必ず送り直させる値。
			 * 出力値は [0, 1] なので、どの値とも一致しない。
			 */
			static constexpr float FORCE_RESEND_VALUE = -1.0f;
		}


		// ==========================================
		// KeyBoard
		// ==========================================

		void KeyBoard::Update(float dt, bool blocked)
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

			// フォーカス外は全キー離しとして扱う。押したまま外れたキーにも Released が 1 回出る。
			if (blocked)
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

		void Mouse::Update(float dt, bool blocked)
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

			// フォーカス外は全ボタン離し・移動 0 として扱う。カーソル位置は前フレームの値に留め、
			// エディタ側を操作している間にゲーム内 UI のホバーが動かないようにする。
			if (blocked)
			{
				const float cursorX = old_.cursorX;
				const float cursorY = old_.cursorY;
				now_         = {};
				now_.cursorX = cursorX;
				now_.cursorY = cursorY;
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

			// フォーカス外はボタンと軸だけを中立値にする。connected は実際の値のまま残す。
			// 未接続扱いにすると下で old_ まで消え、押したまま外れたボタンに Released が出ない。
			if (IsFocusBlocked())
			{
				aq::memory::Clear(now_.buttons, sizeof(now_.buttons));
				aq::memory::Clear(now_.axes, sizeof(now_.axes));
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
			// フォーカス外の入力停止中はパッドを鳴らさない。
			if (IsFocusBlocked()) return;
			backend_->SetVibration(index_, left, right);
		}


		void Pad::Rumble(float left, float right, float durationSec)
		{
			// フォーカス外の入力停止中に要求された振動は受け付けた時点で捨てる。
			// 積んでおくと、フォーカスが戻った瞬間に古い振動が鳴り出す。
			if (IsFocusBlocked()) return;

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


		void Pad::DiscardRumble()
		{
			// 残り時間だけを 0 にしても止まらない(残り 0 は「止めるまで鳴り続ける」扱い)ので、3 つとも 0 にする。
			rumbleLeft_     .store(0.0f);
			rumbleRight_    .store(0.0f);
			rumbleRemainSec_.store(0.0f);

			// 旧 API の Vibrate はこの記録を通らずに鳴らすので、記録が 0 のままだと停止を送らない。
			// 次の ApplyOutput で必ず送り直させる。
			appliedRumbleLeft_  = FORCE_RESEND_VALUE;
			appliedRumbleRight_ = FORCE_RESEND_VALUE;
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

			// フォーカス外の入力停止中は、振動とトリガーエフェクトの強度を 0 として送る。
			// トリガーエフェクトの設定値は保持しておき、明けたフレームに差分として送り直される。
			const bool  blocked = IsFocusBlocked();
			const float left    = blocked ? 0.0f : rumbleLeft_ .load();
			const float right   = blocked ? 0.0f : rumbleRight_.load();
			if (left != appliedRumbleLeft_ || right != appliedRumbleRight_)
			{
				backend_->SetVibration(index_, left, right);
				appliedRumbleLeft_  = left;
				appliedRumbleRight_ = right;
			}

			for (uint32_t i = 0; i < TRIGGER_COUNT; ++i)
			{
				const float startPos = triggerStartPos_[i].load();
				const float strength = blocked ? 0.0f : triggerStrength_[i].load();
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
			// パッドバックエンドはプラットフォームで選択(Win32=XInput / UWP=Windows.Gaming.Input /
			// Android=物理コントローラ + 仮想パッドの合成)。
			// キーボード / マウスの Setup 成否に依らず動くよう、ここで生成しておく。
			for (uint32_t i = 0; i < MAX_PAD_COUNT; ++i)
			{
				pads_[i].SetIndex(i);
				pads_[i].SetFocusBlockedFlag(&focusBlocked_);
			}

			// タッチはパッドより先に作る。Android の仮想パッドがこれを参照するため。
			// タッチを持たない環境では Null が入り、上位は分岐せずに済む。
			touchBackend_ = std::make_unique<DefaultTouchBackend>();

			// 生成のしかたがプラットフォームで違う(仮想パッドはタッチを要求する)ので、
			// 組み立ては PadBackend.h のファクトリへ寄せてここには #if を持ち込まない。
			//
			// 仮想パッドへ渡すのは ITouchBackend ではなく **取り込み済みの TouchState**。
			// タッチの取得は「取得までに一度でも押されたら押下として返す」ラッチを持ち、
			// 取得で消費されるため、同一フレームに 2 回取ると 2 回目が空になる。
			// Update で 1 回だけ取り込み、UI と仮想パッドが同じ値を読む形にしている。
			padBackend_ = CreateDefaultPadBackend(&touch_);
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
			// 仮想パッドが ITouchBackend を参照しているので、パッドを先に壊す
			padBackend_.reset();
			touchBackend_.reset();
		}


		bool InputManager::Setup()
		{
			// キーボード / マウスのバックエンドはプラットフォームで選択
			// (Win32=DirectInput / UWP・Mac=Null)。協調レベル設定と座標変換に使うウィンドウは
			// プラットフォーム非依存ハンドルで渡す。
			// 通常は両方ともメインウィンドウで、フォーカスがあるときだけ読む(従来どおり)。
			// 埋め込み時は子ウィンドウを協調ウィンドウにできないので、入力協調用の非表示ウィンドウへ
			// バックグラウンドで設定し、フォーカスの判定は SetFocusBlocked で別に行う。
			const aq::Engine& engine = aq::Engine::Get();
			InputWindowDesc desc;
			desc.clientWindow      = engine.GetNativeWindowHandle();
			desc.cooperativeWindow = engine.GetInputCooperativeWindow();
			desc.background        = engine.IsEmbedded();

			keyboardBackend_ = std::make_unique<DefaultKeyboardBackend>();
			keyBoard_        = std::make_unique<KeyBoard>();
			keyBoard_->SetBackend(keyboardBackend_.get());
			if (!keyboardBackend_->Initialize(desc)) return false;

			// 生成のしかたがプラットフォームで違う(タッチから合成する環境がある)ので、
			// 組み立ては KeyboardMouseBackend.h のファクトリへ寄せる。
			mouseBackend_ = CreateDefaultMouseBackend(&pointerTouch_);
			mouse_        = std::make_unique<Mouse>();
			mouse_->SetBackend(mouseBackend_.get());
			if (!mouseBackend_->Initialize(desc)) return false;

			return true;
		}


		void InputManager::Update()
		{
			const auto  now = Clock::now();
			const float dt  = std::chrono::duration<float>(now - lastTime_).count();
			lastTime_ = now;

			// タッチはパッドより先に取り込む。仮想パッドが Pad::Update の中で
			// ITouchBackend を読むため、同じフレームの値が見えている必要がある。
			if (touchBackend_) touchBackend_->Poll(touch_);

			// フォーカス外の入力停止はフレームの境目で SetFocusBlocked 済み。ここで 1 回だけ読む。
			const bool blocked = focusBlocked_.load();

			if (keyBoard_) keyBoard_->Update(dt, blocked);

			// パッドはマウスより先に更新する。タッチをポインタとして扱うプラットフォームでは、
			// 仮想パッドが掴んだ指を除いてからでないとマウス側が拾ってしまう
			// (スティックを倒した指で裏の UI をクリックしてしまう)。
			for (auto& pad : pads_) pad.Update(dt);
			BuildPointerTouchState();

			if (mouse_) mouse_->Update(dt, blocked);
		}


		void InputManager::SetFocusBlocked(bool blocked)
		{
			if (focusBlocked_.load() == blocked) return;

			// 先にフラグを立ててから捨てる。以降の Rumble は受付時に捨てられる。
			// 明けるときにも捨てるのは、受付の判定と状態の変更がすれ違った要求を拾うため。
			// ここはフレームの境目(ワーカーが動いていない)で呼ばれるので、明けた後の正規の要求は消さない。
			focusBlocked_.store(blocked);
			for (auto& pad : pads_) pad.DiscardRumble();
		}


		void InputManager::BuildPointerTouchState()
		{
			pointerTouch_.count = 0u;

			const uint32_t count = (touch_.count < TouchState::MAX_POINT_COUNT)
				                     ? touch_.count : TouchState::MAX_POINT_COUNT;
			for (uint32_t i = 0; i < count; ++i) {
				const TouchPoint& point = touch_.points[i];
				if (padBackend_ && padBackend_->IsTouchConsumed(point.id)) {
					continue;
				}
				pointerTouch_.points[pointerTouch_.count] = point;
				++pointerTouch_.count;
			}
		}


		Pad* InputManager::GetPad(uint32_t index)
		{
			EngineAssert(index < MAX_PAD_COUNT);
			if (index >= MAX_PAD_COUNT) return nullptr;
			return &pads_[index];
		}


		void InputManager::Vibrate(uint32_t padIndex, float left, float right)
		{
			// フォーカス外の入力停止中はパッドを鳴らさない(Pad::Vibrate 側でも同じ判定をする)。
			if (IsFocusBlocked()) return;
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
