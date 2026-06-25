#include "stdafx.h"
#include "Scene.h"
#include "BattleScene.h"

namespace app
{
	SceneManager* SceneManager::instance_ = nullptr;

	aq::math::Vector3 IScene::GetFocusPosition() const
	{
		return aq::math::Vector3(0.f, 0.f, 0.f);
	}


	SceneManager::SceneManager()
		: currentScene_(nullptr)
		, nextSceneId_(INVALID_SCENE_ID)
	{
		// 一旦バトルシーンへ飛ばす
		nextSceneId_ = app::battle::BattleScene::GetID();
	}


	SceneManager::~SceneManager()
	{
		if (currentScene_) {
			delete currentScene_;
			currentScene_ = nullptr;
		}
	}


	void SceneManager::Update()
	{
		if (nextSceneId_ != INVALID_SCENE_ID) {
			if (currentScene_) {
				currentScene_->Finalize();
				delete currentScene_;
			}

			//
			if (nextSceneId_ == app::battle::BattleScene::GetID()) {
				currentScene_ = new app::battle::BattleScene();
				currentScene_->Initialize();
			}
			nextSceneId_ = INVALID_SCENE_ID;
		}


		if (currentScene_) {
			currentScene_->Update();
		}
	}
}