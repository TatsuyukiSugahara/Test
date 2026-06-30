#pragma once
#include "Chunk.h"
#include "EntityView.h"
#include <functional>
#include <utility>
#include <tuple>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>


namespace aq
{
	namespace ecs
	{
		struct EntityCommand
		{
			std::function<void()> action;
		};


		class EntityManager
		{
		private:
			std::vector<Chunk> chunkList_;

			// EntityID -> EntitySlot テーブル
			std::vector<EntitySlot> slots_;
			EntityID freeListHead_ = INVALID_ENTITY_ID;

			// 遅延コマンドバッファ（System 並列実行から安全に積める）
			std::vector<EntityCommand> pendingCommands_;
			std::mutex commandMutex_;

			// ForEach がリードロック、CreateEntity / CreateChunk / FlushCommands がライトロック。
			// ForEach 中の構造変更（chunkList_ 再確保）を実際にブロックする。
			mutable std::shared_mutex iterationMutex_;

			bool flushingCommands_  = false; // FlushCommands() の再入防止
			bool isInUserCallback_  = false; // onCreated / onAdded callback 中の即時構造変更を禁止


		private:
			friend class EntityContext;
			template <typename...> friend class EntityView;

			EntityManager() {}
			~EntityManager()
			{
				chunkList_.clear();
				slots_.clear();
			}


		public:
			template <typename ...Args>
			Entity CreateEntity()
			{
				EngineAssertMsg(!isInUserCallback_,
					"EntityManager::CreateEntity: immediate creation is forbidden inside onCreated/onAdded callback. Use RequestCreateEntity instead.");
				if (isInUserCallback_) return Entity();
				constexpr auto archetype = Archetype::Create<Args...>();
				std::unique_lock<std::shared_mutex> lock(iterationMutex_);
				Entity e = CreateEntityImpl(archetype);
				EntityLocation* loc = ResolveLocation(e.GetHandle());
				if (loc) { NewComponent<Args...>(*loc); }
				return e;
			}

			/**
			 * エンティティ生成の遅延予約。FlushCommands() で実行される。
			 * onCreated: 生成完了後に Entity を渡して呼ばれる省略可能なコールバック。
			 * callback 内では component の初期値設定のみ許可。即時 CreateEntity は禁止。
			 */
			template <typename ...Args>
			void RequestCreateEntity(std::function<void(Entity)> onCreated = nullptr)
			{
				EngineAssertMsg(!isInUserCallback_,
					"EntityManager::RequestCreateEntity: structural changes are forbidden inside onCreated/onAdded callback.");
				if (isInUserCallback_) return;
				std::lock_guard<std::mutex> lock(commandMutex_);
				pendingCommands_.push_back({
					[this, onCreated = std::move(onCreated)]() {
						constexpr auto archetype = Archetype::Create<Args...>();
						Entity e = CreateEntityImpl(archetype);
						EntityLocation* loc = ResolveLocation(e.GetHandle());
						if (loc) { NewComponent<Args...>(*loc); }
						if (onCreated) {
							ScopedFlag cbGuard(isInUserCallback_);
							onCreated(e);
						}
					}
				});
			}


			/**
			 * 実行時 TypeInfo 列から Entity を遅延生成する（Prefab 生成の核心 primitive）。
			 * ForEach / System Update 内から安全に呼べる（commandMutex_ のみ取得）。
			 * 実体生成は次の FlushCommands で行われる。
			 *
			 * types     : コンポーネント集合（依存解決済みの完全な列を渡すこと）。重複は内部で除去する。
			 * onCreated : 全コンポーネント構築後に呼ばれる。ここで deserialize 等を行う（GetComponent 有効）。
			 */
			void RequestCreateEntityFromTypes(
				std::vector<TypeInfo>        types,
				std::function<void(Entity)>  onCreated = nullptr)
			{
				EngineAssertMsg(!isInUserCallback_,
					"EntityManager::RequestCreateEntityFromTypes: structural changes are forbidden inside onCreated/onAdded callback.");
				if (isInUserCallback_) return;
				std::lock_guard<std::mutex> lock(commandMutex_);
				pendingCommands_.push_back({
					[this, types = std::move(types), onCreated = std::move(onCreated)]() mutable {
						// FlushCommands が既に iterationMutex_ を保持しているため NoLock 版を使う
						Entity e = CreateEntityFromTypesNoLock(types);
						if (e.IsValid() && onCreated) {
							ScopedFlag cbGuard(isInUserCallback_);
							onCreated(e);
						}
					}
				});
			}


