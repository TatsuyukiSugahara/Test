#pragma once
#include <cstdint>
#include <memory>
#include <vector>


namespace engine
{
	namespace ecs
	{
		/**
		 * Systemの基底クラス
		 */
		class SystemBase
		{
		public:
			virtual ~SystemBase() {}


			virtual void Update() = 0;
		};


		/**
		 * System管理
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
			/** System更新 */
			void Update();


			/** System追加 */
			template <typename T>
			void AddSystem()
			{
				systemList_.push_back(new T());
			}




			/**
			 * インスタンス
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