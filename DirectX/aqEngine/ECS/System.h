#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <future>
#include <string>


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
#ifdef AQ_DEBUG_IMGUI
			virtual void DebugRenderMenu() {}
			virtual void DebugRender() {}
#endif
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
				std::string          displayName;
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

#ifdef AQ_DEBUG_IMGUI
			/** 全 System の DebugRenderMenu をメインメニューバー内で呼ぶ */
			void DebugRenderMenuAll()
			{
				for (auto& entry : systemEntries_)
					entry.system->DebugRenderMenu();
			}

			/** 全 System の DebugRender をメインスレッドで直列実行 */
			void DebugRenderAll()
			{
				for (auto& entry : systemEntries_)
					entry.system->DebugRender();
			}
#endif


			/**
			 * System追加
			 * Dependencies... に依存するSystemの型を指定すると、
			 * そのSystemの完了を待ってから実行される。
			 */
			template <typename T, typename... Dependencies>
			void AddSystem()
			{
				SystemEntry entry;
				entry.system      = std::make_unique<T>();
				entry.displayName = typeid(T).name();
				ResolveDependencies<Dependencies...>(entry.dependencyIndices);
				systemEntries_.push_back(std::move(entry));
			}


			/** 登録済み System の数 */
			size_t GetSystemCount() const { return systemEntries_.size(); }

			/** インデックス i の System の短いクラス名 */
			const std::string& GetSystemDisplayName(size_t i) const { return systemEntries_[i].displayName; }

			/** インデックス i の System が依存するインデックス列 */
			const std::vector<size_t>& GetSystemDependencies(size_t i) const { return systemEntries_[i].dependencyIndices; }


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
