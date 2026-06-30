#pragma once
#include <cstdint>


namespace aq
{
	namespace sound
	{
		// 世代付きインデックスハンドル（§2.4）。
		// 実体（ボイス/ソース）はエンジンがプール所有し再利用する。再生終了後に
		// 触れても世代不一致で安全に no-op になる（dangling 回避）。
		struct SoundHandle
		{
			static constexpr uint32_t INVALID_INDEX = ~0u;

			uint32_t index      = INVALID_INDEX;
			uint32_t generation = 0;

			bool IsValid() const { return index != INVALID_INDEX; }

			bool operator==(const SoundHandle& rhs) const
			{
				return index == rhs.index && generation == rhs.generation;
			}
		};


		// 3D 発音体ハンドル（§2.4）。ECS コンポーネントはこれを保持する。
		struct SoundSourceHandle
		{
			static constexpr uint32_t INVALID_INDEX = ~0u;

			uint32_t index      = INVALID_INDEX;
			uint32_t generation = 0;

			bool IsValid() const { return index != INVALID_INDEX; }

			bool operator==(const SoundSourceHandle& rhs) const
			{
				return index == rhs.index && generation == rhs.generation;
			}
		};
	}
}
