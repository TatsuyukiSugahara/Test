#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <future>
#include <string>
#include <algorithm>


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
			virtual void        DebugRenderMenu()               {}
			virtual void        DebugRender()                   {}

			// グループタブに参加する場合はグループ名を返す（nullptr = 個別ウィンドウ）
			virtual const char* GetDebugGroup()    const        { return nullptr; }
			// タブバー上のラベル
			virtual const char* GetDebugTabLabel() const        { return ""; }
			// Begin/End なしで中身だけ描画（グループタブから呼ばれる）
			virtual void        RenderContent()                 {}
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

#ifdef AQ_DEBUG_IMGUI
			struct GroupEntry
			{
				std::string              name;
				bool                     show = false;
				std::vector<SystemBase*> systems;
			};
#endif

			std::vector<SystemEntry> systemEntries_;
			std::vector<size_t>      updateOrder_;
			bool                     registrationFinalized_ = false;
#ifdef AQ_DEBUG_IMGUI
			std::vector<GroupEntry>  groups_;
#endif


		private:
			friend class EntityContext;

			SystemManager() {}
			~SystemManager()
			{
				systemEntries_.clear();
			}


		public:
			/** System更新(依存関係を考慮して並列実行)。FinalizeRegistration 済みであること。 */
			void Update();

#ifdef AQ_DEBUG_IMGUI
			/** グループなし System のメニュー + グループごとの MenuItem を描画 */
			void DebugRenderMenuAll();
			/** グループなし System の個別ウィンドウ + グループタブウィンドウを描画 */
			void DebugRenderAll();
#endif


			/**
			 * System 追加。既に登録済みの場合は Dependencies の追加のみ行う。
			 * 後方互換として Dependencies... を指定することもできる。
			 */
			template <typename T, typename... Dependencies>
			T* AddSystem()
			{
				EngineAssertMsg(!registrationFinalized_, "AddSystem: called after FinalizeRegistration");

				if (HasSystem<T>()) {
					if constexpr (sizeof...(Dependencies) > 0)
						AddDependencies<T, Dependencies...>();
					return GetSystem<T>();
				}

				SystemEntry entry;
				entry.system      = std::make_unique<T>();
				entry.displayName = typeid(T).name();
				T* ptr            = static_cast<T*>(entry.system.get());
				systemEntries_.push_back(std::move(entry));

				if constexpr (sizeof...(Dependencies) > 0)
					AddDependencies<T, Dependencies...>();

				return ptr;
			}


			/**
			 * TSystem が TDependency の完了を待つ依存を追加する。
			 * 両方とも AddSystem 済みであること（未登録の場合は assert + Release ガード）。
			 */
			template <typename TSystem, typename TDependency>
			void AddDependency()
			{
				EngineAssertMsg(!registrationFinalized_, "AddDependency: called after FinalizeRegistration");

				const size_t sysIdx = FindIndex<TSystem>();
				const size_t depIdx = FindIndex<TDependency>();
				EngineAssertMsg(sysIdx != SIZE_MAX, "AddDependency: TSystem is not registered");
				EngineAssertMsg(depIdx != SIZE_MAX, "AddDependency: TDependency is not registered");
				if (sysIdx == SIZE_MAX || depIdx == SIZE_MAX) return;

				auto& deps = systemEntries_[sysIdx].dependencyIndices;
				if (std::find(deps.begin(), deps.end(), depIdx) == deps.end())
					deps.push_back(depIdx);
			}

			/** 複数の依存をまとめて追加する */
			template <typename TSystem, typename... TDependencies>
			void AddDependencies()
			{
				if constexpr (sizeof...(TDependencies) > 0)
					(AddDependency<TSystem, TDependencies>(), ...);
			}

			/** 型 T の System が登録済みか */
			template <typename T>
			bool HasSystem() const
			{
				return FindIndex<T>() != SIZE_MAX;
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
			/**
			 * 全 System の登録と依存設定が終わったら呼ぶ。
			 * EntityContext::FinalizeRegistration() 経由でのみ呼ぶこと。
			 * トポロジカルソートで実行順を確定し、以降の登録系呼び出しを禁止する。
			 */
			void BuildSchedule();

			template <typename T>
			size_t FindIndex() const
			{
				for (size_t i = 0; i < systemEntries_.size(); ++i) {
					if (dynamic_cast<T*>(systemEntries_[i].system.get()) != nullptr)
						return i;
				}
				return SIZE_MAX;
			}
		};
	}
}
