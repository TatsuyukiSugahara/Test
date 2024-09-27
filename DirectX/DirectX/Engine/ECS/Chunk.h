#pragma once
#include "ComponentArray.h"
#include "Archetype.h"
#include "Entity.h"
#include <memory>


namespace engine
{
	namespace ecs
	{
		class Chunk
		{
		private:
			Archetype archetype_;
			std::unique_ptr<uint8_t[]> begin_;
			/** TODO:特別に初期化 */
			uint32_t size_ = 0;
			uint32_t capacity_ = 1;




		public:
			bool operator==(const Chunk& other) const
			{
				return archetype_ == other.archetype_;
			}
			bool operator!=(const Chunk& other) const
			{
				return archetype_ != other.archetype_;
			}


			/**
			 * 現在のSizeでCapacityを詰める
			 */
			void Fit() { ResetMemory(size_); }


			/**
			 * Otherを自分のチャンクにマージ
			 */
			void Marge(Chunk&& other);


			/**
			 * エンティティを生成
			 */
			Entity CreateEntity();


			/**
			 * エンティティを削除
			 */
			void DestroyEntity(const Entity& entity);


			/**
			 * エンティティを他チャンクに移動
			 */
			void MoveEntity(Entity& entity, Chunk& other);


			/**
			 * コンポーネント追加
			 */
			template <typename ...Args>
			Entity AddComponent(const Args&... value)
			{
				if (capacity_ == size_) {
					ResetMemory(capacity_ * 2);
				}

				const auto entity = Entity(size_);

				AddComponentImp(value...);
				++size_;
				return entity;
			}

			
			/**
			 * コンポーネント取得
			 */
			template <typename T>
			T* GetComponent(const Entity& entity)
			{
				if (entity.index >= size_) {
					// EngineAssert(false);
					return nullptr;
				}

				using TType = std::remove_const_t<std::remove_reference_t<T>>;
				const auto offset = archetype_.GetOffset<TType>() * capacity_;
				const auto currentIndex = sizeof(TType) * entity.index;
				return reinterpret_cast<T*>(begin_.get() + offset + currentIndex);
			}

			
			/**
			 * コンポーネントにデータ設定
			 */
			template <typename T>
			void SetComponent(const Entity& entity, const T& component)
			{
				using TType = std::remove_const_t<std::remove_reference_t<T>>;
				auto* p = GetComponent<T>(entity);
				memcpy(p, &component, sizeof(TType));
			}


			/**
			 * データ取得
			 */
			template <class T>
			ComponentArray<T> GetComponentArray()
			{
				using TType = std::remove_const_t<std::remove_reference_t<T>>;
				auto offset = archetype_.GetOffset<TType>() * capacity_;
				return ComponentArray<T>(reinterpret_cast<TType*>(begin_.get() + offset), size_);
			}


			/**
			 * Archetypeを取得
			 */
			const Archetype& GetArchetype() const
			{
				return archetype_;
			}


			/**
			 * Chunkに登録さているデータの数を取得
			 */
			uint32_t GetSize() const { return size_; }




		private:
			/**
			 * チャンクのメモリーを再アロケート
			 */
			void ResetMemory(uint32_t capacity);


		private:
			template <typename Head, typename ...Types>
			void AddComponentImpl(Head& head, Types&&... type)
			{
				using HeadType = std::remove_const_t<std::remove_reference_t<Head>>;
				const auto offset = archetype_.GetOffset<HeadType>() * capacity_;
				const auto currentIndex = sizeof(HeadType) * size_;
				memcpy(begin_.get() + offset + currentIndex, &head, sizeof(HeadType));
				if constexpr (sizeof...(Types) > 0) {
					AddComponentImpl(type...);
				}
			}




		public:
			template <typename ...Args>
			static Chunk Create(const uint32_t capacity = 1)
			{
				constexpr auto archetype = Archetype::Create<Args...>();
				return Create(archetype, capacity);
			}


			static Chunk Create(const Archetype& archetype, uint32_t capacity = 1);
		};
	}
}