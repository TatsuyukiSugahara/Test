#pragma once
#include "TypeInfo.h"
#include <functional>


namespace engine
{
	namespace ecs
	{
		static constexpr uint32_t INVALID_ENTITY_INDEX = 0xffffffff;

		using EntityID = uint32_t;
		static constexpr EntityID INVALID_ENTITY_ID = 0xffffffff;


		// ECS 内部で chunk 上の物理位置を表す型（外部に公開しない）
		struct EntityLocation
		{
			uint32_t index;
			uint32_t chunkIndex;

			explicit EntityLocation(const uint32_t idx)
				: chunkIndex(INVALID_ENTITY_INDEX)
				, index(idx)
			{
			}

			EntityLocation(const uint32_t chunkIdx, const uint32_t idx)
				: chunkIndex(chunkIdx)
				, index(idx)
			{
			}

			bool operator==(const EntityLocation& other) const
			{
				return index == other.index && chunkIndex == other.chunkIndex;
			}
			bool operator!=(const EntityLocation& other) const
			{
				return !(*this == other);
			}
		};


		// EntityID + generation による安全ハンドル
		struct EntityHandle
		{
			EntityID id;
			uint32_t generation;

			EntityHandle()
				: id(INVALID_ENTITY_ID)
				, generation(0)
			{
			}
			EntityHandle(EntityID id, uint32_t generation)
				: id(id)
				, generation(generation)
			{
			}
			bool operator==(const EntityHandle& other) const
			{
				return id == other.id && generation == other.generation;
			}
			bool operator!=(const EntityHandle& other) const
			{
				return !(*this == other);
			}
			bool IsValid() const { return id != INVALID_ENTITY_ID; }

			static EntityHandle InvalidHandle() { return EntityHandle(); }
		};


		// EntityID -> 現在位置 のマッピングエントリ（内部用）
		struct EntitySlot
		{
			EntityLocation location;
			uint32_t generation = 0;
			bool alive = false;
			EntityID nextFree = INVALID_ENTITY_ID;

			EntitySlot() : location(INVALID_ENTITY_INDEX) {}
		};


		class EntityManager;  // forward declaration


		// 外部に渡す軽量操作ラッパー（値渡し可能）
		// EntityManager* + EntityHandle を保持し、{chunkIndex, index} を隠蔽する。
		// generation チェック済みの API のみを公開する。
		class Entity
		{
		public:
			Entity() = default;
			Entity(EntityManager* manager, EntityHandle handle)
				: manager_(manager)
				, handle_(handle)
			{
			}

			bool IsValid() const;
			EntityHandle GetHandle() const { return handle_; }
			EntityID GetID() const { return handle_.id; }

			void Destroy() const;

			template <typename T>
			T* GetComponent() const;

			template <typename T>
			void AddComponent(std::function<void(T*)> onAdded = nullptr) const;

		private:
			EntityManager* manager_ = nullptr;
			EntityHandle handle_;
		};
	}
}
