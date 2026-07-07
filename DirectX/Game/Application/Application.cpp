#include "stdafx.h"
#include "Application.h"
#include "GameFlow.h"
#include "ECS/ActorComponentSystem.h"
#include "ECS/ActorSteeringComponentSystem.h"
#include "ECS/CameraSteeringComponentSystem.h"
#include "UI/Font/FontResource.h"
#include "Particle/ParticleSystemData.h"
#include "Particle/ParticleLoader.h"
#include "Sound/SoundClip.h"
#include "Sound/SoundEngine.h"
#include "Sound/SoundStream.h"
#include "Sound/Component/SoundSystem.h"
#include "Sound/Authoring/Audio.h"
#ifdef AQ_DEBUG_IMGUI
#include "Core/DebugUI.h"
#include "Sound/Authoring/Debug/AudioAuthoringPanel.h"
#endif

namespace app
{
	// unique_ptr<aq::sound::SoundStream> の破棄に完全型が必要なため、ここで定義する。
	Application::Application() = default;
	Application::~Application() = default;


	bool Application::OnInitialize()
	{
		// オフスクリーン RT を生成してオフスクリーンカメラのアスペクト比を設定する。
		offscreenRTHandle_ = aq::graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(
			static_cast<uint32_t>(kOffscreenRTWidth), static_cast<uint32_t>(kOffscreenRTHeight));
		EngineAssertMsg(offscreenRTHandle_.IsValid(), "Failed to create offscreen render target");
		aq::CameraManager::Get().GetCamera(aq::CameraType::Offscreen)
			->SetViewportSize(kOffscreenRTWidth, kOffscreenRTHeight);

		GameInput::Initialize();

		// BGM: 起動時から常時ループ再生する。SoundEngine は Engine 側で本 OnInitialize より前に
		// 初期化済みのため、ここで直接ストリームを開けばよい(バンク登録に依存しない wav 直読み)。
		if (aq::sound::SoundEngine::IsAvailable()) {
			bgmStream_ = aq::sound::SoundEngine::Get().OpenStream(
				"Assets/Sound/AllBGM.wav", aq::sound::SoundBusId::BGM);
			if (bgmStream_) {
				bgmStream_->Play(aq::sound::LoopRegion{ 0, 1, 0 });   // frameCount!=0 で無限ループ
			}
		}

		// タイトル/ローディング/プレイのゲームフロー(旧 Scene を置換)。UI 画面(タイトル/ローディング)を登録し
		// タイトルを表示する。決定入力で箱 Level を非同期ロードし、完了後にプレイへ遷移する。
		app::GameFlow::Create();
		app::GameFlow::Get().Initialize();

		// Shadow renderer
		{
			const float renderW = static_cast<float>(aq::Engine::Get().GetRenderWidth());
			const float renderH = static_cast<float>(aq::Engine::Get().GetRenderHeight());

			aq::rendering::ShadowSettings shadowSettings;
			shadowSettings.resolution  = 2048;
			shadowSettings.orthoWidth  = 50.0f;
			shadowSettings.orthoHeight = 50.0f;
			shadowSettings.nearPlane   = 0.1f;
			shadowSettings.farPlane    = 60.0f;
			shadowSettings.sceneCenter = aq::math::Vector3(0.0f, 3.0f, 0.0f);
			shadowSettings.depthBias   = 0.005f;
			shadowSettings.softness    = 2.0f;

			auto shadowRenderer = std::make_unique<aq::rendering::HardShadowRenderer>();
			if (shadowRenderer->Create(shadowSettings, "Assets/Shader/ShadowDepth.fx"))
			{
				renderer_.SetShadowRenderer(std::move(shadowRenderer),
				                            aq::Engine::Get().GetMainRenderTargetHandle(),
				                            renderW, renderH);
			}
		}

		// Deferred Renderer
		{
			const uint32_t renderW = aq::Engine::Get().GetRenderWidth();
			const uint32_t renderH = aq::Engine::Get().GetRenderHeight();

			auto deferred = std::make_unique<aq::rendering::DeferredRenderer>();
			if (deferred->Create(renderW, renderH))
			{
				renderer_.SetDeferredRenderer(std::move(deferred));
			}
		}

		// Bloom
		{
			const uint32_t renderW = aq::Engine::Get().GetRenderWidth();
			const uint32_t renderH = aq::Engine::Get().GetRenderHeight();

			auto bloom = std::make_unique<aq::rendering::BloomRenderer>();
			if (bloom->Initialize(renderW, renderH))
			{
				renderer_.SetPostProcessRenderer(std::move(bloom));
			}
		}

#ifdef AQ_DEBUG_IMGUI
		// オーディオ オーサリング/デバッグパネルを DebugUI に登録する。
		if (aq::DebugUI::IsAvailable()) {
			audioPanel_ = std::make_unique<aq::audio::AudioAuthoringPanel>();
			aq::DebugUI::Get().Register(audioPanel_.get());
		}
#endif

		return true;
	}


