#pragma once
#include "TypeInfo.h"


namespace engine
{
	namespace ecs
	{
		struct Entity
		{
			uint32_t index;
			uint32_t chunkIndex;
			//
			explicit Entity(const uint32_t idx)
				: chunkIndex(0xffffffff)
				, index(idx)
			{
			}

			Entity(const uint32_t chunkIdx, const uint32_t idx)
				: chunkIndex(chunkIdx)
				, index(idx)
			{
			}
		};
	}
}