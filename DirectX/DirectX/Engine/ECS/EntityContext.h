#pragma once
#include "EntityManager.h"
#include "System.h"


namespace engine
{
	namespace ecs
	{
		/**
		 * ECS の統括窓口。EntityManager と SystemManager を所有し、
		 * 外部からのアクセス・初期化・更新を一本化する唯一のシングルトン。
		 */
		class EntityContext
		{
		private:
			EntityManager entityManager_;
			SystemManager systemManager_;

			EntityContext() {}
			~EntityContext() {}


		public:
			/** System を一括更新し、積まれたコマンドをフラッシュする */
			void Update()
			{
				systemManager_.Update();
				entityManager_.FlushCommands();
			}


			// --- Entity 操作 ---

			Entity CreateEntity(const Archetype& archetype)
			{
				return entityManager_.CreateEntity(archetype);
			}

			template <typename... Args>
			Entity CreateEntity()
			{
				return entityManager_.CreateEntity<Args...>();
			}

			bool IsValid(const EntityHandle& handle) const
			{
				return entityManager_.IsValid(handle);
			}

			void RequestDestroyEntity(const EntityHandle& handle)
			{
				entityManager_.RequestDestroyEntity(handle);
			}

			template <typename T>
			void AddComponent(const EntityHandle& handle, std::function<void(T*)> onAdded = nullptr)
			{
				entityManager_.AddComponent<T>(handle, std::move(onAdded));
			}

			template <typename T>
			T* GetComponent(const EntityHandle& handle)
			{
				return entityManager_.GetComponent<T>(handle);
			}

			template <typename T>
			T* GetComponent(const Entity& entity)
			{
				return entityManager_.GetComponent<T>(entity);
			}

			template <typename... Cs>
			EntityView<Cs...> GetView()
			{
				return entityManager_.GetView<Cs...>();
			}


			// --- System 操作 ---

			template <typename T, typename... Dependencies>
			void AddSystem()
			{
				systemManager_.AddSystem<T, Dependencies...>();
			}

			template <typename T>
			T* GetSystem()
			{
				return systemManager_.GetSystem<T>();
			}


		private:
			static EntityContext* instance_;


		public:
			static void Initialize()
			{
				if (instance_ == nullptr)
				{
					instance_ = new EntityContext();
				}
			}
			static EntityContext& Get()
			{
				return *instance_;
			}
			static void Finalize()
			{
				if (instance_)
				{
					delete instance_;
					instance_ = nullptr;
				}
			}
		};
	}
}
