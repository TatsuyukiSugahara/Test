#include "stdafx.h"
#include "Application.h"
#include "GameFlow.h"
#include "Rendering/Pipeline/PipelineBuilder.h"
#include "Rendering/Pipeline/Passes/GBufferPass.h"
#include "Rendering/Pipeline/Passes/DeferredLightingPass.h"
#include "Rendering/Pipeline/Passes/ForwardPass.h"
#include "ECS/ActorComponentSystem.h"
#include "ECS/ActorSteeringComponentSystem.h"
#include "ECS/CameraSteeringComponentSystem.h"
#include "ECS/SpeedCharacterComponentSystem.h"
#include "ECS/AutoCameraComponentSystem.h"
#include "ECS/CoinComponentSystem.h"
#include "ECS/SessionComponent.h"
#include "Component/AnimationComponentSystem.h"
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
		// ミニマップ用の俯瞰オフスクリーン。メインとは別の 2 本目のパイプラインを組む。
		// 縮小 GBuffer はこの列の GBufferPass が 512x512 で作るので、メイン解像度の深度と混ざらない。
		{
			// 描画先のカラー RT。深度は GBuffer0 が同じ寸法で持つので hasDepth は false。
			aq::graphics::RenderTargetDesc desc;
			desc.width       = OFFSCREEN_RT_WIDTH;
			desc.height      = OFFSCREEN_RT_HEIGHT;
			desc.colorFormat = aq::graphics::PixelFormat::R8G8B8A8_Unorm;
			desc.hasDepth    = false;
			minimapRT_       = aq::graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(desc);

			// 俯瞰に要るのは GBuffer → ライティング → フォワード/インスタンスだけ。
			// 影 / Hi-Z / デカール / 海 / パーティクル / ポスト / UI は積まない。
			auto deferred = std::make_shared<aq::rendering::DeferredRenderer>();
			aq::rendering::PipelineBuilder builder;
			builder.Add<aq::rendering::GBufferPass>(deferred);
			builder.Add<aq::rendering::DeferredLightingPass>(deferred);
			builder.Add<aq::rendering::ForwardPass>();
			minimapPipeline_ = builder.Build(OFFSCREEN_RT_WIDTH, OFFSCREEN_RT_HEIGHT);

			EngineAssertMsg(minimapRT_.IsValid() && minimapPipeline_,
			                "Failed to create minimap offscreen pipeline");
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

		// 標準の描画構成(Shadow + Deferred + PostProcess + Sky)をまとめて組む。
		// AquaDash で既定から変えているのは影の範囲だけ(コースが細長く、影を落としたいのは
		// プレイヤー周辺の狭い範囲なので、遠クリップと柔らかさを詰めてある)。
		{
			aq::RendererPreset preset;
			preset.shadow.nearPlane   = 0.1f;
			preset.shadow.farPlane    = 60.0f;
			preset.shadow.sceneCenter = aq::math::Vector3(0.0f, 3.0f, 0.0f);
			preset.shadow.softness    = 2.0f;
			SetupStandardRenderers(preset);
		}

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
		auto* motionBlur = renderer_.GetPipeline() ? renderer_.GetPipeline()->Find<aq::rendering::MotionBlurPass>() : nullptr;
		if (motionBlur)
		{
			constexpr float BLUR_SPEED_MIN     = 30.0f;   // [m/s] これ以下はブラーなし
			// ブースト最高速 (MAX_SPEED 83 × BOOST_SPEED_MULTIPLIER 1.50) に合わせる。
			// 83 のままだとブースト中にブラーが強まらない。
			constexpr float BLUR_SPEED_MAX     = 83.0f;   // [m/s] 通常の最高速
			constexpr float BLUR_MAX_STRENGTH  = 0.6f;    // 速度ベクトル (px) に掛けるスケール
			// ブースト域 (P21)。FOV と同じ理由で 2 段に分ける。1 本の InverseLerp をブースト最高速まで
			// 伸ばすと、通常の最高速でのブラーが従来より弱くなってしまう。
			constexpr float BLUR_BOOST_SPEED_MAX = 124.5f;  // [m/s] ブースト時の最高速
			constexpr float BLUR_BOOST_STRENGTH  = 0.25f;   // ブースト最高速での追加強度

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
						// 通常最高速を超えたぶんだけ更に強める (ブースト中だけ 0 より大きくなる)。
						const float boostRate = aq::math::Clamp01(
							aq::math::InverseLerp(BLUR_SPEED_MAX, BLUR_BOOST_SPEED_MAX, character->speed));
						strength = rate * BLUR_MAX_STRENGTH + boostRate * BLUR_BOOST_STRENGTH;
					}
				}
			}
			motionBlur->SetStrength(strength);
		}
	}


	void Application::OnRegister()
	{
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

		// データ駆動オーディオ Bank をロード（SoundClip のバンクはエンジンが既定登録済み）。
		aq::audio::LoadBank("Assets/Audio/Main.audiobank.json");
	}


	void Application::OnPreRender()
	{
		// ミニマップの俯瞰ベイク。要求が立ったフレームだけシーンをもう 1 回描く
		// (ロード完了時の 1 回きりなので走行中のコストはゼロ)。
		if (!minimapBakeRequested_) { return; }
		minimapBakeRequested_ = false;

		if (!minimapPipeline_ || !minimapRT_.IsValid() || !aq::ecs::RenderSystem::IsAvailable()) { return; }

		const aq::Camera* offscreenCamera =
			aq::CameraManager::Get().GetCamera(aq::CameraType::Offscreen);
		if (!offscreenCamera) { return; }

		aq::rendering::RenderFrame offscreenFrame;
		offscreenFrame.lighting = aq::graphics::LightManager::Get().GetLightingData();
		// 影なしの素朴なライティングにする (シャドウマップはメインカメラのカスケード用)。
		offscreenFrame.shadow = aq::rendering::MakeNeutralShadowCBData();

		// 俯瞰は全景が入るのでフラスタムカリング不要。オクリュージョンは Hi-Z が
		// メインカメラ由来で誤判定するため無効。統計はメインパスの値を潰さないよう無効。
		// インスタンスの gather はこの先行呼び出しで済ませ、メインパスは同フレーム内で再利用する。
		aq::ecs::RenderSystem::Get().BuildRenderFrame(offscreenFrame, *offscreenCamera,
			false /*frustum*/, false /*occlusion*/, false /*stats*/, true /*gather*/);

		// メインパス (Application::Render) と同じ作法で、RT / クリア / ビューポートは呼び出し側が積む。
		auto offscreenCmdList = std::make_unique<aq::rendering::RenderCommandList>();
		offscreenCmdList->Enqueue<aq::rendering::SetRenderTargetCommand>(minimapRT_);
		offscreenCmdList->Enqueue<aq::rendering::ClearRenderTargetCommand>(0u, OFFSCREEN_CLEAR_COLOR);
		offscreenCmdList->Enqueue<aq::rendering::SetViewportCommand>(
			0.0f, 0.0f, static_cast<float>(OFFSCREEN_RT_WIDTH), static_cast<float>(OFFSCREEN_RT_HEIGHT));
		minimapPipeline_->Build(offscreenFrame, *offscreenCmdList, minimapRT_,
		                        static_cast<float>(OFFSCREEN_RT_WIDTH),
		                        static_cast<float>(OFFSCREEN_RT_HEIGHT));

		// displayRT は INVALID。オフスクリーンなので Present しない。
		renderThread_.Submit(std::move(offscreenCmdList), aq::rendering::RenderTargetHandle{},
		                     offscreenFrame.lighting, offscreenFrame.shadow);
	}
}