			/**
			 * ハンドルの有効性確認（generation チェック付き）
			 */
			bool IsValid(const EntityHandle& handle) const;


			/**
			 * エンティティ破棄予約（遅延版）
			 * System 並列実行中でも安全に積める。flush は FlushCommands() で行う。
			 */
			void RequestDestroyEntity(const EntityHandle& handle);

			/**
			 * 溜まったコマンドを一括実行（SystemManager::Update 後に呼ぶ）
			 */
			void FlushCommands();


		public:
			/**
			 * 対象 Component を持つ Entity のクエリビューを取得する
			 * EntityView::ForEach(func) でイテレートする
			 */
			template <typename... Cs>
			EntityView<Cs...> GetView()
			{
				return EntityView<Cs...>(this, GetChunkIndices<Cs...>());
			}


		// EntityView / EntityContext からのみ使用する内部 API（friend 宣言で許可）
		// 外部から Chunk* を取得して EntityLocation ベースの低レベル操作を行うルートを
		// 一般コードに開放しない。
		private:
			uint32_t CreateChunk(const Archetype& archetype);

			uint32_t GetChunkIndex(const Archetype& archetype) const;

			// EntityView が保持する安全なインデックス列を返す
			// Chunk* ではなくインデックスを返すことで chunkList_ 再確保後の dangling を防ぐ
			std::vector<uint32_t> GetChunkIndices(const Archetype& archetype) const
			{
				std::vector<uint32_t> result;
				for (uint32_t i = 0; i < static_cast<uint32_t>(chunkList_.size()); ++i) {
					if (archetype.IsIn(chunkList_[i].GetArchetype())) {
						result.push_back(i);
					}
				}
				return result;
			}

			template <typename ...Args>
			std::vector<uint32_t> GetChunkIndices() const
			{
				constexpr auto archetype = Archetype::Create<Args...>();
				return GetChunkIndices(archetype);
			}

			// EntityView::ForEach から使用
			Chunk* GetChunkByIndex(uint32_t index)
			{
				if (index >= static_cast<uint32_t>(chunkList_.size())) return nullptr;
				return &chunkList_[index];
			}

			// EntityView::ForEach の IterationGuard が呼ぶ。
			// shared_lock を返すので RAII でロック・アンロックが完結する。
			std::shared_lock<std::shared_mutex> BeginIteration()
			{
				return std::shared_lock<std::shared_mutex>(iterationMutex_);
			}


		public:
			/**
			 * RemoveComponent コマンドを積む（FlushCommands で実行）
			 * T を持たないエンティティに対しては何もしない。
			 */
			template <typename T>
			void RemoveComponent(const EntityHandle& handle)
			{
				EngineAssertMsg(!isInUserCallback_,
					"EntityManager::RemoveComponent: structural changes are forbidden inside onCreated/onAdded callback.");
				if (isInUserCallback_) return;
				std::lock_guard<std::mutex> lock(commandMutex_);
				pendingCommands_.push_back({
					[this, handle]() {
						EntityLocation* loc = ResolveLocation(handle);
						if (!loc) return;
						RemoveComponentByLocation<T>(*loc);
					}
				});
			}


			/**
			 * AddComponent コマンドを積む（FlushCommands で実行）
			 * onAdded: 追加完了後に T* を渡して呼ばれる省略可能なコールバック
			 */
			template <typename T>
			void AddComponent(const EntityHandle& handle, std::function<void(T*)> onAdded = nullptr)
			{
				EngineAssertMsg(!isInUserCallback_,
					"EntityManager::AddComponent: structural changes are forbidden inside onCreated/onAdded callback.");
				if (isInUserCallback_) return;
				std::lock_guard<std::mutex> lock(commandMutex_);
				pendingCommands_.push_back({
					[this, handle, onAdded = std::move(onAdded)]() {
						EntityLocation* loc = ResolveLocation(handle);
						if (!loc) return;
						if (AddComponentByLocation<T>(*loc) && onAdded) {
							ScopedFlag cbGuard(isInUserCallback_);
							onAdded(GetComponentByLocation<T>(*loc));
						}
					}
				});
			}


