#pragma once
#include "Input.h"

namespace engine
{
	namespace hid
	{
		struct InputBinding
		{
			enum class Type : uint8_t { Key, MouseButton, PadButton };
			Type     type;
			uint32_t code;
			uint32_t padIndex;
		};

		inline InputBinding BindKey  (KeyBoardType key)                  { return { InputBinding::Type::Key,         static_cast<uint32_t>(key), 0        }; }
		inline InputBinding BindMouse(MouseButton btn)                   { return { InputBinding::Type::MouseButton,  static_cast<uint32_t>(btn), 0        }; }
		inline InputBinding BindPad  (PadButton btn, uint32_t padIdx=0) { return { InputBinding::Type::PadButton,   static_cast<uint32_t>(btn), padIdx   }; }

		// アナログスティック用バインディング
		struct StickBinding
		{
			PadAxis  axisX;
			PadAxis  axisY;
			uint32_t padIndex = 0;
		};

		inline StickBinding BindLStick(uint32_t padIndex = 0) { return { PadAxis::LX, PadAxis::LY, padIndex }; }
		inline StickBinding BindRStick(uint32_t padIndex = 0) { return { PadAxis::RX, PadAxis::RY, padIndex }; }
	}
}
