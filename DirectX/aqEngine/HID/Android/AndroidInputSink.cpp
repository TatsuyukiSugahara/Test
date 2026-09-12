#include "aq.h"
// Android 以外では空 TU。
#if defined(AQ_PLATFORM_ANDROID)
#include "HID/Android/AndroidInputSink.h"
#include <android/input.h>
#include <android/keycodes.h>


namespace aq
{
	namespace hid
	{
		namespace
		{
			/** AKEYCODE_* と PadButton(中立値)の対応 */
			struct PadKeyMapEntry
			{
				int32_t   keyCode;
				PadButton button;
			};

			// PadButton の全要素を網羅する。LT / RT はアナログトリガー
			// (AXIS_LTRIGGER 等)からも立つが、デジタルで L2 / R2 を送ってくる
			// 機種があるのでキー側にも置いておく。
			// AKEYCODE_BACK は載せない(戻るキーでアプリを終了させるため、
			// 入力として飲み込んではいけない)。
			static constexpr PadKeyMapEntry PAD_KEY_MAP[] =
			{
				{ AKEYCODE_BUTTON_A,      PadButton::A      },
				{ AKEYCODE_BUTTON_B,      PadButton::B      },
				{ AKEYCODE_BUTTON_X,      PadButton::X      },
				{ AKEYCODE_BUTTON_Y,      PadButton::Y      },

				{ AKEYCODE_BUTTON_L1,     PadButton::LB     },
				{ AKEYCODE_BUTTON_R1,     PadButton::RB     },
				{ AKEYCODE_BUTTON_L2,     PadButton::LT     },
				{ AKEYCODE_BUTTON_R2,     PadButton::RT     },

				{ AKEYCODE_DPAD_UP,       PadButton::DUp    },
				{ AKEYCODE_DPAD_DOWN,     PadButton::DDown  },
				{ AKEYCODE_DPAD_LEFT,     PadButton::DLeft  },
				{ AKEYCODE_DPAD_RIGHT,    PadButton::DRight },

				{ AKEYCODE_BUTTON_START,  PadButton::Start  },
				{ AKEYCODE_BUTTON_SELECT, PadButton::Back   },

				{ AKEYCODE_BUTTON_THUMBL, PadButton::LStick },
				{ AKEYCODE_BUTTON_THUMBR, PadButton::RStick },
			};


			/** padHat_ の添字 */
			static constexpr uint32_t HAT_UP    = 0;
			static constexpr uint32_t HAT_DOWN  = 1;
			static constexpr uint32_t HAT_LEFT  = 2;
			static constexpr uint32_t HAT_RIGHT = 3;

			/**
			 * スティックのデッドゾーン。XInput の XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE(7849)/
			 * RIGHT_THUMB_DEADZONE(8689)を SHORT の最大値 32767 で割った値。
			 * プラットフォーム間で「倒し始めの遊び」を揃えるため、数値ごと合わせている。
			 */
			static constexpr float LEFT_STICK_DEAD_ZONE  = 7849.0f / 32767.0f;
			static constexpr float RIGHT_STICK_DEAD_ZONE = 8689.0f / 32767.0f;

			/** トリガーをボタン扱いする閾値。XInput の bLeftTrigger > 128 と同じ位置 */
			static constexpr float TRIGGER_BUTTON_THRESHOLD = 128.0f / 255.0f;

			/** ハットスイッチを押下とみなす閾値。中央が 0 / 端が ±1 で届く */
			static constexpr float HAT_THRESHOLD = 0.5f;


			/** レベルを更新し、押された瞬間なら取りこぼし対策の latch も立てる */
			void ApplyLevel(bool& level, bool& pressedSinceFetch, const bool pressed)
			{
				level = pressed;
				if (pressed) {
					pressedSinceFetch = true;
				}
			}


			/** トリガー値を [0, 1] に収める */
			float Clamp01(const float value)
			{
				return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
			}
		}


		AndroidInputSink& AndroidInputSink::Get()
		{
			static AndroidInputSink instance;
			return instance;
		}


		void AndroidInputSink::OnTouchDown(const int32_t pointerId, const float x, const float y)
		{
			TouchSlot* slot = FindTouchSlot(pointerId, true);
			if (slot == nullptr) {
				// 同時に触れられる点数を超えた。後から触れた指は捨てる
				// (MAX_POINT_COUNT = 10 なので実機で起きるのは事故のときだけ)。
				return;
			}

			slot->id                = pointerId;
			slot->x                 = x;
			slot->y                 = y;
			slot->down              = true;
			slot->pressedSinceFetch = true;
		}


