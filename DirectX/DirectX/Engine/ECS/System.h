#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <future>


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
			struct SystemEntry
			{
				std::unique_ptr<SystemBase> system;
				std::vector<size_t> dependencyIndices;
			};

			std::vector<SystemEntry> systemEntries_;


		private:
			SystemManager() {}
			~SystemManager()
			{
				systemEntries_.clear();
			}


		public:
			/** System更新(依存関係を考慮して並列実行) */
			void Update();


			/**
			 * System追加
			 * Dependencies... に依存するSystemの型を指定すると、
			 * そのSystemの完了を待ってから実行される。
			 */
			template <typename T, typename... Dependencies>
			void AddSystem()
			{
				SystemEntry entry;
				entry.system = std::make_unique<T>();
				ResolveDependencies<Dependencies...>(entry.dependencyIndices);
				systemEntries_.push_back(std::move(entry));
			}


		private:
			template <typename... Types>
			void ResolveDependencies(std::vector<size_t>& indices)
			{
				if constexpr (sizeof...(Types) > 0) {
					(ResolveOne<Types>(indices), ...);
				}
			}

			template <typename T>
			void ResolveOne(std::vector<size_t>& indices)
			{
				for (size_t i = 0; i < systemEntries_.size(); ++i) {
					if (dynamic_cast<T*>(systemEntries_[i].system.get()) != nullptr) {
						indices.push_back(i);
						return;
					}
				}
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
