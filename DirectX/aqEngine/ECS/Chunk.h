#pragma once
#include "ComponentArray.h"
#include "Archetype.h"
#include "Entity.h"
#include <cassert>
#include <memory>
#include <new>
#include <vector>


namespace aq
{
	namespace ecs
	{
		class Chunk
		{
		private:
			// over-aligned component（alignas(N) で N > __STDCPP_DEFAULT_NEW_ALIGNMENT__）に対応した
			// カスタムデリータ。AllocBuffer で確保したブロックを正しく解放する。
			struct AlignedDeleter {
				size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
				void operator()(uint8_t* p) const noexcept {
					if (align > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
						::operator delete(static_cast<void*>(p), std::align_val_t(align));
					else
						::operator delete(static_cast<void*>(p));
				}
			};

			Archetype archetype_;
			std::unique_ptr<uint8_t, AlignedDeleter> begin_;
			/** TODO: 特別に初期化 */
			uint32_t size_ = 0;
			uint32_t capacity_ = 1;

			// 各行の EntityID を保持（swap-remove 時のスロット更新用）
			std::vector<EntityID> entityIDs_;




		public:
			bool operator==(const Chunk& other) const
			{
				return archetype_ == other.archetype_;
			}
			bool operator!=(const Chunk& other) const
			{
				return archetype_ != other.archetype_;
			}


			Chunk() = default;
			~Chunk();
			Chunk(Chunk&&) = default;
			Chunk& operator=(Chunk&&) = default;


			/**
			 * エンティティを生成（EntityID を row に記録）
			 */
			EntityLocation CreateEntity(EntityID id);

			/**
			 * エンティティを swap-remove で削除
			 * 最後尾エンティティが loc.index に移動した場合、その EntityID を返す
			 * 移動がなければ INVALID_ENTITY_ID を返す
			 */
			EntityID DestroyEntitySwap(const EntityLocation& loc);

			/**
			 * エンティティを他チャンクに移動（EntityID 対応版）
			 * src の swap-remove で移動した EntityID を返す
			 */
			EntityID MoveEntitySlot(EntityLocation& loc, Chunk& other, EntityID entityId);

			/**
			 * エンティティを他チャンクに移動しつつ droppedType のコンポーネントを破棄する。
			 * MoveEntitySlot（AddComponent 方向）の逆操作（RemoveComponent 用）。
			 * DestroyEntitySwap は呼ばず swap-remove ロジックを内包する。
			 * src の swap-remove で移動した EntityID を返す（移動なければ INVALID_ENTITY_ID）。
			 */
			EntityID MoveEntityRemoveArchetype(EntityLocation& loc, Chunk& other, EntityID entityId, const TypeInfo& droppedType);


			/**
			 * 指定行の EntityID を取得
			 */
			EntityID GetEntityID(uint32_t index) const
			{
				if (index >= static_cast<uint32_t>(entityIDs_.size()))
				{
					return INVALID_ENTITY_ID;
				}
				return entityIDs_[index];
			}


			/**
			 * コンポーネント取得
			 */
			template <typename T>
			T* GetComponent(const EntityLocation& loc)
			{
				assert(loc.index < size_ && "GetComponent: index out of bounds");
				if (loc.index >= size_) return nullptr;

				using TType = std::remove_const_t<std::remove_reference_t<T>>;
				const auto offset = archetype_.GetOffset<TType>() * capacity_;
				const auto currentIndex = sizeof(TType) * loc.index;
				return reinterpret_cast<T*>(begin_.get() + offset + currentIndex);
			}


			/**
			 * コンポーネントにデータ設定
			 */
			template <typename T>
			void SetComponent(const EntityLocation& loc, const T& component)
			{
				using TType = std::remove_const_t<std::remove_reference_t<T>>;
				auto* p = GetComponent<T>(loc);
				if (!p) return;
				if constexpr (std::is_trivially_copyable_v<TType>) {
					memcpy(p, &component, sizeof(TType));
				} else {
					*p = component;
				}
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
			 * Chunkに登録されているデータ数を取得
			 */
			uint32_t GetSize() const { return size_; }




		private:
			void ResetMemory(uint32_t capacity);

			static std::unique_ptr<uint8_t, AlignedDeleter> AllocBuffer(size_t bytes, size_t align)
			{
				if (align > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
					return { static_cast<uint8_t*>(::operator new(bytes, std::align_val_t(align))), AlignedDeleter{align} };
				return { static_cast<uint8_t*>(::operator new(bytes)), AlignedDeleter{} };
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
