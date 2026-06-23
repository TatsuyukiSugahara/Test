#pragma once
#include <cstdint>

namespace aq
{
	namespace ui
	{
		enum class UIAction : uint8_t
		{
			Submit,  // Space / Enter / Pad A
			Cancel,  // Escape / Pad B
			Up,
			Down,
			Left,
			Right,
		};

	} // namespace ui
} // namespace aq
