#pragma once
#include "Chunk.h"
#include <vector>
#include <memory>
#include <unordered_map>


namespace engine
{
	namespace ecs
	{
		class EntityManager
		{
		private:
			std::vector<Chunk> chunkList_;
			std::unordered_map<uint32_t, Entity> entityHandles_;


		private:
			EntityManager() {}
			~EntityManager()
			{
				chunkList_.clear();
				entityHandles_.clear();
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
				auto entity = CreateEntity(archetype);
				NewComponent<Args...>(entity);
				return entity;
			}


			/**
			 * エンティティ破棄
			 */
			void DestroyEntity(const Entity& entity);
			void DestroyEntity(const EntityHandle& handle);


		public:
			inline EntityHandle GetHandle(const Entity& entity)
			{
				for (auto& it : entityHandles_) {
					if (it.second == entity) {
						return EntityHandle(it.first);
					}
				}
				// 見つからなかった
				return EntityHandle();
			}


			inline bool IsValid(const EntityHandle& handle) const
			{
				const auto& it = entityHandles_.find(handle.handleIndex);
				if (it != entityHandles_.end()) {
					return true;
				}
				return false;
			}




		public:
			/**
			 * チャンク生成
			 */
			uint32_t CreateChunk(const Archetype& archetype);

			template <typename ...Args>
			uint32_t CreateChunk()
			{
				constexpr auto archetype = Archetype::Create<Args...>();
				return CreateChunk(archetype);
			}

			/**
			 * 指定タイプのチャンクインデックス取得
			 */
			uint32_t GetChunkIndex(const Archetype& archetype) const;


			/**
			 * チャンクリスト取得
			 */
			std::vector<Chunk*> GetChunkList(const Archetype& archetype);

			template <typename ...Args>
			std::vector<Chunk*> GetChunkList()
			{
				constexpr auto archetype = Archetype::Create<Args...>();
				return GetChunkList(archetype);
			}


		public:
			/**
			 * コンポーネント追加
			 */
			template <typename T>
			void AddComponent(Entity& entity)
			{
				auto newArchetype = chunkList_[entity.chunkIndex].GetArchetype();
				newArchetype.AddType<T>();
				auto newChunkIndex = GetChunkIndex(newArchetype);
				if (newChunkIndex == chunkList_.size()) {
					newChunkIndex = CreateChunk(newArchetype);
				}

				// Entity情報が変わるので一旦取得
				EntityHandle entityHandle = GetHandle(entity);

				auto& chunk = chunkList_[newChunkIndex];
				chunkList_[entity.chunkIndex].MoveEntity(entity, chunk);
				entity.chunkIndex = newChunkIndex;

				// EntityHandleのEntity情報変更
				const auto& it = entityHandles_.find(entityHandle.handleIndex);
				if (it != entityHandles_.end()) {
					it->second = entity;
				}
			}
			

			/**
			 * コンポーネント取得
			 */
			template <typename T>
			T* GetComponent(const Entity& entity)
			{
				return chunkList_[entity.chunkIndex].GetComponent<T>(entity);
			}
			template <typename T>
			T* GetComponent(const EntityHandle& handle)
			{
				const auto& it = entityHandles_.find(handle.handleIndex);
				if (it != entityHandles_.end()) {
					return GetComponent<T>(it->second);
				}
				return nullptr;
			}


			/**
			 * コンポーネントにデータ設定
			 */
			template <typename T>
			void SetComponent(const Entity& entity)
			{
				chunkList_[entity.chunkIndex].SetComponent<T>(entity);
			}




			/**
			 * Entity生成時にコンストラクタを呼ぶため配置New
			 */
		private:
#define newComponent(T) new(chunkList_[entity.chunkIndex].GetComponent<T>(entity)) T();

			template<typename T1>
			void NewComponent(const Entity& entity)
			{
				newComponent(T1);
			}
			template<typename T1, typename T2>
			void NewComponent(const Entity& entity)
			{
				newComponent(T1);
				newComponent(T2);
			}
			template<typename T1, typename T2, typename T3>
			void NewComponent(const Entity& entity)
			{
				newComponent(T1);
				newComponent(T2);
				newComponent(T3);
			}
			template<typename T1, typename T2, typename T3, typename T4>
			void NewComponent(const Entity& entity)
			{
				newComponent(T1);
				newComponent(T2);
				newComponent(T3);
				newComponent(T4);
			}
			template<typename T1, typename T2, typename T3, typename T4, typename T5>
			void NewComponent(const Entity& entity)
			{
				newComponent(T1);
				newComponent(T2);
				newComponent(T3);
				newComponent(T4);
				newComponent(T5);
			}
			template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
			void NewComponent(const Entity& entity)
			{
				newComponent(T1);
				newComponent(T2);
				newComponent(T3);
				newComponent(T4);
				newComponent(T5);
				newComponent(T6);
			}

#undef newComponent




			/**
			 * インスタンス
			 */
		private:
			static EntityManager* instance_;


		public:
			static void Initialize()
			{
				if (instance_ == nullptr) {
					instance_ = new EntityManager();
				}
			}
			static EntityManager& Get()
			{
				return *instance_;
			}
			static void Finalize()
			{
				if (instance_) {
					delete instance_;
					instance_ = nullptr;
				}
			}
		};
	}
}