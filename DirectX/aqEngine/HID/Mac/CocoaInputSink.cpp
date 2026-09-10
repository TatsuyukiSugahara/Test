#include "aq.h"
// macOS 以外では空 TU。
#if defined(AQ_PLATFORM_MAC)
#include "HID/Mac/CocoaInputSink.h"

#include <cmath>


namespace aq
{
	namespace hid
	{
		namespace
		{
			/**
			 * macOS の仮想キーコード(Carbon の kVK_*)。
			 *
			 * `Carbon.framework` を引くためだけにリンクを増やしたくないので、値をここに写す。
			 * これらは ANSI 配列を基準とした**物理位置**の番号で、macOS の歴史を通じて固定。
			 * JIS 配列でも同じ物理キーに同じ番号が振られる。
			 */
			enum : uint16_t
			{
				kVK_ANSI_A      = 0x00,
				kVK_ANSI_S      = 0x01,
				kVK_ANSI_D      = 0x02,
				kVK_ANSI_G      = 0x05,
				kVK_ANSI_W      = 0x0D,
				kVK_ANSI_1      = 0x12,
				kVK_ANSI_2      = 0x13,
				kVK_ANSI_3      = 0x14,
				kVK_ANSI_4      = 0x15,
				kVK_Return      = 0x24,
				kVK_Space       = 0x31,
				kVK_Escape      = 0x35,
				kVK_LeftArrow   = 0x7B,
				kVK_RightArrow  = 0x7C,
				kVK_DownArrow   = 0x7D,
				kVK_UpArrow     = 0x7E,
			};


			/** KeyBoardType(中立値)と macOS 仮想キーコードの対応 */
			struct KeyMapEntry
			{
				KeyBoardType key;
				uint16_t     macKeyCode;
			};


			// KeyBoardType の全要素を網羅する。要素を増やしたらここにも追加すること
			// (対応は DirectInputKeyboardBackend.cpp の KEY_MAP と 1 対 1 に保つ)。
			static constexpr KeyMapEntry KEY_MAP[] =
			{
				{ KeyBoardType::Left,   kVK_LeftArrow  },
				{ KeyBoardType::Right,  kVK_RightArrow },
				{ KeyBoardType::Up,     kVK_UpArrow    },
				{ KeyBoardType::Down,   kVK_DownArrow  },

				{ KeyBoardType::W,      kVK_ANSI_W     },
				{ KeyBoardType::A,      kVK_ANSI_A     },
				{ KeyBoardType::S,      kVK_ANSI_S     },
				{ KeyBoardType::D,      kVK_ANSI_D     },
				{ KeyBoardType::G,      kVK_ANSI_G     },

				{ KeyBoardType::Space,  kVK_Space      },
				{ KeyBoardType::Enter,  kVK_Return     },
				{ KeyBoardType::Escape, kVK_Escape     },

				{ KeyBoardType::Num1,   kVK_ANSI_1     },
				{ KeyBoardType::Num2,   kVK_ANSI_2     },
				{ KeyBoardType::Num3,   kVK_ANSI_3     },
				{ KeyBoardType::Num4,   kVK_ANSI_4     },
			};


			/** 押下ビット。DirectInput のバッファ形式に合わせる */
			static constexpr uint8_t PRESSED_BIT = 0x80;

			/**
			 * ホイール 1 ノッチ分の量。DirectInput の lZ は WHEEL_DELTA(120)刻みで返るため、
			 * Cocoa の行単位のスクロール量を同じ尺度へ揃える(利用側の閾値を共通にできる)。
			 */
			static constexpr float WHEEL_SCALE = 120.0f;


			/** 溜めた相対量から整数部を取り出し、小数部を残す */
			int32_t ConsumeDelta(float& pending)
			{
				const float truncated = std::trunc(pending);
				pending -= truncated;
				return static_cast<int32_t>(truncated);
			}
		}


