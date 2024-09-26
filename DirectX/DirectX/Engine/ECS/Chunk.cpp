#include "Chunk.h"


namespace engine
{
	namespace ecs
	{
		void Chunk::Marge(Chunk&& other)
		{
			// Archetypeが同じでなければマージできない
			if (archetype_ != other.archetype_) {
				return;
			}

			const auto needSize = size_ + other.size_;
			if (capacity_ < needSize) {
				ResetMemory(needSize);
				capacity_ = needSize;
			}

			for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i) {
				const auto archetypeOffset = archetype_.GetOffsetByIndex(i);
				const auto archetypeSize = archetype_.GetSize(i);
				const auto offset = archetypeOffset * capacity_ + archetypeSize * size_;
				const auto otherOffset = archetypeOffset * other.capacity_;
				memcpy(begin_.get() + offset, other.begin_.get() + otherOffset, archetypeSize * other.size_);
			}
			size_ += other.size_;

			other.begin_.reset();
			other.size_ = 0;
			other.capacity_ = 0;
		}


		Entity Chunk::CreateEntity()
		{
			// エンティティをチャンクに追加できないのでメモリの移動を行う
			if (capacity_ == size_) {
				// とりあえず倍
				ResetMemory(capacity_ * 2);
			}
			const auto entity = Entity(size_);
			++size_;
			return entity;
		}


		void Chunk::DestroyEntity(const Entity& entity)
		{
			for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i) {
				const auto offset = archetype_.GetOffsetByIndex(i) * capacity_;
				const auto currentIndex = offset + archetype_.GetSize(i) * entity.index;
				memmove(begin_.get() + currentIndex, begin_.get() + currentIndex + archetype_.GetSize(i), archetype_.GetSize(i) * (size_ - entity.index - 1));
			}
			--size_;
		}


		void Chunk::MoveEntity(Entity& entity, Chunk& other)
		{
			const auto newEntity = other.CreateEntity();
			for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i) {
				const auto currentOffset = archetype_.GetOffsetByIndex(i) * capacity_;
				const auto currentIndex = currentOffset + archetype_.GetSize(i) * entity.index;
				
				const auto otherArchetypeIndex = other.archetype_.GetIndexByTypeInfo(archetype_.GetTypeInfo(i));
				const auto otherOffset = other.archetype_.GetOffsetByIndex(otherArchetypeIndex) * other.capacity_;
				const auto otherIndex = otherOffset + other.archetype_.GetSize(otherArchetypeIndex) * newEntity.index;

				memcpy(other.begin_.get() + otherIndex, begin_.get() + currentIndex, archetype_.GetSize(i));
			}

			DestroyEntity(entity);
			entity = newEntity;
		}


		void Chunk::ResetMemory(uint32_t capacity)
		{
			auto work = std::make_unique<uint8_t[]>(archetype_.GetArchetypeMemorySize() * capacity);

			size_t offseBase = 0;
			for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i) {
				const auto offset = offseBase * capacity_;
				const auto newOffset = offseBase * capacity;
				memcpy(work.get() + newOffset, begin_.get() + offset, archetype_.GetSize(i) * size_);

				offseBase += archetype_.GetSize(i);
			}

			capacity_ = capacity;
			begin_ = std::move(work);
		}


		Chunk Chunk::Create(const Archetype& archetype, const uint32_t capacity)
		{
			Chunk result;
			result.capacity_ = capacity;
			result.archetype_ = archetype;
			result.begin_ = std::make_unique<uint8_t[]>(result.capacity_ * result.archetype_.GetArchetypeMemorySize());
			return result;
		}
	}
}