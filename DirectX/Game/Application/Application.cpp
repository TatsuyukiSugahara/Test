#include "stdafx.h"
#include "Application.h"
#include "GameFlow.h"
#include "ECS/ActorComponentSystem.h"
#include "ECS/ActorSteeringComponentSystem.h"
#include "ECS/CameraSteeringComponentSystem.h"
#include "UI/Font/FontResource.h"
#include "Sound/SoundClip.h"
#include "Sound/SoundEngine.h"
#include "Sound/SoundStream.h"
#include "Sound/SoundSource.h"
#include "Sound/Component/SoundSystem.h"
#include "Sound/Authoring/Audio.h"
#include "Sound/Video/VideoPlayer.h"
#ifdef AQ_DEBUG_IMGUI
#include "Core/DebugUI.h"
#include "Sound/Authoring/Debug/AudioAuthoringPanel.h"
#endif
#include <cmath>

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

		// サウンドテストの後始末（SoundEngine 解放前に行う）。
		videoPlayer_.reset();
		bgmStream_.reset();
		if (orbitSource_.IsValid() && aq::sound::SoundEngine::IsAvailable()) {
			aq::sound::SoundEngine::Get().DestroySource(orbitSource_);
		}

		app::GameFlow::Release();
		GameInput::Finalize();
	}


	void Application::UpdateSoundTest()
	{
		if (!aq::sound::SoundEngine::IsAvailable()) {
			return;
		}
		aq::sound::SoundEngine& sound = aq::sound::SoundEngine::Get();

		// 遅延ロード（バンク登録は OnRegister で行われ OnInitialize より後のため、ここで取得）。
		if (!testClip_) {
			testClip_ = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>("Assets/Sound/beep.wav");
		}

		// GetAsyncKeyState の最下位ビット = 前回呼び出し以降に押されたか（簡易エッジ検出）。
		// UWP(Xbox) では GetAsyncKeyState が使えないため、デバッグ入力は当面無効（Phase 4 の GameInput 後に対応）。
#if defined(AQ_PLATFORM_UWP)
		auto triggered = [](int) { return false; };
#else
		auto triggered = [](int vk) { return (GetAsyncKeyState(vk) & 1) != 0; };
#endif

		// P: 2D ワンショット再生
		if (triggered('P') && testClip_ && testClip_->IsCompleted()) {
			sound.Play(testClip_, aq::sound::SoundBusId::SE);
		}

		// B: BGM ループのトグル（1 秒フェードイン / フェードアウト）
		if (triggered('B')) {
			if (!bgmPlaying_) {
				bgmStream_ = sound.OpenStream("Assets/Sound/bgm.wav", aq::sound::SoundBusId::BGM);
				if (bgmStream_) {
					bgmStream_->Play(aq::sound::LoopRegion{ 0, 1, 0 });
					bgmStream_->FadeIn(1.0f, 0.7f);
					bgmPlaying_ = true;
				}
			}
			else if (bgmStream_) {
				bgmStream_->FadeOut(1.0f);   // フェード完了時に自動停止
				bgmPlaying_ = false;
			}
		}

		// M: 全体ポーズ / 再開のトグル
		if (triggered('M')) {
			if (!soundPaused_) { sound.PauseAll();  soundPaused_ = true;  }
			else               { sound.ResumeAll(); soundPaused_ = false; }
		}

		// ── データ駆動オーディオ層（イベント）テスト ──
		// E: イベント "Test_Beep"（Kind=SE, 120ms クールダウン）
		if (triggered('E')) {
			aq::audio::PostEvent("Test_Beep");
		}
		// G: BGM イベント トグル（Play_TitleBGM / Stop_TitleBGM, 1秒フェード）
		if (triggered('G')) {
			if (!eventBgmOn_) { aq::audio::PostEvent("Play_TitleBGM"); eventBgmOn_ = true; }
			else              { aq::audio::PostEvent("Stop_TitleBGM"); eventBgmOn_ = false; }
		}
		// 1 / 2: 地面（Surface）スイッチを切り替え（Grass / Wood）
		if (triggered('1')) { aq::audio::SetSwitch("Surface", "Grass"); }
		if (triggered('2')) { aq::audio::SetSwitch("Surface", "Wood"); }
		// F: 足音（Switch→Random。Surface に応じて grass/wood が選ばれ、毎回別の音・ピッチ/音量変化）
		if (triggered('F')) {
			aq::audio::PostEvent("Player_Footstep");
		}
		// T: アルペジオ（Sequence コンテナ。押すたび C→E→G→… と進む）
		if (triggered('T')) {
			aq::audio::PostEvent("Arpeggio_Step");
		}
		// 7: 3D イベント音（オーサリング層経由）の周回トグル。GameObject=99 を周回させ Beep3D をループ再生
		if (triggered('7')) {
			if (!event3DOn_) {
				sound.GetListener().SetPosition(aq::math::Vector3(0.0f, 0.0f, 0.0f));
				sound.GetListener().SetOrientation(aq::math::Vector3(0.0f, 0.0f, 1.0f),
				                                   aq::math::Vector3(0.0f, 1.0f, 0.0f));
				event3DAngle_ = 0.0f;
				aq::audio::SetGameObjectTransform(99, aq::math::Vector3(8.0f, 0.0f, 0.0f),
				    aq::math::Vector3(0.0f, 0.0f, 1.0f), aq::math::Vector3(0.0f, 1.0f, 0.0f),
				    aq::math::Vector3(0.0f, 0.0f, 0.0f));
				aq::audio::PostEvent("Play_Beep3D", 99);
				event3DOn_ = true;
			}
			else {
				aq::audio::UnregisterGameObject(99);   // 紐づく 3D ループ音を停止
				event3DOn_ = false;
			}
		}
		// 9 / 0: RTPC "Pitch3D" を変更（SE3D=Beep3D のピッチが連続変化。7 で再生中に試す）
		if (triggered('9')) { aq::audio::SetRTPC("Pitch3D", 0.0f); }
		if (triggered('0')) { aq::audio::SetRTPC("Pitch3D", 1.0f); }
		// C / X: State "Music" を Combat / Explore に切替（stateRules で BGM クロスフェード）
		if (triggered('C')) { aq::audio::SetState("Music", "Combat"); }
		if (triggered('X')) { aq::audio::SetState("Music", "Explore"); }
		// Y: セリフ（Voice バス）再生。再生中は VoiceDuck で BGM が自動的に下がる
		if (triggered('Y')) { aq::audio::PostEvent("Play_Voice"); }
		// U: エンジン音（Blend: 低音ループ + RTPC 制御の高音ループ）のトグル
		if (triggered('U')) {
			if (!engineOn_) { aq::audio::PostEvent("Play_Engine"); engineOn_ = true; }
			else            { aq::audio::PostEvent("Stop_Engine"); engineOn_ = false; }
		}
		// J / K: RTPC "EngineLoad"（Blend 高音レイヤの音量。U で鳴らしてる間に試す）
		if (triggered('J')) { aq::audio::SetRTPC("EngineLoad", 0.0f); }
		if (triggered('K')) { aq::audio::SetRTPC("EngineLoad", 1.0f); }
		// I: 3D アンビエンス×4 を spawn/stop（Ambience3D は maxVoices=2 + virtualize）。
		//    Audio パネルの Active Voices で 2 実 + 2 virtual(橙) が確認できる。
		if (triggered('I')) {
			if (!ambienceOn_) {
				sound.GetListener().SetPosition(aq::math::Vector3(0.0f, 0.0f, 0.0f));
				sound.GetListener().SetOrientation(aq::math::Vector3(0.0f, 0.0f, 1.0f),
				                                   aq::math::Vector3(0.0f, 1.0f, 0.0f));
				const float px[4] = { 5.0f, -5.0f, 0.0f, 0.0f };
				const float pz[4] = { 0.0f, 0.0f, 5.0f, -5.0f };
				for (int i = 0; i < 4; ++i) {
					const uint64_t go = 101 + i;
					aq::audio::SetGameObjectTransform(go, aq::math::Vector3(px[i], 0.0f, pz[i]),
					    aq::math::Vector3(0.0f, 0.0f, 1.0f), aq::math::Vector3(0.0f, 1.0f, 0.0f),
					    aq::math::Vector3(0.0f, 0.0f, 0.0f));
					aq::audio::PostEvent("Play_Amb3D", go);
				}
				ambienceOn_ = true;
			}
			else {
				for (int i = 0; i < 4; ++i) { aq::audio::UnregisterGameObject(101 + i); }
				ambienceOn_ = false;
			}
		}
		// O: GameObject 101 を停止（実ボイスが空き、仮想の1つが昇格するのを Audio パネルで確認）
		if (triggered('O')) { aq::audio::UnregisterGameObject(101); }
		// N: VideoPlayer（動画音声側）のトグル。メディアの音声トラックを MF デコード→プッシュ供給。
		//    ここでは bgm.wav を再生（mp4 を置けば動画の音声も同経路で鳴る）。
		if (triggered('N')) {
			if (!videoPlayer_) {
				videoPlayer_ = std::make_unique<aq::video::VideoPlayer>();
				if (!videoPlayer_->Open("Assets/Sound/bgm.wav", aq::sound::SoundBusId::BGM)) {
					videoPlayer_.reset();
				}
			}
			else {
				videoPlayer_.reset();   // 停止
			}
		}
		// VideoPlayer の音声供給を毎フレーム回す。
		if (videoPlayer_) {
			videoPlayer_->Update(aq::Engine::GetDeltaTime());
		}
		// GameObject 99 を周回させる（パン + ドップラーをオーサリング層で確認）
		if (event3DOn_) {
			const float angularSpeed = 2.0f;
			const float radius       = 8.0f;
			event3DAngle_ += aq::Engine::GetDeltaTime() * angularSpeed;
			aq::audio::SetGameObjectTransform(99,
			    aq::math::Vector3(cosf(event3DAngle_) * radius, 0.0f, sinf(event3DAngle_) * radius),
			    aq::math::Vector3(0.0f, 0.0f, 1.0f), aq::math::Vector3(0.0f, 1.0f, 0.0f),
			    aq::math::Vector3(-sinf(event3DAngle_) * radius * angularSpeed, 0.0f,
			                       cosf(event3DAngle_) * radius * angularSpeed));
		}

		// 3: 3D 周回ソースのトグル（パン + ドップラーの確認）
		if (triggered('3')) {
			if (!orbitActive_ && testClip_ && testClip_->IsCompleted()) {
				sound.GetListener().SetPosition(aq::math::Vector3(0.0f, 0.0f, 0.0f));
				sound.GetListener().SetOrientation(aq::math::Vector3(0.0f, 0.0f, 1.0f),
				                                   aq::math::Vector3(0.0f, 1.0f, 0.0f));
				orbitSource_ = sound.CreateSource(testClip_, aq::sound::SoundBusId::SE);
				if (aq::sound::SoundSource* src = sound.Resolve(orbitSource_)) {
					src->SetAttenuation(aq::sound::AttenuationModel::Inverse);
					src->SetDistances(1.0f, 40.0f);
					src->SetDopplerFactor(1.0f);
					src->Play(aq::sound::LoopRegion{ 0, 1, 0 });
					orbitActive_ = true;
				}
			}
			else if (orbitActive_) {
				sound.DestroySource(orbitSource_);
				orbitSource_ = aq::sound::SoundSourceHandle{};
				orbitActive_ = false;
			}
		}

		// 周回ソースの位置・速度を更新（SoundEngine::Update が定位を反映する）。
		if (orbitActive_) {
			if (aq::sound::SoundSource* src = sound.Resolve(orbitSource_)) {
				const float angularSpeed = 2.0f;   // rad/s
				const float radius       = 8.0f;
				orbitAngle_ += aq::Engine::GetDeltaTime() * angularSpeed;

				src->SetPosition(aq::math::Vector3(cosf(orbitAngle_) * radius, 0.0f, sinf(orbitAngle_) * radius));
				src->SetVelocity(aq::math::Vector3(-sinf(orbitAngle_) * radius * angularSpeed, 0.0f,
				                                    cosf(orbitAngle_) * radius * angularSpeed));
			}
			else {
				orbitActive_ = false;
			}
		}
	}


	void Application::OnUpdate()
	{
		UpdateSoundTest();

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

		aq::res::ResourceManager::Reflection<aq::res::GPUResource, aq::res::TextureLoader>();
		aq::res::ResourceManager::Reflection<aq::res::MeshResource, aq::res::MeshLoader>();
		aq::res::ResourceManager::Reflection<aq::res::PMDResource, aq::res::PMDLoader>();
		aq::res::ResourceManager::Reflection<aq::res::ShaderResource, aq::res::ShaderLoader>();
		aq::res::ResourceManager::Reflection<aq::res::SkeletalMeshResource, aq::res::SkeletalMeshLoader>();
		aq::res::ResourceManager::Reflection<aq::res::AnimationResource, aq::res::AnimationLoader>();
		aq::res::ResourceManager::Reflection<aq::ui::FontResource, aq::ui::FontLoader>();
		aq::res::ResourceManager::Reflection<aq::sound::SoundClip, aq::sound::SoundClipLoader>();

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
