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

			uint32_t entityHandleIndex = entityHandles_.size();
			entityHandles_.insert(std::pair<uint32_t, Entity>(entityHandleIndex, entity));

			return entity;
		}


		void EntityManager::DestroyEntity(const Entity& entity)
		{
			DestroyEntity(GetHandle(entity));
		}


		void EntityManager::DestroyEntity(const EntityHandle& handle)
		{
			if (!IsValid(handle)) {
				return;
			}
			const auto& it = entityHandles_.find(handle.handleIndex);
			auto entity = it->second;

			chunkList_[entity.chunkIndex].DestroyEntity(entity);

			entityHandles_.erase(handle.handleIndex);
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