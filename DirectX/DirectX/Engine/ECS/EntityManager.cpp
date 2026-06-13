#include "../EnginePreCompile.h"
#include "EntityManager.h"



namespace engine
{
	namespace ecs
	{
Entity EntityManager::CreateEntityImpl(const Archetype& archetype)
		{
			uint32_t chunkIndex = GetChunkIndex(archetype);
			if (chunkIndex == chunkList_.size()) {
				chunkIndex = CreateChunk(archetype);
			}

			// 空きスロットを確保
			EntityID id;
			if (freeListHead_ != INVALID_ENTITY_ID) {
				id = freeListHead_;
				freeListHead_ = slots_[id].nextFree;
			} else {
				id = static_cast<EntityID>(slots_.size());
				slots_.emplace_back();
			}

			EntityLocation loc = chunkList_[chunkIndex].CreateEntity(id);
			loc.chunkIndex = chunkIndex;

			slots_[id].location   = loc;
			slots_[id].alive      = true;
			slots_[id].nextFree   = INVALID_ENTITY_ID;

			return MakeEntity(EntityHandle(id, slots_[id].generation));
		}


		Entity EntityManager::CreateEntity(const Archetype& archetype)
		{
			std::unique_lock<std::shared_mutex> lock(iterationMutex_);
			return CreateEntityImpl(archetype);
		}


		bool EntityManager::IsValid(const EntityHandle& handle) const
		{
			if (!handle.IsValid()) return false;
			if (handle.id >= static_cast<EntityID>(slots_.size())) return false;
			const auto& slot = slots_[handle.id];
			return slot.alive && slot.generation == handle.generation;
		}


		void EntityManager::DestroyEntityNow(const EntityHandle& handle)
		{
			if (!IsValid(handle)) return;

			auto& slot = slots_[handle.id];
			const EntityLocation loc = slot.location;
			const uint32_t oldIndex  = loc.index;

			// swap-remove（最後尾が oldIndex に移動）
			const EntityID swappedId = chunkList_[loc.chunkIndex].DestroyEntitySwap(loc);

			// 移動してきた entity のスロットを更新
			if (swappedId != INVALID_ENTITY_ID) {
				slots_[swappedId].location.index = oldIndex;
			}

			// スロットを無効化してフリーリストへ返却
			slot.alive      = false;
			slot.generation++;
			slot.nextFree   = freeListHead_;
			freeListHead_   = handle.id;
		}


		void EntityManager::RequestDestroyEntity(const EntityHandle& handle)
		{
			std::lock_guard<std::mutex> lock(commandMutex_);
			pendingCommands_.push_back({ [this, handle]() { DestroyEntityNow(handle); } });
		}


		void EntityManager::FlushCommands()
		{
			std::unique_lock<std::shared_mutex> lock(iterationMutex_);

			std::vector<EntityCommand> commands;
			{
				std::lock_guard<std::mutex> lock(commandMutex_);
				commands = std::move(pendingCommands_);
				pendingCommands_.clear();
			}
			for (auto& cmd : commands) {
				cmd.action();
			}
		}


		uint32_t EntityManager::CreateChunk(const Archetype& archetype)
		{
			const uint32_t chunkIndex = static_cast<uint32_t>(chunkList_.size());
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