			/**
			 * コンポーネント取得（EntityHandle 経由、安全アクセスの主経路）
			 * 対象 Archetype に T が含まれない場合は nullptr を返す
			 */
			template <typename T>
			T* GetComponent(const EntityHandle& handle)
			{
				EntityLocation* loc = ResolveLocation(handle);
				if (!loc) return nullptr;
				if (!chunkList_[loc->chunkIndex].GetArchetype().HasType<T>()) return nullptr;
				return GetComponentByLocation<T>(*loc);
			}

			/**
			 * コンポーネント取得（public Entity 経由）
			 */
			template <typename T>
			T* GetComponent(const Entity& entity)
			{
				return GetComponent<T>(entity.GetHandle());
			}


			/**
			 * EntityHandle から public Entity を生成
			 */
			Entity MakeEntity(const EntityHandle& handle)
			{
				return Entity(this, handle);
			}

		private:
			/**
			 * EntityID から Entity を生成（EntityView::ForEach 内部でのみ使用）
			 */
			Entity MakeEntity(EntityID id)
			{
				if (id >= static_cast<EntityID>(slots_.size())) return Entity();
				return Entity(this, EntityHandle(id, slots_[id].generation));
			}

			struct ScopedFlag {
				bool& flag;
				ScopedFlag(bool& f) : flag(f) { flag = true; }
				~ScopedFlag() { flag = false; }
			};

			// 外部公開なし。typed 版 CreateEntity<Args...>() からのみ呼ぶ
			Entity CreateEntity(const Archetype& archetype);

			// ロックなしで Entity を生成する内部実装。
			// 呼び出し元（CreateEntity / CreateEntity<Args...>）が iterationMutex_ を保持していること。
			Entity CreateEntityImpl(const Archetype& archetype);

			// 即時破棄（FlushCommands 内部でのみ使用）
			void DestroyEntityNow(const EntityHandle& handle);

			EntityLocation* ResolveLocation(const EntityHandle& handle)
			{
				if (!IsValid(handle)) return nullptr;
				return &slots_[handle.id].location;
			}

			template <typename T>
			T* GetComponentByLocation(const EntityLocation& loc)
			{
				return chunkList_[loc.chunkIndex].GetComponent<T>(loc);
			}

			// true: 実際に追加した / false: 既に T を持っていたため何もしなかった
			template <typename T>
			bool AddComponentByLocation(EntityLocation& loc)
			{
				if (chunkList_[loc.chunkIndex].GetArchetype().HasType<T>()) {
					return false;
				}

				auto newArchetype = chunkList_[loc.chunkIndex].GetArchetype();
				newArchetype.AddType<T>();
				auto newChunkIndex = GetChunkIndex(newArchetype);
				if (newChunkIndex == chunkList_.size()) {
					newChunkIndex = CreateChunk(newArchetype);
				}

				const EntityID id       = chunkList_[loc.chunkIndex].GetEntityID(loc.index);
				const uint32_t oldIndex = loc.index;

				auto& destChunk = chunkList_[newChunkIndex];
				const EntityID swappedId = chunkList_[loc.chunkIndex].MoveEntitySlot(loc, destChunk, id);
				loc.chunkIndex = newChunkIndex;

				if (id != INVALID_ENTITY_ID) {
					slots_[id].location = loc;
				}
				if (swappedId != INVALID_ENTITY_ID) {
					slots_[swappedId].location.index = oldIndex;
				}

				new(GetComponentByLocation<T>(loc)) T();
				return true;
			}

			template<typename... Ts>
			void NewComponent(const EntityLocation& loc)
			{
				(new(chunkList_[loc.chunkIndex].GetComponent<Ts>(loc)) Ts(), ...);
			}

			// TypeInfo 列の重複除去（Archetype::AddType は重複を弾かないため呼び出し側で行う）。
			static void DedupTypes(std::vector<TypeInfo>& types)
			{
				std::vector<TypeInfo> unique;
				unique.reserve(types.size());
				for (const TypeInfo& t : types) {
					bool dup = false;
					for (const TypeInfo& u : unique) {
						if (u == t) { dup = true; break; }
					}
					if (!dup) unique.push_back(t);
				}
				types.swap(unique);
			}

