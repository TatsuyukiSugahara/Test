//#include "../../Engine/EnginePreCompile.h"
//#include "Scene.h"
//
//
//namespace app
//{
//	namespace scene
//	{
//
//
//		SceneManager* SceneManager::instance_ = nullptr;
//
//		SceneManager::SceneManager()
//			: currentScene_(nullptr)
//			, nextSceneId_(INVALID_SCENE_ID)
//		{
//		}
//
//
//		SceneManager::~SceneManager()
//		{
//			if (currentScene_) {
//				delete currentScene_;
//				currentScene_ = nullptr;
//			}
//		}
//
//
//		void SceneManager::Update()
//		{
//			if (nextSceneId_ == TestScene::GetID()) {
//				currentScene_ = new TestScene();
//				currentScene_->Initialize();
//				nextSceneId_ = INVALID_SCENE_ID;
//			}
//			
//
//			if (currentScene_) {
//				currentScene_->Update();
//			}
//		}
//
//
//		/***************************/
//
//
//
//
//		TestScene::TestScene()
//		{
//		}
//
//
//		TestScene::~TestScene()
//		{
//		}
//
//
//		void TestScene::Update()
//		{
//
//		}
//
//
//		void TestScene::Initialize()
//		{
//
//		}
//	}
//}