#include "stdafx.h"
#include "Application.h"
#include "GameFlow.h"
#include "ECS/ActorComponentSystem.h"
#include "ECS/ActorSteeringComponentSystem.h"
#include "ECS/CameraSteeringComponentSystem.h"
#include "ECS/SpeedCharacterComponentSystem.h"
#include "ECS/AutoCameraComponentSystem.h"
#include "ECS/CoinComponentSystem.h"
#include "ECS/SessionComponent.h"
#include "Component/AnimationComponentSystem.h"
#include "UI/Font/FontResource.h"
#include "Resource/ParticleSystemData.h"
#include "Resource/ParticleLoader.h"
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
	Application* Application::instance_ = nullptr;


	// unique_ptr<aq::sound::SoundStream> の破棄に完全型が必要なため、ここで定義する。
	Application::Application()
	{
		instance_ = this;
	}


	Application::~Application()
	{
		instance_ = nullptr;
	}


	bool Application::OnInitialize()
	{
		// ミニマップ用の俯瞰オフスクリーンパス (縮小 GBuffer を内包するので
		// メイン解像度の深度と混ざらない)。背景はミニマップ下地と同じ暗い青。
		if (offscreenPass_.Create(OFFSCREEN_RT_WIDTH, OFFSCREEN_RT_HEIGHT)) {
			offscreenPass_.SetClearColor(aq::math::Vector4(0.02f, 0.08f, 0.16f, 1.0f));
		} else {
			EngineAssertMsg(false, "Failed to create offscreen scene pass");
		}
		aq::CameraManager::Get().GetCamera(aq::CameraType::Offscreen)
			->SetViewportSize(static_cast<float>(OFFSCREEN_RT_WIDTH),
			                  static_cast<float>(OFFSCREEN_RT_HEIGHT));

		GameInput::Initialize();

		// タイトル/ローディング/プレイのゲームフロー(旧 Scene を置換)。UI 画面(タイトル/ローディング)を登録し
		// タイトルを表示する。決定入力で箱 Level を非同期ロードし、完了後にプレイへ遷移する。
		app::GameFlow::Create();
		app::GameFlow::Get().Initialize();
		aq::StartupMark("    [game] GameFlow ok");

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
		aq::StartupMark("    [game] Shadow ok (VS x1)");

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
		aq::StartupMark("    [game] Deferred ok (shaders x4 + GBuffer)");

		// Bloom
		{
			const uint32_t renderW = aq::Engine::Get().GetRenderWidth();
			const uint32_t renderH = aq::Engine::Get().GetRenderHeight();

			auto bloom = std::make_unique<aq::rendering::PostProcessChain>();
			if (bloom->Initialize(renderW, renderH))
			{
				// カメラモーションブラー用に GBuffer2 (worldPos) を渡す (ディファード有効時のみ)。
				if (auto* dr = dynamic_cast<aq::rendering::DeferredRenderer*>(renderer_.GetDeferredRenderer())) {
					bloom->SetWorldPosRT(dr->GetGBuffer2Handle());
				}
				renderer_.SetPostProcessRenderer(std::move(bloom));
			}
		}
		aq::StartupMark("    [game] Bloom ok (CS x6)");

		// BGM: 起動時から常時ループ再生する(バンク登録に依存しない wav 直読み)。
		// SoundEngine の初期化は Engine が別スレッドで進めているので、ここで合流してから開く。
		// レンダラ初期化(シェーダコンパイル)の後ろに置くことで、その間もサウンド初期化が並走する。
		if (aq::Engine::Get().EnsureSoundInitialized() && aq::sound::SoundEngine::IsAvailable()) {
			// AquaDash 用に生成したシンセループ (Tools/generate_bgm.py)。
			bgmStream_ = aq::sound::SoundEngine::Get().OpenStream(
				"Assets/Sound/AquaDashBGM.wav", aq::sound::SoundBusId::BGM);
			if (bgmStream_) {
				bgmStream_->Play(aq::sound::LoopRegion{ 0, 1, 0 });   // frameCount!=0 で無限ループ
			}
		}
		aq::StartupMark("    [game] BGM stream opened (sound joined)");

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

		// スピード感演出: 速度に応じたカメラモーションブラー強度 (タイトル/リザルトでは 0 で無効)。
		if (auto* postProcess = renderer_.GetPostProcessRenderer())
		{
			constexpr float BLUR_SPEED_MIN     = 30.0f;   // [m/s] これ以下はブラーなし
			constexpr float BLUR_SPEED_MAX     = 83.0f;   // [m/s] 最高速
			constexpr float BLUR_MAX_STRENGTH  = 0.6f;    // 速度ベクトル (px) に掛けるスケール

			float strength = 0.0f;
			// セッション状態はここでは読み取りのみ (書き込みは GameFlow の状態クラス)。
			const auto* session =
				aq::ecs::EntityContext::Get().GetSingletonComponent<const app::ecs::SessionComponent>();
			if (session && session->activeStage && !session->gameplayPaused)
			{
				auto& ctx = aq::ecs::EntityContext::Get();
				if (ctx.IsValid(session->playerHandle)) {
					if (const auto* character =
							ctx.GetComponent<app::ecs::SpeedCharacterComponent>(session->playerHandle)) {
						const float rate = aq::math::Clamp01(
							aq::math::InverseLerp(BLUR_SPEED_MIN, BLUR_SPEED_MAX, character->speed));
						strength = rate * BLUR_MAX_STRENGTH;
					}
				}
			}
			postProcess->SetMotionBlurStrength(strength);
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
		aq::res::ResourceManager::RegisterBank<aq::res::ParticleSystemData, aq::res::TResourceBank<aq::res::ParticleSystemData>>();

		aq::res::ResourceManager::Reflection<aq::res::GPUResource, aq::res::TextureLoader>();
		aq::res::ResourceManager::Reflection<aq::res::MeshResource, aq::res::MeshLoader>();
		aq::res::ResourceManager::Reflection<aq::res::PMDResource, aq::res::PMDLoader>();
		aq::res::ResourceManager::Reflection<aq::res::ShaderResource, aq::res::ShaderLoader>();
		aq::res::ResourceManager::Reflection<aq::res::SkeletalMeshResource, aq::res::SkeletalMeshLoader>();
		aq::res::ResourceManager::Reflection<aq::res::AnimationResource, aq::res::AnimationLoader>();
		aq::res::ResourceManager::Reflection<aq::ui::FontResource, aq::ui::FontLoader>();
		aq::res::ResourceManager::Reflection<aq::sound::SoundClip, aq::sound::SoundClipLoader>();
		aq::res::ResourceManager::Reflection<aq::res::ParticleSystemData, aq::res::ParticleLoader>();

		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CharacterSteeringSystem>();
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::ActorStateMachineSystem>();

		aq::ecs::EntityContext::Get().AddDependency<app::ecs::ActorStateMachineSystem, app::ecs::CharacterSteeringSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::HierarcicalTransformSystem, app::ecs::ActorStateMachineSystem>();

		// AquaDash: 入力転写 → スプライン走行 → ワールド変換の順で流す。
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::PlayerInputSystem>();
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::SpeedCharacterSystem,
			app::ecs::PlayerInputSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::HierarcicalTransformSystem, app::ecs::SpeedCharacterSystem>();
		// AquaDash: 走行状態からアニメを切り替えるため、アニメ更新は走行更新の後段に固定する。
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::AnimationSystem, app::ecs::SpeedCharacterSystem>();

		// AquaDash: コイン判定は走行結果の位置を使うため SpeedCharacterSystem の後、ワールド変換の前。
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CoinSystem,
			app::ecs::SpeedCharacterSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::HierarcicalTransformSystem, app::ecs::CoinSystem>();

		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CameraSteeringSystem,
			aq::ecs::HierarcicalTransformSystem>();
		// AquaDash: 自動カメラは走行結果の distance を使うため SpeedCharacterSystem の後。
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::AutoCameraSystem,
			app::ecs::SpeedCharacterSystem>();
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CameraEffectSystem,
			app::ecs::CameraSteeringSystem>();
		aq::ecs::EntityContext::Get().AddDependency<app::ecs::CameraEffectSystem, app::ecs::AutoCameraSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, app::ecs::CameraEffectSystem>();

		// サウンド: ワールド変換確定後に 3D を反映する（HierarcicalTransformSystem に依存）。
		aq::ecs::EntityContext::Get().AddSystem<aq::sound::SoundSystem,
			aq::ecs::HierarcicalTransformSystem>();

		// データ駆動オーディオ Bank をロード（SoundClip バンク登録後に行う）。
		aq::audio::LoadBank("Assets/Audio/Main.audiobank.json");
	}


	void Application::OnPreRender()
	{
		// ミニマップの俯瞰ベイク。要求が立ったフレームだけシーンをもう 1 回描く
		// (ロード完了時の 1 回きりなので走行中のコストはゼロ)。
		if (!minimapBakeRequested_) { return; }
		minimapBakeRequested_ = false;

		if (!offscreenPass_.IsReady() || !aq::ecs::RenderSystem::IsAvailable()) { return; }

		const aq::Camera* offscreenCamera =
			aq::CameraManager::Get().GetCamera(aq::CameraType::Offscreen);
		if (!offscreenCamera) { return; }

		aq::rendering::RenderFrame offscreenFrame;
		offscreenFrame.lighting = aq::graphics::LightManager::Get().GetLightingData();
		// 影なしの素朴なライティングにする (シャドウマップはメインカメラのカスケード用)。
		offscreenFrame.shadow = aq::rendering::OffscreenScenePass::MakeNeutralShadowCBData();

		// 俯瞰は全景が入るのでフラスタムカリング不要。オクリュージョンは Hi-Z が
		// メインカメラ由来で誤判定するため無効。統計はメインパスの値を潰さないよう無効。
		// インスタンスの gather はこの先行呼び出しで済ませ、メインパスは同フレーム内で再利用する。
		aq::ecs::RenderSystem::Get().BuildRenderFrame(offscreenFrame, *offscreenCamera,
			false /*frustum*/, false /*occlusion*/, false /*stats*/, true /*gather*/);

		auto offscreenCmdList = std::make_unique<aq::rendering::RenderCommandList>();
		offscreenPass_.BuildCommandList(offscreenFrame, *offscreenCmdList);

		// displayRT は INVALID。オフスクリーンなので Present しない。
		renderThread_.Submit(std::move(offscreenCmdList), aq::rendering::RenderTargetHandle{},
		                     offscreenFrame.lighting, offscreenFrame.shadow);
	}
}
