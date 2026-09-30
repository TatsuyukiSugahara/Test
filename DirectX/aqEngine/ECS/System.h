#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <future>
#include <string>
#include <typeindex>
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
				std::type_index      type = typeid(void); // 登録時の型。照合は完全一致で行う
				std::vector<size_t> dependencyIndices;
				std::string          displayName;
				size_t               level = 0; // wave schedule 用 実行レベル
			};

#ifdef AQ_DEBUG_IMGUI
			struct GroupEntry
			{
				std::string              name;
				bool                     show = false;
				std::vector<SystemBase*> systems;
			};
#endif

			/** 登録状態(登録中 → 確定済み / 確定失敗 の一方向) */
			enum class RegistrationState
			{
				Registering,
				Finalized,
				Failed,
			};

			std::vector<SystemEntry> systemEntries_;
			std::vector<size_t>      updateOrder_;
			RegistrationState        registrationState_   = RegistrationState::Registering;
			std::vector<std::string> registrationErrors_;
			bool                     updateRejectLogged_  = false;
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
				if (registrationState_ != RegistrationState::Registering) {
					LogRejectedAfterFinalize("AddSystem", typeid(T).name(), nullptr);
					EngineAssertMsg(false, "AddSystem: called after FinalizeRegistration");
					return nullptr;
				}

				if (HasSystem<T>()) {
					if constexpr (sizeof...(Dependencies) > 0)
						AddDependencies<T, Dependencies...>();
					return GetSystem<T>();
				}

				SystemEntry entry;
				entry.system      = std::make_unique<T>();
				entry.type        = typeid(T);
				entry.displayName = typeid(T).name();
				T* ptr            = static_cast<T*>(entry.system.get());
				systemEntries_.push_back(std::move(entry));

				if constexpr (sizeof...(Dependencies) > 0)
					AddDependencies<T, Dependencies...>();

				return ptr;
			}


			/**
			 * TSystem が TDependency の完了を待つ依存を追加する。
			 * 両方とも AddSystem 済みであること（未登録の場合はエラーとして記録し、確定時に失敗する）。
			 */
			template <typename TSystem, typename TDependency>
			void AddDependency()
			{
				static_assert(!std::is_same_v<TSystem, TDependency>, "AddDependency: TSystem and TDependency are the same type (self-dependency)");

				if (registrationState_ != RegistrationState::Registering) {
					LogRejectedAfterFinalize("AddDependency", typeid(TSystem).name(), typeid(TDependency).name());
					EngineAssertMsg(false, "AddDependency: called after FinalizeRegistration");
					return;
				}

				const size_t sysIdx = FindIndex<TSystem>();
				const size_t depIdx = FindIndex<TDependency>();
				if (sysIdx == SIZE_MAX) {
					RecordDependencyError("system is not registered", typeid(TSystem).name(), typeid(TDependency).name());
				}
				if (depIdx == SIZE_MAX) {
					RecordDependencyError("dependency is not registered", typeid(TSystem).name(), typeid(TDependency).name());
				}
				if (sysIdx == SIZE_MAX || depIdx == SIZE_MAX) return;

				// 型は完全一致で照合するので通常は起きない。照合規則が変わったときの防御
				if (sysIdx == depIdx) {
					RecordDependencyError("both types resolve to the same system", typeid(TSystem).name(), typeid(TDependency).name());
					return;
				}

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
			 * 登録した型との完全一致のみ。基底型を指定しても派生型の System は返らない。
			 */
			template <typename T>
			T* GetSystem()
			{
				const size_t index = FindIndex<T>();
				if (index == SIZE_MAX) return nullptr;
				return static_cast<T*>(systemEntries_[index].system.get());
			}


		private:
			/**
			 * 全 System の登録と依存設定が終わったら呼ぶ。
			 * EntityContext::FinalizeRegistration() 経由でのみ呼ぶこと。
			 * トポロジカルソートで実行順を確定し、以降の登録系呼び出しを禁止する。
			 * @return 登録エラーが 1 件もなく実行順を確定できたら true
			 */
			bool BuildSchedule();

			/** 確定後の登録呼び出しを拒否した旨をログに出す(dependencyName は nullptr 可) */
			void LogRejectedAfterFinalize(const char* api, const char* systemName, const char* dependencyName) const;

			/** AddDependency の登録エラーを記録する */
			void RecordDependencyError(const char* reason, const char* systemName, const char* dependencyName);

			template <typename T>
			size_t FindIndex() const
			{
				const std::type_index type(typeid(T));
				for (size_t i = 0; i < systemEntries_.size(); ++i) {
					if (systemEntries_[i].type == type)
						return i;
				}
				return SIZE_MAX;
			}
		};
	}
}
