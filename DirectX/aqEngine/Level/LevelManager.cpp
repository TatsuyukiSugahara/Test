#include "aq.h"
#include "Level/LevelManager.h"
#include "Level/LevelRegistry.h"
#include "Level/LevelComponents.h"
#include "ECS/ECS.h"            // ecs::Foreach
#include "ECS/EntityContext.h"
#include "ECS/Prefab.h"
#include <algorithm>


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


		void LevelManager::SetStartupLevel(std::string_view pathOrId)
		{
			startupPath_ = LevelRegistry::Normalize(pathOrId);
		}


		LevelId LevelManager::LoadStartup()
		{
			if (startupPath_.empty())
			{
				EngineAssertMsg(false, "LevelManager::LoadStartup: startup level is not set");
				return LevelId();
			}
			return Load(startupPath_);
		}


		LevelId LevelManager::Load(std::string_view pathOrId, LevelId parent)
		{
			const std::string key = LevelRegistry::Normalize(pathOrId);

			// 循環サブLevel 参照の検出（A→B→A など）。無限ロードを防ぐ。
			for (const std::string& s : loadStack_)
			{
				if (s == key)
				{
					EnginePrintf("[Level] circular subLevel reference detected: %s\n", key.c_str());
					return LevelId();
				}
			}

			std::shared_ptr<const LevelData> data = LevelRegistry::Get().Load(key);
			if (!data)
			{
				EngineAssertMsg(false, "LevelManager::Load: failed to resolve level data");
				return LevelId();
			}

			const LevelId id = AllocateSlot(key, data, parent);
			InstantiateEntities(data, id);

			// loadOnStart のサブLevel を再帰ロードする（親=このLevel。children は AllocateSlot が登録し、
			// Unload は L3 のカスケードで一括破棄される）。false のサブは休眠（後から手動/自動ストリーム）。
			loadStack_.push_back(key);
			for (const SubLevelRef& sub : data->subLevels)
			{
				if (sub.loadOnStart) Load(sub.path, id);
			}
			loadStack_.pop_back();

			return id;
		}


		LevelId LevelManager::MakeId(uint32_t index) const
		{
			LevelId id;
			id.index      = index;
			id.generation = slots_[index].generation;
			return id;
		}


		void LevelManager::CollectSubtree(LevelId id, std::vector<LevelId>& out) const
		{
			if (!IsLoaded(id)) return;
			out.push_back(id);
			for (const uint32_t childIndex : slots_[id.index].children)
			{
				if (childIndex < slots_.size() && slots_[childIndex].loaded)
					CollectSubtree(MakeId(childIndex), out);
			}
		}


		void LevelManager::FreeSlot(LevelId id)
		{
			if (id.index >= slots_.size()) return;
			LevelSlot& slot = slots_[id.index];
			if (!slot.loaded || slot.generation != id.generation) return;

			slot.loaded = false;
			slot.data.reset();
			slot.path.clear();
			slot.children.clear();
			slot.parent = LevelId();
			++slot.generation;                       // stale 化（再利用時に旧 LevelId を弾く）
			freeList_.push_back(id.index);
		}


		void LevelManager::DetachFromParent(LevelId id)
		{
			if (id.index >= slots_.size()) return;
			const LevelId parent = slots_[id.index].parent;
			if (!parent.IsValid() || parent.index >= slots_.size()) return;
			if (slots_[parent.index].generation != parent.generation) return;

			auto& kids = slots_[parent.index].children;
			kids.erase(std::remove(kids.begin(), kids.end(), id.index), kids.end());
		}


		void LevelManager::Unload(LevelId id)
		{
			if (!IsLoaded(id)) return;

			// 対象 + 全子孫の LevelId を先に集める（この後 slot を stale 化するため）。
			std::vector<LevelId> targets;
			CollectSubtree(id, targets);

			// 集合に含まれる levelId の Entity を遅延破棄する。
			// member->levelId の値はこの Foreach 内で読むので、後で slot を stale 化しても判定は正しい。
			ecs::Foreach<LevelMemberComponent>(
				[&targets](const ecs::Entity& entity, LevelMemberComponent* member)
				{
					for (const LevelId& t : targets)
					{
						if (member->levelId == t) { entity.Destroy(); break; }
					}
				});

			// Level ツリーから切り離し、slot を解放する（親から先に外してから全 slot を free）。
			DetachFromParent(id);
			for (const LevelId& t : targets) FreeSlot(t);
		}


		void LevelManager::UnloadAll()
		{
			// root（親を持たない）ロード済み Level を集めてから Unload する
			//（Unload 中に slots_/freeList_ を変更するため、収集と実行を分ける）。
			std::vector<LevelId> roots;
			for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
			{
				const LevelSlot& slot = slots_[i];
				if (slot.loaded && !slot.parent.IsValid()) roots.push_back(MakeId(i));
			}
			for (const LevelId& r : roots) Unload(r);
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
