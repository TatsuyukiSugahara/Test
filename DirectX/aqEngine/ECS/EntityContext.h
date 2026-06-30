#pragma once
#include "EntityManager.h"
#include "System.h"
#include "EntityDebugTag.h"
#include <type_traits>


namespace aq
{
	namespace ecs
	{
		// TC+HTC 同時生成の static_assert 用前方宣言
		struct TransformComponent;
		struct HierarchicalTransformComponent;
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

#ifdef AQ_DEBUG_IMGUI
			bool showSystemGraph_ = false;
#endif

		public:
			/** System を一括更新し、積まれたコマンドをフラッシュする */
			void Update()
			{
				systemManager_.Update();
				entityManager_.FlushCommands();
			}


			// --- Entity 操作 ---

			template <typename... Args>
			Entity CreateEntity()
			{
				static_assert(
					!(std::is_same_v<TransformComponent, Args> || ...) ||
					 (std::is_same_v<HierarchicalTransformComponent, Args> || ...),
					"TransformComponent を含む Entity は HierarchicalTransformComponent も必須です");
#ifdef AQ_DEBUG_IMGUI
				if constexpr ((std::is_same_v<Args, EntityDebugTag> || ...))
					return entityManager_.CreateEntity<Args...>();
				else
					return entityManager_.CreateEntity<Args..., EntityDebugTag>();
#else
				return entityManager_.CreateEntity<Args...>();
#endif
			}

			template <typename... Args>
			void RequestCreateEntity(std::function<void(Entity)> onCreated = nullptr)
			{
				static_assert(
					!(std::is_same_v<TransformComponent, Args> || ...) ||
					 (std::is_same_v<HierarchicalTransformComponent, Args> || ...),
					"TransformComponent を含む Entity は HierarchicalTransformComponent も必須です");
#ifdef AQ_DEBUG_IMGUI
				if constexpr ((std::is_same_v<Args, EntityDebugTag> || ...))
					entityManager_.RequestCreateEntity<Args...>(std::move(onCreated));
				else
					entityManager_.RequestCreateEntity<Args..., EntityDebugTag>(std::move(onCreated));
#else
				entityManager_.RequestCreateEntity<Args...>(std::move(onCreated));
#endif
			}

			// 実行時 TypeInfo 列から Entity を遅延生成する（Prefab 生成の核心 primitive）。
			// debug ビルドでは typed 版 CreateEntity と同様に EntityDebugTag を自動注入する。
			void RequestCreateEntityFromTypes(
				std::vector<TypeInfo>        types,
				std::function<void(Entity)>  onCreated = nullptr)
			{
				InjectDebugTag(types);
				entityManager_.RequestCreateEntityFromTypes(std::move(types), std::move(onCreated));
			}

			// 実行時 TypeInfo 列から Entity を即時生成する（init / エディタ用・ForEach 外限定）。
			// debug ビルドでは EntityDebugTag を自動注入する。
			Entity CreateEntityFromTypes(std::vector<TypeInfo> types)
			{
				InjectDebugTag(types);
				return entityManager_.CreateEntityFromTypes(std::move(types));
			}

			// 複数 Entity を 1 コマンドで生成する遅延ビルドフック（Prefab ツリー生成の土台）。
			// builder へ渡す生成関数は、debug ビルドで EntityDebugTag を自動注入する。
			void RequestDeferredBuild(std::function<void(const EntityManager::DeferredCreateFn&)> builder)
			{
				entityManager_.RequestDeferredBuild(
					[builder = std::move(builder)](const EntityManager::DeferredCreateFn& rawCreate)
					{
						EntityManager::DeferredCreateFn create =
							[&rawCreate](std::vector<TypeInfo> types) -> Entity
							{
								InjectDebugTag(types);
								return rawCreate(std::move(types));
							};
						builder(create);
					});
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
			void RemoveComponent(const EntityHandle& handle)
			{
				entityManager_.RemoveComponent<T>(handle);
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

			// EntityHandle から Entity を取得する。
			Entity GetEntity(EntityHandle handle);

			// EntityID から Entity を取得する。
			// EntityID は generation を持たないため、
			// 破棄済み ID を渡すと旧世代の Entity が返ってしまう危険がある。
			// 外部に ID を保持する場合は EntityHandle への移行を推奨する。
			Entity GetEntity(EntityID id);


			// --- 親子階層操作（HierarchicalTransformComponent が必要） ---

			// 親の EntityHandle を返す。HierarchicalTransformComponent がないか親未設定なら無効なハンドルを返す。
			EntityHandle GetParent(EntityHandle handle);

			// 子の EntityHandle リストを返す。無効なハンドルはスキップする。
			std::vector<EntityHandle> GetChildren(EntityHandle handle);

			// child の親を parent に設定する。HierarchicalTransformComponent の SetParent に委譲。
			bool SetParent(EntityHandle child, EntityHandle parent);

			// entity の親子関係を解除する。HierarchicalTransformComponent の DetachParent に委譲。
			void DetachParent(EntityHandle entity);


			// --- System 操作 ---

			template <typename T, typename... Dependencies>
			T* AddSystem()
			{
				return systemManager_.AddSystem<T, Dependencies...>();
			}

			template <typename TSystem, typename TDependency>
			void AddDependency()
			{
				systemManager_.AddDependency<TSystem, TDependency>();
			}

			template <typename TSystem, typename... TDependencies>
			void AddDependencies()
			{
				systemManager_.AddDependencies<TSystem, TDependencies...>();
			}

			template <typename T>
			bool HasSystem() const
			{
				return systemManager_.HasSystem<T>();
			}

			/**
			 * 全 System の登録と依存設定が終わったら呼ぶ。
			 * Register() / OnRegister() の末尾で一度だけ実行すること。
			 */
			void FinalizeRegistration()
			{
				systemManager_.BuildSchedule();
			}

			template <typename T>
			T* GetSystem()
			{
				return systemManager_.GetSystem<T>();
			}

#ifdef AQ_DEBUG_IMGUI
			/** メインメニューバー内に ECS メニュー（System Graph + 各 System / グループ）を追加する（BeginMainMenuBar 済み前提） */
			void DebugRenderMenu();
			/** 各 System のデバッグウィンドウ + 依存グラフウィンドウを描画する */
			void DebugRender();
#endif


		private:
			// debug ビルドでのみ EntityDebugTag を型列へ注入する（重複追加はしない）。
			// typed 版 CreateEntity と同じく、実行時 TypeInfo 列からの生成でも名前付けを可能にする。
			static void InjectDebugTag(std::vector<TypeInfo>& types)
			{
#ifdef AQ_DEBUG_IMGUI
				const TypeInfo tag = TypeInfo::Create<EntityDebugTag>();
				for (const TypeInfo& t : types) {
					if (t == tag) return;
				}
				types.push_back(tag);
#else
				(void)types;
#endif
			}

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
