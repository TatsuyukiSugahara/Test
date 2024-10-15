#pragma once
#include "TypeInfo.h"


namespace engine
{
	namespace ecs
	{
		static constexpr uint32_t INVALID_ENTITY_INDEX = 0xffffffff;

		struct Entity
		{
			uint32_t index;
			uint32_t chunkIndex;
			//
			explicit Entity(const uint32_t idx)
				: chunkIndex(INVALID_ENTITY_INDEX)
				, index(idx)
			{
			}

			Entity(const uint32_t chunkIdx, const uint32_t idx)
				: chunkIndex(chunkIdx)
				, index(idx)
			{
			}

			bool operator==(const Entity& other) const
			{
				return index == other.index && chunkIndex == other.chunkIndex;
			}
			bool operator!=(const Entity& other) const
			{
				return !(*this == other);
			}
		};




		struct EntityHandle
		{
			uint32_t handleIndex;
			//
			EntityHandle()
				: handleIndex(INVALID_ENTITY_INDEX)
			{
			}
			EntityHandle(uint32_t index)
				: handleIndex(index)
			{
			}
			bool operator==(const EntityHandle& other) const
			{
				return handleIndex == other.handleIndex;
			}
			bool operator!=(const EntityHandle& other) const
			{
				return !(*this == other);
			}
			bool IsValid() const { return handleIndex != INVALID_ENTITY_INDEX; }

			static EntityHandle InvalidHandle() { return EntityHandle(); }
		};
	}
}