#include "aq.h"
#include "Level/LevelManager.h"
#include "Level/LevelRegistry.h"
#include "Level/LevelComponents.h"
#include "ECS/EntityContext.h"
#include "ECS/Prefab.h"
#include "ECS/PrefabRegistry.h" // ReloadAll でキャッシュを捨てる
#include <filesystem>


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
			slot.path     = std::move(path);
			slot.data     = std::move(data);
			slot.parent   = parent;
			slot.children.clear();
			slot.loaded   = true;
			slot.fileTime = 0;

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
			if (IsFileKey(key)) slots_[id.index].fileTime = QueryFileTime(key);   // D2: 変更検知の基準時刻
			InstantiateEntities(data, id);

			// loadOnStart のサブLevel を再帰ロードする（親=このLevel。children は AllocateSlot が登録し、
			// Unload は L3 のカスケードで一括破棄される）。false のサブは休眠（後から手動/自動ストリーム）。
			loadStack_.push_back(key);
			for (const SubLevelRef& sub : data->subLevels)
			{
				if (!sub.loadOnStart) continue;
				if (sub.inlineData) LoadInline(sub.inlineData, id);   // インライン定義
				else                Load(sub.path, id);               // 外部ファイル参照
			}
			loadStack_.pop_back();

			return id;
		}


		LevelId LevelManager::LoadInline(std::shared_ptr<const LevelData> data, LevelId parent)
		{
			if (!data) return LevelId();

			const LevelId id = AllocateSlot("<inline>", data, parent);
			InstantiateEntities(data, id);

			// インラインのサブLevel も再帰（インライン / ファイル参照 両対応）。
			for (const SubLevelRef& sub : data->subLevels)
			{
				if (!sub.loadOnStart) continue;
				if (sub.inlineData) LoadInline(sub.inlineData, id);
				else                Load(sub.path, id);
			}
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


		bool LevelManager::IsFileKey(const std::string& key)
		{
			// "<inline>" / "<level-editor-preview-N>" などの合成キーはファイルではない。
			return !key.empty() && key.front() != '<';
		}


		int64_t LevelManager::QueryFileTime(const std::string& path) const
		{
			std::error_code ec;
			const auto t = std::filesystem::last_write_time(path, ec);
			if (ec) return 0;
			return static_cast<int64_t>(t.time_since_epoch().count());
		}


		void LevelManager::ReloadAll()
		{
			// 参照キャッシュを捨てて参照先ファイルを読み直す。
			ecs::PrefabRegistry::Get().Clear();
			LevelRegistry::Get().Clear();

			// ファイル由来の root Level を収集してから作り直す（reload 中に slots_ が変わるため収集と実行を分離）。
			std::vector<std::pair<std::string, LevelId>> roots;
			for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
			{
				const LevelSlot& slot = slots_[i];
				if (slot.loaded && !slot.parent.IsValid() && IsFileKey(slot.path))
					roots.push_back({ slot.path, MakeId(i) });
			}
			for (const auto& r : roots) { if (IsLoaded(r.second)) Unload(r.second); }
			for (const auto& r : roots) { Load(r.first); }
		}


		void LevelManager::SetAutoReload(const bool enabled)
		{
			autoReload_ = enabled;
			pollTimer_  = 0.0f;
		}


		bool LevelManager::IsAutoReload() const
		{
			return autoReload_;
		}


		void LevelManager::Tick(const float dt)
		{
			// §15: 非同期ロードを 1 フレーム分進める（安全点＝EntityContext::Update 後で呼ばれる前提）。
			ProcessAsyncLoads();

			// D2: ファイル変更の自動監視。
			if (!autoReload_) return;
			constexpr float POLL_INTERVAL = 0.5f;   // 監視の間引き（秒）
			pollTimer_ += dt;
			if (pollTimer_ < POLL_INTERVAL) return;
			pollTimer_ = 0.0f;
			PollFileChanges();
		}


		void LevelManager::PollFileChanges()
		{
			// ファイル由来 root の mtime を確認し、変わっていた Level を作り直す。
			std::vector<std::pair<std::string, LevelId>> changed;
			for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i)
			{
				const LevelSlot& slot = slots_[i];
				if (!slot.loaded || slot.parent.IsValid() || !IsFileKey(slot.path)) continue;
				const int64_t now = QueryFileTime(slot.path);
				if (now != 0 && now != slot.fileTime) changed.push_back({ slot.path, MakeId(i) });
			}
			if (changed.empty()) return;

			// 変更を検知したら参照先（プレハブ含む）も読み直すためキャッシュを捨てる。
			LevelRegistry::Get().Clear();
			ecs::PrefabRegistry::Get().Clear();
			for (const auto& c : changed) { if (IsLoaded(c.second)) Unload(c.second); }
			for (const auto& c : changed) { Load(c.first); }
		}


		LevelLoadHandle LevelManager::LoadAsync(std::string_view pathOrId, LevelId parent, const uint32_t entitiesPerFrame)
		{
			const std::string key = LevelRegistry::Normalize(pathOrId);
			std::shared_ptr<const LevelData> data = LevelRegistry::Get().Load(key);
			if (!data)
			{
				EngineAssertMsg(false, "LevelManager::LoadAsync: failed to resolve level data");
				return LevelLoadHandle();
			}

			const LevelId id = AllocateSlot(key, data, parent);
			if (IsFileKey(key)) slots_[id.index].fileTime = QueryFileTime(key);

			AsyncLoad load;
			load.entitiesPerFrame = (entitiesPerFrame > 0) ? entitiesPerFrame : 1;
			load.progress         = std::make_shared<LevelLoadProgress>();
			load.progress->id     = id;

			// フォレスト（+ loadOnStart サブLevel）を BuildJob 列にフラット化する。
			FlattenLevel(data, id, load, 0);
			load.progress->total = static_cast<uint32_t>(load.jobs.size());

			LevelLoadHandle handle(load.progress);
			asyncLoads_.push_back(std::move(load));
			return handle;
		}


		void LevelManager::FlattenNode(const ecs::PrefabNodeData& node, LevelId levelId, const int32_t parentJobIndex, AsyncLoad& load)
		{
			const int32_t myIndex = static_cast<int32_t>(load.jobs.size());
			BuildJob job;
			job.node           = &node;
			job.parentJobIndex = parentJobIndex;
			job.levelId        = levelId;
			load.jobs.push_back(job);

			for (const ecs::PrefabNodeData& child : node.children)
				FlattenNode(child, levelId, myIndex, load);
		}


		void LevelManager::FlattenLevel(const std::shared_ptr<const LevelData>& data, LevelId levelId, AsyncLoad& load, const int depth)
		{
			constexpr int MAX_DEPTH = 32;
			if (!data || depth >= MAX_DEPTH) return;

			load.keepAlive.push_back(data);   // job.node ポインタの生存を保証する

			for (const ecs::PrefabNodeData& node : data->entities)
				FlattenNode(node, levelId, -1, load);

			// loadOnStart サブLevel も同じジョブ列に含める（各エンティティは自身の levelId を持つ）。
			for (const SubLevelRef& sub : data->subLevels)
			{
				if (!sub.loadOnStart) continue;
				if (sub.inlineData)
				{
					const LevelId childId = AllocateSlot("<inline>", sub.inlineData, levelId);
					FlattenLevel(sub.inlineData, childId, load, depth + 1);
				}
				else
				{
					std::shared_ptr<const LevelData> subData = LevelRegistry::Get().Load(sub.path);
					if (subData)
					{
						const LevelId childId = AllocateSlot(sub.path, subData, levelId);
						if (IsFileKey(sub.path)) slots_[childId.index].fileTime = QueryFileTime(sub.path);
						FlattenLevel(subData, childId, load, depth + 1);
					}
				}
			}
		}


		void LevelManager::ProcessAsyncLoads()
		{
			if (asyncLoads_.empty()) return;

			// 計測: このフレームで生成した entity 数と所要時間。どこで時間を食っているかの裏取り用。
			const auto profStart      = std::chrono::steady_clock::now();
			uint32_t   builtThisFrame = 0;

			for (AsyncLoad& load : asyncLoads_)
			{
				uint32_t budget = load.entitiesPerFrame;
				while (budget > 0 && load.cursor < load.jobs.size())
				{
					BuildJob&     job        = load.jobs[load.cursor];
					const LevelId jobLevelId = job.levelId;

					// LevelMemberComponent を注入する即時 create（安全点なので CreateEntityFromTypes を使える）。
					const auto create = [](std::vector<ecs::TypeInfo> types) -> ecs::Entity
					{
						types.push_back(ecs::TypeInfo::Create<LevelMemberComponent>());
						return ecs::EntityContext::Get().CreateEntityFromTypes(std::move(types));
					};
					const auto stamp = [jobLevelId](ecs::Entity e, const ecs::PrefabNodeData&)
					{
						if (auto* member = e.GetComponent<LevelMemberComponent>()) member->levelId = jobLevelId;
					};

					const ecs::EntityHandle parentHandle =
						(job.parentJobIndex >= 0) ? load.jobs[job.parentJobIndex].handle : ecs::EntityHandle();

					const ecs::Entity entity = ecs::InstantiatePrefabNode(*job.node, parentHandle, create, stamp);
					job.handle = entity.GetHandle();

					++load.cursor;
					++load.progress->built;
					++builtThisFrame;
					--budget;
				}
				if (load.cursor >= load.jobs.size()) load.progress->done = true;
			}

			if (builtThisFrame > 0)
			{
				const double ms = std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - profStart).count();
				EnginePrintf("[LoadProf] entity build: %u ent, %.2f ms\n", builtThisFrame, ms);
			}

			// 完了したロードを除去する。
			asyncLoads_.erase(
				std::remove_if(asyncLoads_.begin(), asyncLoads_.end(),
					[](const AsyncLoad& a) { return a.progress->done; }),
				asyncLoads_.end());
		}
	}
}