		void AndroidInputSink::OnTouchMove(const int32_t pointerId, const float x, const float y)
		{
			TouchSlot* slot = FindTouchSlot(pointerId, false);
			if (slot == nullptr) {
				// 押下を取り逃していても動きは拾えるようにしておく
				// (フォーカス復帰直後など、down が来ないまま move から始まる場合がある)。
				OnTouchDown(pointerId, x, y);
				return;
			}

			slot->x = x;
			slot->y = y;
		}


		void AndroidInputSink::OnTouchUp(const int32_t pointerId, const float x, const float y)
		{
			TouchSlot* slot = FindTouchSlot(pointerId, false);
			if (slot == nullptr) {
				return;
			}

			// スロットは消さない。次の取得で pressed = false を 1 回返してから空ける
			// (上位のタップ判定は「押下 → 解放」の組で成立するため)。
			slot->x    = x;
			slot->y    = y;
			slot->down = false;
		}


		void AndroidInputSink::OnTouchCancel()
		{
			for (TouchSlot& slot : touches_)
			{
				slot.down = false;

				// 取りこぼし対策の latch も落とす。OS にジェスチャを奪われた場合は
				// 「押して離した」ではないので、タップとして成立させてはいけない。
				slot.pressedSinceFetch = false;
			}
		}


		bool AndroidInputSink::OnPadButton(const int32_t keyCode, const bool pressed)
		{
			for (const PadKeyMapEntry& entry : PAD_KEY_MAP)
			{
				if (entry.keyCode == keyCode) {
					const uint32_t index = static_cast<uint32_t>(entry.button);
					ApplyLevel(padButtons_[index], padPressedSinceFetch_[index], pressed);
					padEventSeen_ = true;
					return true;
				}
			}

			// 対応表に無いキーは扱わない(= 呼び出し側でイベントを消費させない)。
			return false;
		}


		void AndroidInputSink::OnPadAxis(const int32_t axis, const float value)
		{
			switch (axis) {
			case AMOTION_EVENT_AXIS_X:  stickLX_ = value; break;
			case AMOTION_EVENT_AXIS_Y:  stickLY_ = value; break;
			case AMOTION_EVENT_AXIS_Z:  stickRX_ = value; break;
			case AMOTION_EVENT_AXIS_RZ: stickRY_ = value; break;

			case AMOTION_EVENT_AXIS_LTRIGGER: triggerL_ = value; break;
			case AMOTION_EVENT_AXIS_RTRIGGER: triggerR_ = value; break;
			case AMOTION_EVENT_AXIS_BRAKE:    brake_    = value; break;
			case AMOTION_EVENT_AXIS_GAS:      gas_      = value; break;

			case AMOTION_EVENT_AXIS_HAT_X:
				ApplyLevel(padHat_[HAT_LEFT],
				           padPressedSinceFetch_[static_cast<uint32_t>(PadButton::DLeft)],
				           value <= -HAT_THRESHOLD);
				ApplyLevel(padHat_[HAT_RIGHT],
				           padPressedSinceFetch_[static_cast<uint32_t>(PadButton::DRight)],
				           value >= HAT_THRESHOLD);
				break;

			case AMOTION_EVENT_AXIS_HAT_Y:
				// ハットの Y は画面と同じ向き(上が負)で届く。
				ApplyLevel(padHat_[HAT_UP],
				           padPressedSinceFetch_[static_cast<uint32_t>(PadButton::DUp)],
				           value <= -HAT_THRESHOLD);
				ApplyLevel(padHat_[HAT_DOWN],
				           padPressedSinceFetch_[static_cast<uint32_t>(PadButton::DDown)],
				           value >= HAT_THRESHOLD);
				break;

			default:
				// 見ていない軸(圧力・傾き等)。接続扱いにもしない。
				return;
			}

			padEventSeen_ = true;
		}


		void AndroidInputSink::OnFocusLost()
		{
			OnTouchCancel();

			aq::memory::Clear(padButtons_, sizeof(padButtons_));
			aq::memory::Clear(padPressedSinceFetch_, sizeof(padPressedSinceFetch_));
			aq::memory::Clear(padHat_, sizeof(padHat_));

			// 倒したままバックグラウンドへ行った場合に持ち越さない。
			stickLX_  = 0.0f;
			stickLY_  = 0.0f;
			stickRX_  = 0.0f;
			stickRY_  = 0.0f;
			triggerL_ = 0.0f;
			triggerR_ = 0.0f;
			brake_    = 0.0f;
			gas_      = 0.0f;

			// padEventSeen_ は落とさない。フォーカスを失っただけで
			// コントローラが抜けたわけではないため。
		}


