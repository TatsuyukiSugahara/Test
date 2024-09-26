#pragma once
#include <cstdint>
#include <memory>
#include <vector>


namespace engine
{
	namespace ecs
	{
		/**
		 * System�̊��N���X
		 */
		class SystemBase
		{
		public:
			virtual ~SystemBase() {}


			virtual void Update() = 0;
		};


		/**
		 * System�Ǘ�
		 */
		class SystemManager
		{
		private:
			std::vector<std::unique_ptr<SystemBase>> systemList_;



		private:
			SystemManager() {}
			~SystemManager()
			{
				systemList_.clear();
			}


		public:
			/** System�X�V */
			void Update();


			/** System�ǉ� */
			template <typename T>
			void AddSystem()
			{
				systemList_.push_back(new T());
			}




			/**
			 * �C���X�^���X
			 */
		private:
			static SystemManager* instance_;


		public:
			static void Initialize()
			{
				if (instance_ == nullptr) {
					instance_ = new SystemManager();
				}
			}
			static SystemManager& Get()
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