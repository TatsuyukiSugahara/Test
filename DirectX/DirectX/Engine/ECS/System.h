#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <future>


namespace aq
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
			friend class EntityContext;

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


			/**
			 * 型で System を取得する。登録されていない場合は nullptr を返す。
			 */
			template <typename T>
			T* GetSystem()
			{
				for (auto& entry : systemEntries_) {
					if (auto* system = dynamic_cast<T*>(entry.system.get())) {
						return system;
					}
				}
				return nullptr;
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


		};
	}
}
