/**
 * ゲームオブジェクトの基底クラス
 */
#pragma once


namespace engine
{
	class IGameObject;
	using GameObjectPriority = uint8_t;

	// GameObjectを新たに定義した際に記載する
#define engineGameObject(name) public:\
	static int32_t ID() { return util::Hash32(#name); }


	/**
	 * ゲームオブジェクトの基底クラス
	 */
	class IGameObject
	{
		friend class GameObjectManager;


	public:
		static int32_t ID() { EngineAssert(false); return 0; }

	protected:
		GameObjectPriority priority_;
		bool isStart_;
		bool isDead_;
		bool isRegistDeadList_;
		bool isNewFromGameObjectManager_;
		bool isRegist_;
		bool isActive_;


	public:
		/** コンストラクタ */
		IGameObject()
			: priority_(0)
			, isStart_(false)
			, isDead_(false)
			, isRegistDeadList_(false)
			, isNewFromGameObjectManager_(false)
			, isRegist_(false)
			, isActive_(true)
		{
		}
		/** デストラクタ */
		virtual ~IGameObject()
		{
		}


	public:
		/** NOTE: 継承先で実装が必要な関数群 */

		/** ゲームオブジェクト準備完了 */
		virtual bool Start() = 0;
		/** 更新 */
		virtual void Update() = 0;
		/** 描画 */
		virtual void Render(graphics::RenderContext& context) = 0;


	public:
		/** 生成直後に1度呼ばれる関数 */
		virtual void Awake() {}
		/** Update関数前に呼ばれる更新 */
		virtual void PreUpdate() {}
		/** Update関数後に呼ばれる更新 */
		virtual void PostUpdate() {}
		/** Render関数前に呼ばれる描画 */
		virtual void PreRender(graphics::RenderContext& context) {}
		/** Render関数後に呼ばれる描画 */
		virtual void PostRender(graphics::RenderContext& context) {}
		

	public:
		/** 削除されるときに呼ばれる関数 */
		virtual void OnDestroy() {}


	public:
		/** 優先度取得 */
		GameObjectPriority GetPriority() const { return priority_; }
		
		/** 動作中か設定 */
		void SetActive(bool active) { isActive_ = active; }
		/** 動作中か取得 */
		bool IsActive() const { return isActive_; }


	public:
		/** NOTE: 外部で準備完了/更新/描画などを呼ぶ時に使用する関数群 */
		
		/** 準備完了 */
		void StartWrapper()
		{
			if (CanStart() && Start()) {
				isStart_ = true;
			}
		}
		void UpdateWapper()
		{
			if (CanRun()) {
				Update();
			}
		}
		void PreUpdateWapper()
		{
			if (CanRun()) {
				PreUpdate();
			}
		}
		void PostUpdateWapper()
		{
			if (CanRun()) {
				PreUpdate();
			}
		}
		void PreRenderWapper(graphics::RenderContext& context)
		{
			if (CanRun()) {
				PreRender(context);
			}
		}
		void RenderWapper(graphics::RenderContext& context)
		{
			if (CanRun()) {
				Render(context);
			}
		}
		void PostRenderWapper(graphics::RenderContext& context)
		{
			if (CanRun()) {
				PostRender(context);
			}
		}


	private:
		bool CanStart() const
		{
			return isActive_ && !isStart_ && !isDead_ && !isRegistDeadList_;
		}

		bool CanRun() const
		{
			return isActive_ && isStart_ && !isDead_ && !isRegistDeadList_;
		}
	};



	/**
	 * ゲームオブジェクトを管理
	 */
	class GameObjectManager
	{
	private:
		static const GameObjectPriority GAME_OBJECT_PRIORITY_MAX = 255;
		using GameObjectList = std::list<IGameObject*>;


	private:
		std::vector<GameObjectList> gameObjectListArray_;
		std::vector<GameObjectList> deleteGameObjectListArray_[2];
		uint32_t currentDeleteGameObjectBufferIndex_;
		GameObjectPriority maximumPriority_;


	private:
		GameObjectManager();
		~GameObjectManager();


	public:
		void Initialize(GameObjectPriority maximumPriority);

		void Execute(graphics::RenderContext& context);

		void Add(GameObjectPriority priority, IGameObject* gameObject, const char* name = nullptr)
		{
			if (gameObject->isRegist_) return;
			EngineAssert(priority < GAME_OBJECT_PRIORITY_MAX);
			gameObject->Awake();
			gameObjectListArray_.at(priority).push_back(gameObject);
			gameObject->isRegist_ = true;
			gameObject->priority_ = priority;
			// 死亡しているゲームオブジェクトを登録しようとしている
			if (gameObject->isDead_) {
				EngineAssert(false);
			}
		}
		
		template<typename T>
		T* CreateGameObject(GameObjectPriority priority, const char* name)
		{
			EngineAssert(priority < GAME_OBJECT_PRIORITY_MAX);
			T* gameObject = new T();
			gameObject->Awake();
			gameObjectListArray_.at(priority).push_back(gameObject);
			gameObject->isNewFromGameObjectManager_ = true;
			gameObject->isRegisted_ = true;
			gameObject->priority_ = priority;
			return gameObject;
		}


		void DeleteGameObject(IGameObject* gameObject)
		{
			if (gameObject == nullptr) return;
			if (gameObject->isRegist_) return;
			gameObject->isDead_ = true;
			gameObject->OnDestroy();
			gameObject->isRegist_ = false;
			gameObject->isRegistDeadList_ = true;
			deleteGameObjectListArray_[currentDeleteGameObjectBufferIndex_].at(gameObject->GetPriority()).push_back(gameObject);
		}


		void ForEach(int32_t id, void(*func)(IGameObject* gameObject))
		{
			for (auto& list : gameObjectListArray_) {
				for (auto& gameObject : list) {
					if ((id & gameObject->ID()) != 0) {
						(*func)(gameObject);
					}
				}
			}
		}


	private:
		/** ゲームオブジェクト削除を実行 */
		void ExecuteDeleteGameObject();


		/** シーングラフ更新 */
		void UpdateSceneGraph();


		/**
		 * インスタンス関連
		 */
	private:
		static GameObjectManager* instance_;


	public:
		static GameObjectManager& Get() { return *instance_; }
		static bool IsAvailable() { return instance_ != nullptr; }
		static void Create()
		{
			EngineAssert(instance_ == nullptr);
			instance_ = new GameObjectManager();
		}
		static void Release()
		{
			if (instance_) {
				delete instance_;
				instance_ = nullptr;
			}
		}
	};
}