			// 実行時 TypeInfo 列から即時生成する（ロックなし）。
			// 呼び出し元が iterationMutex_ を保持していること（FlushCommands 内のコマンドが該当）。
			// dedup + 上限診断 + 全コンポーネント構築までを行う。失敗時は無効な Entity を返す。
			Entity CreateEntityFromTypesNoLock(std::vector<TypeInfo>& types)
			{
				DedupTypes(types);

				// MAX 超過は無言で切り捨てず診断エラーにする（AddType は上限超過を無視するため）
				EngineAssertMsg(types.size() <= MAX_COMPONENT_SIZE,
					"CreateEntityFromTypes: component count exceeds MAX_COMPONENT_SIZE");
				if (types.size() > MAX_COMPONENT_SIZE) return Entity();

				Archetype archetype;
				for (const TypeInfo& t : types) {
					archetype.AddType(t);
				}

				Entity e = CreateEntityImpl(archetype);
				EntityLocation* loc = ResolveLocation(e.GetHandle());
				if (!loc) return Entity();

				// 全コンポーネントを placement-new（生メモリのため triviality 無関係に構築する）
				for (const TypeInfo& t : types) {
					void* p = chunkList_[loc->chunkIndex].GetComponentByType(*loc, t);
					if (p) t.Construct(p);
				}
				return e;
			}

			// true: 実際に除去した / false: T を持っていなかったため何もしなかった
			template <typename T>
			bool RemoveComponentByLocation(EntityLocation& loc)
			{
				if (!chunkList_[loc.chunkIndex].GetArchetype().HasType<T>()) return false;

				const Archetype dstArchetype =
					chunkList_[loc.chunkIndex].GetArchetype().RemoveType(TypeInfo::Create<T>());

				auto dstChunkIndex = GetChunkIndex(dstArchetype);
				if (dstChunkIndex == static_cast<uint32_t>(chunkList_.size()))
					dstChunkIndex = CreateChunk(dstArchetype);

				const EntityID entityId = chunkList_[loc.chunkIndex].GetEntityID(loc.index);
				const uint32_t oldIndex = loc.index;

				const EntityID swappedId = chunkList_[loc.chunkIndex].MoveEntityRemoveArchetype(
					loc, chunkList_[dstChunkIndex], entityId, TypeInfo::Create<T>());

				loc.chunkIndex = dstChunkIndex;

				if (entityId != INVALID_ENTITY_ID)
					slots_[entityId].location = loc;
				if (swappedId != INVALID_ENTITY_ID)
					slots_[swappedId].location.index = oldIndex;

				return true;
			}




		};
	}
}


// Entity インライン実装（EntityManager の完全定義が必要なためここで定義する）
namespace aq
{
	namespace ecs
	{
		inline bool Entity::IsValid() const
		{
			if (!manager_) return false;
			return manager_->IsValid(handle_);
		}

		inline void Entity::Destroy() const
		{
			if (!manager_) return;
			manager_->RequestDestroyEntity(handle_);
		}

		template <typename T>
		T* Entity::GetComponent() const
		{
			if (!manager_) return nullptr;
			return manager_->GetComponent<T>(handle_);
		}

		template <typename T>
		void Entity::AddComponent(std::function<void(T*)> onAdded) const
		{
			if (!manager_) return;
			manager_->AddComponent<T>(handle_, std::move(onAdded));
		}


		// EntityView::ForEach インライン実装（EntityManager / Entity の完全定義が必要なためここで定義する）
		template <typename... Components>
		template <typename Func>
		void EntityView<Components...>::ForEach(Func&& func) const
		{
			// shared_lock を取得して ForEach 中の構造変更をブロックする。
			// RAII で例外発生時も確実にアンロックされる。
			struct IterationGuard {
				std::shared_lock<std::shared_mutex> lock;
				explicit IterationGuard(EntityManager* m) : lock(m->BeginIteration()) {}
			} guard(manager_);

			for (const uint32_t idx : chunkIndices_) {
				Chunk* chunk = manager_->GetChunkByIndex(idx);
				if (!chunk) { continue; }

				// ComponentArray を Chunk ごとに一度だけ取得する
				auto arrays = std::make_tuple(chunk->GetComponentArray<Components>()...);

				for (uint32_t i = 0; i < chunk->GetSize(); ++i) {
					const EntityID id = chunk->GetEntityID(i);
					Entity entity = manager_->MakeEntity(id);
					if (!entity.IsValid()) { continue; }

					std::apply([&](auto&... arr) {
						func(entity, &arr[i]...);
					}, arrays);
				}
			}
		}
	}
}
