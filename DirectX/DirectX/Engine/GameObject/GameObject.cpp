#include "../EnginePreCompile.h"
#include "../Engine.h"
#include "GameObject.h"

namespace engine
{
	GameObjectManager* GameObjectManager::instance_ = nullptr;


	GameObjectManager::GameObjectManager()
		: maximumPriority_(0)
		, currentDeleteGameObjectBufferIndex_(0)
	{
	}


	GameObjectManager::~GameObjectManager()
	{
	}


	void GameObjectManager::Initialize(GameObjectPriority maximumPriority)
	{
		EngineAssert(maximumPriority < GAME_OBJECT_PRIORITY_MAX);
		maximumPriority_ = maximumPriority;
		gameObjectListArray_.resize(maximumPriority_);
		for (uint32_t i = 0; i < ArraySize(deleteGameObjectListArray_); ++i) {
			deleteGameObjectListArray_[i].resize(maximumPriority_);
		}
	}


	void GameObjectManager::Execute(graphics::RenderContext& context)
	{
		// Start
		for (auto& list : gameObjectListArray_) {
			for (auto* gameObject : list) {
				gameObject->StartWrapper();
				gameObject->StartComponent();
			}
		}

		// Update
		for (auto& list : gameObjectListArray_) {
			for (auto* gameObject : list) {
				gameObject->PreUpdate();
				gameObject->PreUpdateComponent();
			}
		}
		for (auto& list : gameObjectListArray_) {
			for (auto* gameObject : list) {
				gameObject->UpdateWapper();
				gameObject->UpdateComponent();
			}
		}
		for (auto& list : gameObjectListArray_) {
			for (auto* gameObject : list) {
				gameObject->PostUpdateWapper();
				gameObject->PostUpdateComponent();
			}
		}

		// シーングラフ更新
		UpdateSceneGraph();

		// 画面クリア
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context.OMSetRenderTargets(1, &Engine::Get().GetMainRenderTarget());
		context.ClearRenderTargetView(0, clearColor);
		context.RSSetViewport(0.0f, 0.0f, static_cast<float>(Engine::Get().GetRenderWidth()), static_cast<float>(Engine::Get().GetRenderHeight()));

		// 描画
		for (auto& list : gameObjectListArray_) {
			for (auto* gameObject : list) {
				gameObject->PreRender(context);
				gameObject->PreRenderComponent(context);
			}
		}
		for (auto& list : gameObjectListArray_) {
			for (auto* gameObject : list) {
				gameObject->Render(context);
				gameObject->RenderComponent(context);
			}
		}
		for (auto& list : gameObjectListArray_) {
			for (auto* gameObject : list) {
				gameObject->PostRender(context);
				gameObject->PostRenderComponent(context);
			}
		}
	}


	void GameObjectManager::ExecuteDeleteGameObject()
	{
		const uint32_t bufferIndex = currentDeleteGameObjectBufferIndex_;
		// バッファ切り替え
		currentDeleteGameObjectBufferIndex_ = 1 ^ currentDeleteGameObjectBufferIndex_;

		for (GameObjectList& list : deleteGameObjectListArray_[bufferIndex]) {
			for (auto* gameObject : list) {
				const auto priority = gameObject->GetPriority();
				GameObjectList& execeList = gameObjectListArray_.at(priority);
				auto it = std::find(execeList.begin(), execeList.end(), gameObject);
				if (it != execeList.end()) {
					(*it)->isRegistDeadList_ = false;
					if ((*it)->isNewFromGameObjectManager_) {
						delete (*it);
					}
				}
				execeList.erase(it);
			}
			list.clear();
		}
	}


	void GameObjectManager::UpdateSceneGraph()
	{

	}
}