	void Application::OnFinalize()
	{
#ifdef AQ_DEBUG_IMGUI
		if (audioPanel_ && aq::DebugUI::IsAvailable()) {
			aq::DebugUI::Get().Unregister(audioPanel_.get());
		}
		audioPanel_.reset();
#endif

		// BGM ストリームの後始末（SoundEngine 解放前に行う）。
		bgmStream_.reset();

		app::GameFlow::Release();
		GameInput::Finalize();
	}


	void Application::OnUpdate()
	{
		app::GameFlow::Get().Update(aq::Engine::GetDeltaTime());

		if (renderer_.GetShadowRenderer())
		{
			auto pos = app::GameFlow::Get().GetFocusPosition();
			pos.y += 2.0f;
			renderer_.GetShadowRenderer()->SetSceneCenter(pos);
		}
	}


	void Application::OnRegister()
	{
		aq::res::ResourceManager::RegisterBank<aq::res::GPUResource, aq::res::TResourceBank<aq::res::GPUResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::MeshResource, aq::res::TResourceBank<aq::res::MeshResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::PMDResource, aq::res::TResourceBank<aq::res::PMDResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::ShaderResource, aq::res::TResourceBank<aq::res::ShaderResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::SkeletalMeshResource, aq::res::TResourceBank<aq::res::SkeletalMeshResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::AnimationResource, aq::res::TResourceBank<aq::res::AnimationResource>>();
		aq::res::ResourceManager::RegisterBank<aq::ui::FontResource, aq::res::TResourceBank<aq::ui::FontResource>>();
		aq::res::ResourceManager::RegisterBank<aq::sound::SoundClip, aq::res::TResourceBank<aq::sound::SoundClip>>();
		aq::res::ResourceManager::RegisterBank<aq::particle::ParticleSystemData, aq::res::TResourceBank<aq::particle::ParticleSystemData>>();

		aq::res::ResourceManager::Reflection<aq::res::GPUResource, aq::res::TextureLoader>();
		aq::res::ResourceManager::Reflection<aq::res::MeshResource, aq::res::MeshLoader>();
		aq::res::ResourceManager::Reflection<aq::res::PMDResource, aq::res::PMDLoader>();
		aq::res::ResourceManager::Reflection<aq::res::ShaderResource, aq::res::ShaderLoader>();
		aq::res::ResourceManager::Reflection<aq::res::SkeletalMeshResource, aq::res::SkeletalMeshLoader>();
		aq::res::ResourceManager::Reflection<aq::res::AnimationResource, aq::res::AnimationLoader>();
		aq::res::ResourceManager::Reflection<aq::ui::FontResource, aq::ui::FontLoader>();
		aq::res::ResourceManager::Reflection<aq::sound::SoundClip, aq::sound::SoundClipLoader>();
		aq::res::ResourceManager::Reflection<aq::particle::ParticleSystemData, aq::particle::ParticleLoader>();

		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CharacterSteeringSystem>();
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::ActorStateMachineSystem>();

		aq::ecs::EntityContext::Get().AddDependency<app::ecs::ActorStateMachineSystem, app::ecs::CharacterSteeringSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::HierarcicalTransformSystem, app::ecs::ActorStateMachineSystem>();

		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CameraSteeringSystem,
			aq::ecs::HierarcicalTransformSystem>();
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CameraEffectSystem,
			app::ecs::CameraSteeringSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, app::ecs::CameraEffectSystem>();

		// サウンド: ワールド変換確定後に 3D を反映する（HierarcicalTransformSystem に依存）。
		aq::ecs::EntityContext::Get().AddSystem<aq::sound::SoundSystem,
			aq::ecs::HierarcicalTransformSystem>();

		// データ駆動オーディオ Bank をロード（SoundClip バンク登録後に行う）。
		aq::audio::LoadBank("Assets/Audio/Main.audiobank.json");
	}


	void Application::OnPreRender()
	{
		if (!offscreenRTHandle_.IsValid()) return;

		const float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		auto offscreenCmdList = std::make_unique<aq::rendering::RenderCommandList>();
		offscreenCmdList->Enqueue<aq::rendering::SetRenderTargetCommand>(offscreenRTHandle_);
		offscreenCmdList->Enqueue<aq::rendering::ClearRenderTargetCommand>(0u, clearColor);
		offscreenCmdList->Enqueue<aq::rendering::ClearDepthCommand>();
		offscreenCmdList->Enqueue<aq::rendering::SetViewportCommand>(
			0.0f, 0.0f, kOffscreenRTWidth, kOffscreenRTHeight);

		aq::rendering::RenderFrame offscreenFrame;
		offscreenFrame.lighting = aq::graphics::LightManager::Get().GetLightingData();
		aq::ecs::RenderSystem::Get().BuildRenderFrame(offscreenFrame, aq::CameraType::Offscreen);
		renderer_.BuildCommandList(offscreenFrame, *offscreenCmdList,
		                          offscreenRTHandle_, kOffscreenRTWidth, kOffscreenRTHeight, false);
		renderThread_.Submit(std::move(offscreenCmdList), aq::rendering::RenderTargetHandle{},
		                    offscreenFrame.lighting, offscreenFrame.shadow);
	}
}
