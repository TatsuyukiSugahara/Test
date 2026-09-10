#include "aq.h"
// UWP / Mac では DirectInput が使えないため空 TU。
#if defined(AQ_PLATFORM_WIN32)
#include "HID/Win32/DirectInputKeyboardBackend.h"


namespace aq
{
	namespace hid
	{
		namespace
		{
			/** DirectInput のキーボード状態バッファ長(DIK_ スキャンコードの定義域) */
			static constexpr uint32_t DIK_BUFFER_SIZE = 256;


			/** KeyBoardType(中立値)と DIK_ スキャンコードの対応 */
			struct KeyMapEntry
			{
				KeyBoardType key;
				uint8_t      dik;
			};


			// KeyBoardType の全要素を網羅する。要素を増やしたらここにも追加すること。
			static constexpr KeyMapEntry KEY_MAP[] =
			{
				{ KeyBoardType::Left,   DIK_LEFT   },
				{ KeyBoardType::Right,  DIK_RIGHT  },
				{ KeyBoardType::Up,     DIK_UP     },
				{ KeyBoardType::Down,   DIK_DOWN   },

				{ KeyBoardType::W,      DIK_W      },
				{ KeyBoardType::A,      DIK_A      },
				{ KeyBoardType::S,      DIK_S      },
				{ KeyBoardType::D,      DIK_D      },
				{ KeyBoardType::G,      DIK_G      },

				{ KeyBoardType::Space,  DIK_SPACE  },
				{ KeyBoardType::Enter,  DIK_RETURN },
				{ KeyBoardType::Escape, DIK_ESCAPE },

				{ KeyBoardType::Num1,   DIK_1      },
				{ KeyBoardType::Num2,   DIK_2      },
				{ KeyBoardType::Num3,   DIK_3      },
				{ KeyBoardType::Num4,   DIK_4      },
			};
		}


		DirectInputKeyboardBackend::~DirectInputKeyboardBackend()
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


		bool DirectInputKeyboardBackend::Initialize(aq::graphics::NativeWindowHandle window)
		{
			if (FAILED(DirectInput8Create(
				GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
				IID_IDirectInput8, reinterpret_cast<VOID**>(&input_), nullptr)))
				return false;

			if (FAILED(input_->CreateDevice(GUID_SysKeyboard, &device_, nullptr)))
				return false;
			if (FAILED(device_->SetDataFormat(&c_dfDIKeyboard)))
				return false;
			if (FAILED(device_->SetCooperativeLevel(static_cast<HWND>(window.handle), DISCL_NONEXCLUSIVE | DISCL_FOREGROUND)))
				return false;
			device_->Acquire();
			return true;
		}


		void DirectInputKeyboardBackend::Poll(KeyboardState& out)
		{
			out = {};
			if (!device_) { return; }

			// DIK_ スキャンコードで引ける生バッファ。取得に失敗したらフォーカス喪失とみなして
			// 再 Acquire し、それでも駄目なら入力なし(全ゼロ)として扱う。
			uint8_t keys[DIK_BUFFER_SIZE]{};
			if (FAILED(device_->GetDeviceState(sizeof(keys), keys)))
			{
				device_->Acquire();
				if (FAILED(device_->GetDeviceState(sizeof(keys), keys)))
					aq::memory::Clear(keys, sizeof(keys));
			}

			// DIK → KeyBoardType。押下ビット(0x80)はそのまま引き継ぐ。
			for (const KeyMapEntry& entry : KEY_MAP)
			{
				out.keys[static_cast<uint32_t>(entry.key)] = keys[entry.dik];
			}
		}
	}
}
#endif // AQ_PLATFORM_WIN32
