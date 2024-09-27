#include "EntityManager.h"



namespace engine
{
	namespace ecs
	{
		EntityManager* EntityManager::instance_ = nullptr;


		Entity EntityManager::CreateEntity(const Archetype& archetype)
		{
			uint32_t chunkIndex = GetChunkIndex(archetype);
			if (chunkIndex == chunkList_.size()) {
				chunkIndex = CreateChunk(archetype);
			}

			auto entity = chunkList_[chunkIndex].CreateEntity();
			entity.chunkIndex = chunkIndex;

			return entity;
		}


		void EntityManager::DestroyEntity(const Entity& entity)
		{
			chunkList_[entity.chunkIndex].DestroyEntity(entity);
		}


		uint32_t EntityManager::CreateChunk(const Archetype& archetype)
		{
			const uint32_t chunkIndex = GetChunkIndex(archetype);

			chunkList_.push_back(Chunk::Create(archetype));
			return chunkIndex;
		}

		
		uint32_t EntityManager::GetChunkIndex(const Archetype& archetype) const
		{
			auto chunkIndex = 0;
			for (auto& chunk : chunkList_)
			{
				if (chunk.GetArchetype() == archetype) {
					return chunkIndex;
				}
				++chunkIndex;
			}
			return chunkIndex;
		}


		std::vector<Chunk*> EntityManager::GetChunkList(const Archetype& archetype)
		{
			std::vector<Chunk*> result;
			result.reserve(4);
			for (auto& chunk : chunkList_) {
				if (archetype.IsIn(chunk.GetArchetype())) {
					result.push_back(&chunk);
				}
			}
			return result;
		}
	}
}