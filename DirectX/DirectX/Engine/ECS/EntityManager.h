#pragma once
#include "Chunk.h"
#include <vector>
#include <memory>


namespace engine
{
	namespace ecs
	{
		class EntityManager
		{
		private:
			std::vector<Chunk> chunkList_;



		private:
			EntityManager() {}
			~EntityManager()
			{
				chunkList_.clear();
			}


		public:
			/**
			 * �G���e�B�e�B����
			 */
			Entity CreateEntity(const Archetype& archetype);

			template <typename ...Args>
			Entity CreateEntity()
			{
				constexpr auto archetype = Archetype::Create<Args...>();
				return CreateEntity(archetype);
			}


			/**
			 * �G���e�B�e�B�j��
			 */
			void DestroyEntity(const Entity& entity);


		public:
			/**
			 * �`�����N����
			 */
			uint32_t CreateChunk(const Archetype& archetype);

			template <typename ...Args>
			uint32_t CreateChunk()
			{
				constexpr auto archetype = Archetype::Create<Args...>();
				return CreateChunk(archetype);
			}

			/**
			 * �w��^�C�v�̃`�����N�C���f�b�N�X�擾
			 */
			uint32_t GetChunkIndex(const Archetype& archetype) const;


			/**
			 * �`�����N���X�g�擾
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
			 * �R���|�[�l���g�ǉ�
			 */
			template <typename T>
			void AddComponent(Entity& entity)
			{
				auto newArchetype = chunkList_[entity.chunkIndex].GetArchetype();
				newArchetype.AddType<T>();
				const auto newChunkIndex = GetChunkIndex(newArchetype);
				if (newChunkIndex == chunkList_.size()) {
					newChunkIndex = CreateChunk(newArchetype);
				}

				auto& chunk = chunkList_[newChunkIndex];
				chunkList_[entity.chunkIndex].MoveEntity(entity, chunk);
				entity.chunkIndex = newChunkIndex;
			}
			

			/**
			 * �R���|�[�l���g�擾
			 */
			template <typename T>
			T* GetComponent(const Entity& entity)
			{
				const auto chunkIndex = GetChunkIndex(entity.chunkIndex);
				return chunkList_[chunkIndex].GetComponent<T>(entity);
			}


			/**
			 * �R���|�[�l���g�Ƀf�[�^�ݒ�
			 */
			template <typename T>
			void SetComponent(const Entity& entity)
			{
				const auto chunkIndex = GetChunkIndex(entity.chunkIndex);
				chunkList_[chunkIndex].SetComponent<T>(entity);
			}




			/**
			 * �C���X�^���X
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