		CocoaInputSink& CocoaInputSink::Get()
		{
			static CocoaInputSink instance;
			return instance;
		}


		void CocoaInputSink::OnKey(uint16_t macKeyCode, bool pressed)
		{
			for (const KeyMapEntry& entry : KEY_MAP)
			{
				if (entry.macKeyCode == macKeyCode)
				{
					const uint32_t index = static_cast<uint32_t>(entry.key);
					keyboard_.keys[index] = pressed ? PRESSED_BIT : 0;
					if (pressed)
					{
						pressedSinceFetch_[index] = PRESSED_BIT;
					}
					return;
				}
			}
			// 対応表に無いキーは無視する(KeyBoardType に無い = ゲームが見ていない)。
		}


		void CocoaInputSink::OnModifierFlagsChanged(bool commandPressed)
		{
			// Command を押している間、macOS は通常キーの keyUp を配送しない。
			// 押しっぱなしのまま残るのを防ぐため、離された時点で一掃する。
			if (commandHeld_ && !commandPressed)
			{
				keyboard_ = {};
			}
			commandHeld_ = commandPressed;
		}


		void CocoaInputSink::OnMouseButton(int32_t buttonNumber, bool pressed)
		{
			if (buttonNumber < 0 || buttonNumber >= static_cast<int32_t>(MouseState::BUTTON_COUNT))
			{
				return;
			}
			mouseButtons_[buttonNumber] = pressed ? PRESSED_BIT : 0;
			if (pressed)
			{
				mousePressedSinceFetch_[buttonNumber] = PRESSED_BIT;
			}
		}


		void CocoaInputSink::OnMouseMove(float clientX, float clientY, float deltaX, float deltaY)
		{
			cursorX_ = clientX;
			cursorY_ = clientY;
			pendingDeltaX_ += deltaX;
			pendingDeltaY_ += deltaY;
		}


		void CocoaInputSink::OnScrollWheel(float delta)
		{
			pendingWheel_ += delta * WHEEL_SCALE;
		}


		void CocoaInputSink::OnFocusLost()
		{
			keyboard_ = {};
			aq::memory::Clear(pressedSinceFetch_, sizeof(pressedSinceFetch_));
			aq::memory::Clear(mouseButtons_, sizeof(mouseButtons_));
			aq::memory::Clear(mousePressedSinceFetch_, sizeof(mousePressedSinceFetch_));
			commandHeld_ = false;
			// カーソル位置は最後の値を保つ(復帰時に次の mouseMoved で上書きされる)。
			pendingDeltaX_ = 0.0f;
			pendingDeltaY_ = 0.0f;
			pendingWheel_  = 0.0f;
		}


		void CocoaInputSink::FetchKeyboard(KeyboardState& out)
		{
			for (uint32_t i = 0; i < KeyboardState::KEY_COUNT; ++i)
			{
				out.keys[i] = static_cast<uint8_t>(keyboard_.keys[i] | pressedSinceFetch_[i]);
			}
			aq::memory::Clear(pressedSinceFetch_, sizeof(pressedSinceFetch_));
		}


		void CocoaInputSink::FetchMouse(MouseState& out)
		{
			out = {};
			for (uint32_t i = 0; i < MouseState::BUTTON_COUNT; ++i)
			{
				out.buttons[i] = static_cast<uint8_t>(mouseButtons_[i] | mousePressedSinceFetch_[i]);
			}
			aq::memory::Clear(mousePressedSinceFetch_, sizeof(mousePressedSinceFetch_));
			out.cursorX = cursorX_;
			out.cursorY = cursorY_;

			// 相対量は「前回の取得以降の積算」。消費した分だけ減らす。
			out.dx    = ConsumeDelta(pendingDeltaX_);
			out.dy    = ConsumeDelta(pendingDeltaY_);
			out.wheel = ConsumeDelta(pendingWheel_);
		}
	}
}
#endif // AQ_PLATFORM_MAC
