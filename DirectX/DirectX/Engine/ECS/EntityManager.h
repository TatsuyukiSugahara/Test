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


namespace engine
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


		private:
			friend class EntityContext;

			EntityManager() {}
			~EntityManager()
			{
				chunkList_.clear();
				slots_.clear();
			}


		public:
			/**
			 * エンティティ生成
			 */
			Entity CreateEntity(const Archetype& archetype);

			template <typename ...Args>
			Entity CreateEntity()
			{
				constexpr auto archetype = Archetype::Create<Args...>();
				std::unique_lock<std::shared_mutex> lock(iterationMutex_);
				Entity e = CreateEntityImpl(archetype);
				EntityLocation* loc = ResolveLocation(e.GetHandle());
				if (loc) { NewComponent<Args...>(*loc); }
				return e;
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


		// ECS.h の Foreach / Query からのみ使用する内部 API
		// 外部から Chunk* を取得して EntityLocation ベースの低レベル操作を行うルートを
		// 一般コードに開放しないための分離。将来的には非公開化を検討する。
		public:
			uint32_t CreateChunk(const Archetype& archetype);

			template <typename ...Args>
			uint32_t CreateChunk()
			{
				constexpr auto archetype = Archetype::Create<Args...>();
				return CreateChunk(archetype);
			}

			uint32_t GetChunkIndex(const Archetype& archetype) const;

			std::vector<Chunk*> GetChunkList(const Archetype& archetype);

			template <typename ...Args>
			std::vector<Chunk*> GetChunkList()
			{
				constexpr auto archetype = Archetype::Create<Args...>();
				return GetChunkList(archetype);
			}

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
			 * AddComponent コマンドを積む（FlushCommands で実行）
			 * onAdded: 追加完了後に T* を渡して呼ばれる省略可能なコールバック
			 */
			template <typename T>
			void AddComponent(const EntityHandle& handle, std::function<void(T*)> onAdded = nullptr)
			{
				std::lock_guard<std::mutex> lock(commandMutex_);
				pendingCommands_.push_back({
					[this, handle, onAdded = std::move(onAdded)]() {
						EntityLocation* loc = ResolveLocation(handle);
						if (!loc) return;
						if (AddComponentByLocation<T>(*loc) && onAdded) {
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

			/**
			 * EntityID から public Entity を生成（Foreach 内部で使用）
			 */
			Entity MakeEntity(EntityID id)
			{
				if (id >= static_cast<EntityID>(slots_.size())) return Entity();
				return Entity(this, EntityHandle(id, slots_[id].generation));
			}


		private:
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




		};
	}
}


// Entity インライン実装（EntityManager の完全定義が必要なためここで定義する）
namespace engine
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
