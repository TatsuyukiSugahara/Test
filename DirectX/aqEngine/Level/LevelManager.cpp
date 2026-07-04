#include "aq.h"
#include "Level/LevelManager.h"
#include "Level/LevelRegistry.h"
#include "Level/LevelComponents.h"
#include "ECS/EntityContext.h"
#include "ECS/Prefab.h"


namespace aq
{
	namespace level
	{
		LevelManager& LevelManager::Get()
		{
			static LevelManager instance;
			return instance;
		}


		LevelId LevelManager::AllocateSlot(std::string path, std::shared_ptr<const LevelData> data, LevelId parent)
		{
			uint32_t index;
			if (!freeList_.empty())
			{
				index = freeList_.back();
				freeList_.pop_back();
			}
			else
			{
				index = static_cast<uint32_t>(slots_.size());
				slots_.emplace_back();
			}

			LevelSlot& slot = slots_[index];
			slot.path   = std::move(path);
			slot.data   = std::move(data);
			slot.parent = parent;
			slot.children.clear();
			slot.loaded = true;

			LevelId id;
			id.index      = index;
			id.generation = slot.generation;

			// 親 Level の children に登録（Unload カスケード用・L3/L4）。
			if (parent.IsValid() && parent.index < slots_.size() &&
			    slots_[parent.index].generation == parent.generation)
			{
				slots_[parent.index].children.push_back(index);
			}
			return id;
		}


		void LevelManager::InstantiateEntities(const std::shared_ptr<const LevelData>& data, LevelId levelId)
		{
			// 設計 §7 寿命ルール: this/参照ではなく shared_ptr<const LevelData> と LevelId(値) を捕獲する。
			ecs::EntityContext& ctx = ecs::EntityContext::Get();
			ctx.RequestDeferredBuild(
				[data, levelId](const ecs::EntityManager::DeferredCreateFn& rawCreate)
				{
					// 各ノードのアーキタイプへ LevelMemberComponent を注入する create ラッパ。
					ecs::EntityManager::DeferredCreateFn create =
						[&rawCreate](std::vector<ecs::TypeInfo> types) -> ecs::Entity
						{
							types.push_back(ecs::TypeInfo::Create<LevelMemberComponent>());
							return rawCreate(std::move(types));
						};

					// 生成・deserialize 完了直後に levelId を差す。
					auto stamp = [levelId](ecs::Entity e, const ecs::PrefabNodeData&)
					{
						if (auto* member = e.GetComponent<LevelMemberComponent>())
							member->levelId = levelId;
					};

					for (const ecs::PrefabNodeData& node : data->entities)
						ecs::InstantiatePrefabTree(node, ecs::EntityHandle(), create, stamp);
				});
		}


		LevelId LevelManager::Load(std::string_view pathOrId, LevelId parent)
		{
			std::shared_ptr<const LevelData> data = LevelRegistry::Get().Load(pathOrId);
			if (!data)
			{
				EngineAssertMsg(false, "LevelManager::Load: failed to resolve level data");
				return LevelId();
			}

			const LevelId id = AllocateSlot(LevelRegistry::Normalize(pathOrId), data, parent);
			InstantiateEntities(data, id);
			return id;
		}


		bool LevelManager::IsLoaded(LevelId id) const
		{
			if (!id.IsValid() || id.index >= slots_.size()) return false;
			const LevelSlot& slot = slots_[id.index];
			return slot.loaded && slot.generation == id.generation;
		}


		LevelId LevelManager::Find(std::string_view pathOrId) const
		{
			const std::string key = LevelRegistry::Normalize(pathOrId);
			for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
			{
				const LevelSlot& slot = slots_[i];
				if (slot.loaded && slot.path == key)
				{
					LevelId id;
					id.index      = i;
					id.generation = slot.generation;
					return id;
				}
			}
			return LevelId();
		}
	}
}