		void AndroidInputSink::FetchTouch(TouchState& out)
		{
			out = {};

			for (TouchSlot& slot : touches_)
			{
				if (slot.id < 0) {
					continue;
				}

				const bool pressed     = slot.down || slot.pressedSinceFetch;
				slot.pressedSinceFetch = false;

				if (out.count < TouchState::MAX_POINT_COUNT) {
					TouchPoint& point = out.points[out.count];
					point.id      = slot.id;
					point.x       = slot.x;
					point.y       = slot.y;
					point.pressed = pressed;
					++out.count;
				}

				// pressed = false を返した回で解放する。これで「離した瞬間のフレームだけ
				// pressed = false が 1 回返る」形になる。
				if (!pressed) {
					slot = TouchSlot{};
				}
			}
		}


		void AndroidInputSink::FetchPad(PadState& out)
		{
			out = {};

			// 物理パッドのイベントが一度も来ていなければ未接続。タッチ側の仮想パッドが
			// 常に接続を返すので、こちらは正直に false でよい。
			out.connected = padEventSeen_;
			if (!out.connected) {
				return;
			}

			for (uint32_t i = 0; i < PadState::BUTTON_COUNT; ++i)
			{
				out.buttons[i] = padButtons_[i] || padPressedSinceFetch_[i];
			}

			// ハット由来の十字キーを重ねる(キー由来の押下を消さないよう OR)。
			{
				static constexpr PadButton HAT_BUTTONS[] =
				{
					PadButton::DUp, PadButton::DDown, PadButton::DLeft, PadButton::DRight,
				};
				for (uint32_t i = 0; i < 4; ++i)
				{
					const uint32_t index = static_cast<uint32_t>(HAT_BUTTONS[i]);
					out.buttons[index]   = out.buttons[index] || padHat_[i];
				}
			}

			// トリガーは LTRIGGER/RTRIGGER と BRAKE/GAS のどちらで来ても拾う。
			const float leftTrigger  = Clamp01(triggerL_ > brake_ ? triggerL_ : brake_);
			const float rightTrigger = Clamp01(triggerR_ > gas_   ? triggerR_ : gas_);
			out.axes[static_cast<uint32_t>(PadAxis::LTrigger)] = leftTrigger;
			out.axes[static_cast<uint32_t>(PadAxis::RTrigger)] = rightTrigger;

			// LT / RT ボタンはアナログ値からも立てる(XInput 実装と同じ閾値)。
			{
				const uint32_t lt = static_cast<uint32_t>(PadButton::LT);
				const uint32_t rt = static_cast<uint32_t>(PadButton::RT);
				out.buttons[lt] = out.buttons[lt] || (leftTrigger  > TRIGGER_BUTTON_THRESHOLD);
				out.buttons[rt] = out.buttons[rt] || (rightTrigger > TRIGGER_BUTTON_THRESHOLD);
			}

			// Android のスティックは上が負(画面座標系)。XInput の sThumbLY は上が正なので
			// Y を反転して、上位から見た向きをプラットフォーム間で揃える。
			out.axes[static_cast<uint32_t>(PadAxis::LX)] = NormalizeStick( stickLX_, LEFT_STICK_DEAD_ZONE);
			out.axes[static_cast<uint32_t>(PadAxis::LY)] = NormalizeStick(-stickLY_, LEFT_STICK_DEAD_ZONE);
			out.axes[static_cast<uint32_t>(PadAxis::RX)] = NormalizeStick( stickRX_, RIGHT_STICK_DEAD_ZONE);
			out.axes[static_cast<uint32_t>(PadAxis::RY)] = NormalizeStick(-stickRY_, RIGHT_STICK_DEAD_ZONE);

			aq::memory::Clear(padPressedSinceFetch_, sizeof(padPressedSinceFetch_));
		}


		AndroidInputSink::TouchSlot* AndroidInputSink::FindTouchSlot(const int32_t pointerId, const bool create)
		{
			if (pointerId < 0) {
				return nullptr;
			}

			for (TouchSlot& slot : touches_)
			{
				if (slot.id == pointerId) {
					return &slot;
				}
			}

			if (!create) {
				return nullptr;
			}

			for (TouchSlot& slot : touches_)
			{
				if (slot.id < 0) {
					return &slot;
				}
			}
			return nullptr;
		}


		float AndroidInputSink::NormalizeStick(const float value, const float deadZone)
		{
			if (value > -deadZone && value < deadZone) {
				return 0.0f;
			}

			float result;
			if (value > 0.0f) {
				result = (value - deadZone) / (1.0f - deadZone);
			} else {
				result = (value + deadZone) / (1.0f - deadZone);
			}

			// 機種によっては生値が ±1 をわずかに超えて届くのでクランプする。
			return result < -1.0f ? -1.0f : (result > 1.0f ? 1.0f : result);
		}
	}
}
#endif // AQ_PLATFORM_ANDROID
