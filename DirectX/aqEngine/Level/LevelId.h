#pragma once
#include <cstdint>


namespace aq
{
	namespace level
	{
		static constexpr uint32_t INVALID_LEVEL_INDEX = 0xffffffff;


		// ロード済み Level を一意に識別する世代付きハンドル。
		// ecs::EntityHandle と同じ設計で、破棄済み Level スロットが再利用されても
		// generation 不一致で stale なアンロード/参照を弾く。
		struct LevelId
		{
			uint32_t index      = INVALID_LEVEL_INDEX;
			uint32_t generation = 0;

			bool IsValid() const { return index != INVALID_LEVEL_INDEX; }

			bool operator==(const LevelId& other) const
			{
				return index == other.index && generation == other.generation;
			}
			bool operator!=(const LevelId& other) const { return !(*this == other); }
		};
	}
}
