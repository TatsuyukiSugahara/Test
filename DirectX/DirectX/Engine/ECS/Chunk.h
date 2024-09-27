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
			/** TODO:���ʂɏ����� */
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
			 * ���݂�Size��Capacity���l�߂�
			 */
			void Fit() { ResetMemory(size_); }


			/**
			 * Other�������̃`�����N�Ƀ}�[�W
			 */
			void Marge(Chunk&& other);


			/**
			 * �G���e�B�e�B�𐶐�
			 */
			Entity CreateEntity();


			/**
			 * �G���e�B�e�B���폜
			 */
			void DestroyEntity(const Entity& entity);


			/**
			 * �G���e�B�e�B�𑼃`�����N�Ɉړ�
			 */
			void MoveEntity(Entity& entity, Chunk& other);


			/**
			 * �R���|�[�l���g�ǉ�
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
			 * �R���|�[�l���g�擾
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
			 * �R���|�[�l���g�Ƀf�[�^�ݒ�
			 */
			template <typename T>
			void SetComponent(const Entity& entity, const T& component)
			{
				using TType = std::remove_const_t<std::remove_reference_t<T>>;
				auto* p = GetComponent<T>(entity);
				memcpy(p, &component, sizeof(TType));
			}


			/**
			 * �f�[�^�擾
			 */
			template <class T>
			ComponentArray<T> GetComponentArray()
			{
				using TType = std::remove_const_t<std::remove_reference_t<T>>;
				auto offset = archetype_.GetOffset<TType>() * capacity_;
				return ComponentArray<T>(reinterpret_cast<TType*>(begin_.get() + offset), size_);
			}


			/**
			 * Archetype���擾
			 */
			const Archetype& GetArchetype() const
			{
				return archetype_;
			}


			/**
			 * Chunk�ɓo�^���Ă���f�[�^�̐����擾
			 */
			uint32_t GetSize() const { return size_; }




		private:
			/**
			 * �`�����N�̃������[���ăA���P�[�g
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