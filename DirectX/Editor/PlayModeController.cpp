#include "stdafx.h"
#include "PlayModeController.h"
#include "Core/AppHost.h"
#include "Level/LevelEditor.h"
#include "Level/LevelManager.h"
#include "Sound/SoundEngine.h"
#include "Sound/Authoring/AudioDirector.h"
#include "UI/Input/UIInputSystem.h"
#include <string>


namespace editor
{
	void PlayModeController::RequestPlay()
	{
		if (!IsRequestValid(Request::Play, aq::AppHost::Get().GetRunMode())) return;
		pendingRequest_ = Request::Play;
	}


	void PlayModeController::RequestPause()
	{
		if (!IsRequestValid(Request::Pause, aq::AppHost::Get().GetRunMode())) return;
		pendingRequest_ = Request::Pause;
	}


	void PlayModeController::RequestResume()
	{
		if (!IsRequestValid(Request::Resume, aq::AppHost::Get().GetRunMode())) return;
		pendingRequest_ = Request::Resume;
	}


	void PlayModeController::RequestStop()
	{
		if (!IsRequestValid(Request::Stop, aq::AppHost::Get().GetRunMode())) return;
		pendingRequest_ = Request::Stop;
	}


	void PlayModeController::ApplyPendingRequest()
	{
		const Request request = pendingRequest_;
		pendingRequest_ = Request::None;

		// 要求を受け付けた後に別の要求でモードが変わっていることがあるので、実行の時点でもう一度確かめる
		if (!IsRequestValid(request, aq::AppHost::Get().GetRunMode())) return;

		switch (request) {
		case Request::Play:   BeginPlay();  break;
		case Request::Pause:  PausePlay();  break;
		case Request::Resume: ResumePlay(); break;
		case Request::Stop:   StopPlay();   break;
		default: break;
		}
	}


	bool PlayModeController::IsWorldSpawned() const
	{
		const aq::level::LevelManager& levelManager = aq::level::LevelManager::Get();
		for (const aq::level::LevelId& id : restoredLevelIds_) {
			// 読み直しに失敗した Level(無効な LevelId)は生成されないので、ここで false になる
			if (!levelManager.IsSpawned(id)) return false;
		}

		if (hadPreview_) {
			const aq::level::LevelEditModel* model = FindLevelEditModel();
			if (model == nullptr) return false;
			if (!levelManager.IsSpawned(model->GetPreviewLevelId())) return false;
		}
		return true;
	}


	bool PlayModeController::IsRequestValid(const Request request, const aq::RunMode mode)
	{
		switch (request) {
		case Request::Play:   return mode == aq::RunMode::Edit;
		case Request::Pause:  return mode == aq::RunMode::Play;
		case Request::Resume: return mode == aq::RunMode::Pause;
		case Request::Stop:   return mode == aq::RunMode::Play || mode == aq::RunMode::Pause;
		default:              return false;
		}
	}


	void PlayModeController::BeginPlay()
	{
		aq::AppHost& appHost = aq::AppHost::Get();
		aq::level::LevelManager& levelManager = aq::level::LevelManager::Get();

		// GPU が前フレームを実行中のことがあるので、エンティティを破棄する前に完了を待つ
		appHost.WaitForRenderIdle();
		aq::ecs::EntityContext::Get().FlushPendingCommands();

		// 編集中の Level のプレビューは Edit の表示用なので、再生中はアンロードしておく(停止で作り直す)
		aq::level::LevelEditModel* model = FindLevelEditModel();
		hadPreview_ = (model != nullptr) && model->ClearPreview();

		// Edit のワールドを記録する(プレビューは外した後なので含まれない)
		{
			savedLevelKeys_.clear();
			restoredLevelIds_.clear();
			std::vector<aq::level::LevelId> roots;
			levelManager.CollectLoadedRoots(roots);
			for (const aq::level::LevelId& id : roots) {
				savedLevelKeys_.push_back(levelManager.GetSourceKey(id));
			}
		}
		{
			aq::CameraManager& cameraManager = aq::CameraManager::Get();
			const aq::Camera* mainCamera      = cameraManager.GetCamera(aq::CameraType::Main);
			const aq::Camera* offscreenCamera = cameraManager.GetCamera(aq::CameraType::Offscreen);
			hasSavedMainCamera_      = (mainCamera != nullptr);
			hasSavedOffscreenCamera_ = (offscreenCamera != nullptr);
			if (hasSavedMainCamera_)      savedMainCamera_      = mainCamera->CaptureState();
			if (hasSavedOffscreenCamera_) savedOffscreenCamera_ = offscreenCamera->CaptureState();
		}

		// 再生はゲームを最初から始めるので、ワールドを空にしてから OnPlayBegin を呼ぶ
		ClearWorld();

		aq::level::LevelEditorPanel* panel = appHost.GetLevelEditorPanel();
		if (panel != nullptr) {
			panel->SetLocked(true);
		}

		appHost.SetRunMode(aq::RunMode::Play);
		appHost.GetModule().OnPlayBegin();

		const std::string message = "[editor] play begin: levels=" + std::to_string(savedLevelKeys_.size())
		                          + " preview=" + (hadPreview_ ? "1" : "0");
		aq::StartupLog(message.c_str());
	}


