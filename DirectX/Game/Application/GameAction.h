#pragma once
#include <cstdint>

namespace app
{
	enum class GameAction : uint32_t
	{
		MoveLeft,
		MoveRight,
		MoveForward,
		MoveBackward,
		Move,           // 左スティック
		Look,           // 右スティック
	};
}
