/**
 * �Q�[���I�u�W�F�N�g�̊��N���X
 */
#pragma once


namespace engine
{
	class IGameObject;
	using GameObjectPriority = uint8_t;

	// GameObject��V���ɒ�`�����ۂɋL�ڂ���
#define engineGameObject(name) public:\
	static int32_t ID() { return util::Hash32(#name); }


	/**
	 * �Q�[���I�u�W�F�N�g�̊��N���X
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
		/** �R���X�g���N�^ */
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
		/** �f�X�g���N�^ */
		virtual ~IGameObject()
		{
		}


	public:
		/** NOTE: �p����Ŏ������K�v�Ȋ֐��Q */

		/** �Q�[���I�u�W�F�N�g�������� */
		virtual bool Start() = 0;
		/** �X�V */
		virtual void Update() = 0;
		/** �`�� */
		virtual void Render(graphics::RenderContext& context) = 0;


	public:
		/** ���������1�x�Ă΂��֐� */
		virtual void Awake() {}
		/** Update�֐��O�ɌĂ΂��X�V */
		virtual void PreUpdate() {}
		/** Update�֐���ɌĂ΂��X�V */
		virtual void PostUpdate() {}
		/** Render�֐��O�ɌĂ΂��`�� */
		virtual void PreRender(graphics::RenderContext& context) {}
		/** Render�֐���ɌĂ΂��`�� */
		virtual void PostRender(graphics::RenderContext& context) {}
		

	public:
		/** �폜�����Ƃ��ɌĂ΂��֐� */
		virtual void OnDestroy() {}


	public:
		/** �D��x�擾 */
		GameObjectPriority GetPriority() const { return priority_; }
		
		/** ���쒆���ݒ� */
		void SetActive(bool active) { isActive_ = active; }
		/** ���쒆���擾 */
		bool IsActive() const { return isActive_; }


	public:
		/** NOTE: �O���ŏ�������/�X�V/�`��Ȃǂ��ĂԎ��Ɏg�p����֐��Q */
		
		/** �������� */
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
	 * �Q�[���I�u�W�F�N�g���Ǘ�
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
			// ���S���Ă���Q�[���I�u�W�F�N�g��o�^���悤�Ƃ��Ă���
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
		/** �Q�[���I�u�W�F�N�g�폜�����s */
		void ExecuteDeleteGameObject();


		/** �V�[���O���t�X�V */
		void UpdateSceneGraph();


		/**
		 * �C���X�^���X�֘A
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