	void PlayModeController::PausePlay()
	{
		aq::AppHost::Get().SetRunMode(aq::RunMode::Pause);
		if (aq::sound::SoundEngine::IsAvailable()) {
			aq::sound::SoundEngine::Get().PauseAll();
		}
		aq::StartupLog("[editor] pause");
	}


	void PlayModeController::ResumePlay()
	{
		if (aq::sound::SoundEngine::IsAvailable()) {
			aq::sound::SoundEngine::Get().ResumeAll();
		}
		aq::AppHost::Get().SetRunMode(aq::RunMode::Play);
		aq::StartupLog("[editor] resume");
	}


	void PlayModeController::StopPlay()
	{
		aq::AppHost& appHost = aq::AppHost::Get();
		aq::level::LevelManager& levelManager = aq::level::LevelManager::Get();
		const bool hasSoundEngine   = aq::sound::SoundEngine::IsAvailable();
		const bool hasAudioDirector = aq::audio::AudioDirector::IsAvailable();

		// サウンドを止める。一時停止中のボイスは先に再開してから止める(一時停止のまま残さないため)
		if (appHost.GetRunMode() == aq::RunMode::Pause && hasSoundEngine) {
			aq::sound::SoundEngine::Get().ResumeAll();
		}
		appHost.GetModule().OnPlayEnd();
		if (hasAudioDirector) {
			aq::audio::AudioDirector::Get().StopAll();
		}
		if (hasSoundEngine) {
			aq::sound::SoundEngine::Get().StopAll();
		}

		appHost.WaitForRenderIdle();
		aq::ecs::EntityContext::Get().FlushPendingCommands();

		ClearWorld();

		// ゲームが積んだ画面と、UI の入力の途中状態を捨てる
		{
			aq::ui::UIContext& ui = aq::ui::UIContext::Get();
			ui.Screens().Clear();
			ui.GetInputSystem().ClearState();
		}

		// カメラを再生前の状態へ戻す
		{
			aq::CameraManager& cameraManager = aq::CameraManager::Get();
			aq::Camera* mainCamera      = cameraManager.GetCamera(aq::CameraType::Main);
			aq::Camera* offscreenCamera = cameraManager.GetCamera(aq::CameraType::Offscreen);
			if (hasSavedMainCamera_ && mainCamera != nullptr)           mainCamera->RestoreState(savedMainCamera_);
			if (hasSavedOffscreenCamera_ && offscreenCamera != nullptr) offscreenCamera->RestoreState(savedOffscreenCamera_);
		}

		// 記録した Level を記録の順に読み直す(生成は次の Flush。失敗した分も無効な LevelId のまま覚え、完了の判定で拾う)
		restoredLevelIds_.clear();
		for (const std::string& key : savedLevelKeys_) {
			restoredLevelIds_.push_back(levelManager.Load(key));
		}

		// プレビューはここで直接作り直す。Level エディタの DebugRender は、ゲーム用のデバッグ UI を隠すと呼ばれないため
		aq::level::LevelEditorPanel* panel = appHost.GetLevelEditorPanel();
		if (hadPreview_ && panel != nullptr) {
			aq::level::LevelEditModel& model = panel->GetModel();
			model.RequestPreview();
			model.ProcessPreviewRequest();
		}
		if (panel != nullptr) {
			panel->SetLocked(false);
		}

		appHost.SetRunMode(aq::RunMode::Edit);

		const std::string message = "[editor] stop: levels=" + std::to_string(savedLevelKeys_.size())
		                          + " preview=" + (hadPreview_ ? "1" : "0");
		aq::StartupLog(message.c_str());
	}


	void PlayModeController::ClearWorld()
	{
		aq::ecs::EntityContext& context = aq::ecs::EntityContext::Get();

		// Level と、その子 Level・非同期ロードを片付ける
		aq::level::LevelManager::Get().UnloadAll();
		context.FlushPendingCommands();

		// Level を使わずに作られたエンティティ(ゲームのステージ・セッションなど)も残さない
		std::vector<aq::ecs::EntityHandle> handles;
		context.CollectAllHandles(handles);
		for (const aq::ecs::EntityHandle& handle : handles) {
			if (context.IsValid(handle)) {
				context.RequestDestroyEntity(handle);
			}
		}
		context.FlushPendingCommands();
	}


	aq::level::LevelEditModel* PlayModeController::FindLevelEditModel()
	{
		aq::level::LevelEditorPanel* panel = aq::AppHost::Get().GetLevelEditorPanel();
		return (panel != nullptr) ? &panel->GetModel() : nullptr;
	}
}
