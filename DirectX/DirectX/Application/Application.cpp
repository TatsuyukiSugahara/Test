#include "Utility.h"
#include "Application.h"
#include "Scene/Scene.h"
#include "GameInput.h"
#include "Engine.h"
#include "HID/Input.h"
#include "Graphics/Camera.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/LightManager.h"
#include "Resource/Resource.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/FrameCommands.h"
#include "Rendering/Shadow/HardShadowRenderer.h"

#include "ECS/ECS.h"
#include "Component/TransformComponentSystem.h"
#include "Component/BodyComponentSystem.h"
#include "ECS/ActorComponentSystem.h"
#include "ECS/ActorSteeringComponentSystem.h"

namespace app
{
	Application::Application()
	{
	}


	Application::~Application()
	{
	}


	bool Application::Initialize(engine::graphics::RenderContext& renderContext)
	{
		// レンダースレッドを Initialize 時に起動する。
		// renderContext はエンジン生存期間中アドレスが固定されているため、ポインタを保持して安全。
		// Update() は RenderContext を受け取らないので、ゲームコードが immediate context に
		// 直接触れる経路がなくなる。
		renderThread_.Initialize(&renderContext);
		renderThreadReady_ = true;

		// Resource management
		engine::res::ResourceManager::Initialize();
		// ECS
		engine::ecs::EntityContext::Initialize();
		// Input
		engine::hid::InputManager::Initialize();
		if (FAILED(engine::hid::InputManager::Get().Setup()))
		{
			EngineAssertMsg(false, "InputManager::Setup failed: DirectInput or device initialization failed");
			return false;
		}
		// Camera
		engine::CameraManager::Initialize();
		// Light
		engine::graphics::LightManager::Initialize();

		// オフスクリーン RT を生成してオフスクリーンカメラのアスペクト比を設定する。
		// サイズ変更時は RT を再生成し SetViewportSize() を再度呼ぶこと。
		offscreenRTHandle_ = engine::graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(
			static_cast<uint32_t>(kOffscreenRTWidth), static_cast<uint32_t>(kOffscreenRTHeight));
		EngineAssertMsg(offscreenRTHandle_.IsValid(), "Failed to create offscreen render target");
		engine::CameraManager::Get().GetCamera(engine::CameraType::Offscreen)
			->SetViewportSize(kOffscreenRTWidth, kOffscreenRTHeight);

		// GameInput (ActionInput + ActionMap)
		GameInput::Initialize();

		// Scene
		app::SceneManager::Create();

		// Shadow renderer
		{
			const float renderW = static_cast<float>(engine::Engine::Get().GetRenderWidth());
			const float renderH = static_cast<float>(engine::Engine::Get().GetRenderHeight());

			engine::rendering::ShadowSettings shadowSettings;
			shadowSettings.resolution  = 2048;
			shadowSettings.orthoWidth  = 20.0f;
			shadowSettings.orthoHeight = 20.0f;
			shadowSettings.nearPlane   = 0.1f;
			shadowSettings.farPlane    = 60.0f;
			shadowSettings.sceneCenter = engine::math::Vector3(0.0f, 3.0f, 0.0f);
			shadowSettings.depthBias   = 0.005f;

			auto shadowRenderer = std::make_unique<engine::rendering::HardShadowRenderer>();
			if (shadowRenderer->Create(shadowSettings, "Assets/Shader/ShadowDepth.fx"))
			{
				renderer_.SetShadowRenderer(std::move(shadowRenderer), renderW, renderH);
			}
		}

		return true;
	}


	void Application::Finalize()
	{
		if (renderThreadReady_)
		{
			renderThread_.Finalize();
			renderThreadReady_ = false;
		}

		// Scene
		app::SceneManager::Release();
		// GameInput
		GameInput::Finalize();
		// Light
		engine::graphics::LightManager::Finalize();
		// ECS
		engine::ecs::EntityContext::Finalize();
		// Resources
		engine::res::ResourceManager::Finalize();
		// Input
		engine::hid::InputManager::Finalize();
	}


	void Application::Update()
	{
		// Input update — ECS/Scene より先に更新して今フレームの入力を同一フレームで使えるようにする
		engine::hid::InputManager::Get().Update();

		// System update
		engine::ecs::EntityContext::Get().Update();

		// Resource manager update
		engine::res::ResourceManager::Get().Update();
		// Scene update
		app::SceneManager::Get().Update();

		// Rebuild camera matrices after all scene/app camera changes are done
		engine::CameraManager::Get().UpdateAll();

		// Render
		Render();
	}


	void Application::Register()
	{
		// Resource registration
		engine::res::ResourceManager::RegisterBank<engine::res::GPUResource, engine::res::TResourceBank<engine::res::GPUResource>>();
		engine::res::ResourceManager::RegisterBank<engine::res::MeshResource, engine::res::TResourceBank<engine::res::MeshResource>>();
		engine::res::ResourceManager::RegisterBank<engine::res::PMDResource, engine::res::TResourceBank<engine::res::PMDResource>>();
		engine::res::ResourceManager::RegisterBank<engine::res::ShaderResource, engine::res::TResourceBank<engine::res::ShaderResource>>();

		engine::res::ResourceManager::Reflection<engine::res::GPUResource, engine::res::TextureLoader>();
		engine::res::ResourceManager::Reflection<engine::res::MeshResource, engine::res::MeshLoader>();
		engine::res::ResourceManager::Reflection<engine::res::PMDResource, engine::res::PMDLoader>();
		engine::res::ResourceManager::Reflection<engine::res::ShaderResource, engine::res::ShaderLoader>();


		// System registration
		engine::ecs::EntityContext::Get().AddSystem<app::ecs::CharacterSteeringSystem>();
		engine::ecs::EntityContext::Get().AddSystem<app::ecs::ActorStateMachineSystem>();
		engine::ecs::EntityContext::Get().AddSystem<engine::ecs::HierarcicalTransformSystem>();
		engine::ecs::EntityContext::Get().AddSystem<engine::ecs::RenderSystem>();
	}


	void Application::Render()
	{
		const float renderW = static_cast<float>(engine::Engine::Get().GetRenderWidth());
		const float renderH = static_cast<float>(engine::Engine::Get().GetRenderHeight());
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		// カメラ座標をライトマネージャへ通知してからライティングデータを取得する
		auto* mainCamera = engine::CameraManager::Get().GetCamera(engine::CameraType::Main);
		if (mainCamera)
			engine::graphics::LightManager::Get().SetCameraPosition(mainCamera->GetPosition());

		// --- オフスクリーンパス ---
		// displayRT に INVALID を渡すことで CopyToBackBuffer / Present をスキップする。
		if (offscreenRTHandle_.IsValid())
		{
			auto offscreenCmdList = std::make_unique<engine::rendering::RenderCommandList>();
			offscreenCmdList->Enqueue<engine::rendering::SetRenderTargetCommand>(offscreenRTHandle_);
			offscreenCmdList->Enqueue<engine::rendering::ClearRenderTargetCommand>(0u, clearColor);
			offscreenCmdList->Enqueue<engine::rendering::SetViewportCommand>(
				0.0f, 0.0f, kOffscreenRTWidth, kOffscreenRTHeight);
			engine::rendering::RenderFrame offscreenFrame;
			offscreenFrame.lighting = engine::graphics::LightManager::Get().GetLightingData();
			engine::ecs::RenderSystem::Get().BuildRenderFrame(offscreenFrame, engine::CameraType::Offscreen);
			renderer_.BuildCommandList(offscreenFrame, *offscreenCmdList);
			renderThread_.Submit(std::move(offscreenCmdList), engine::rendering::RenderTargetHandle{}, offscreenFrame.lighting);
		}

		// --- メインパス ---
		// displayRT にメイン RT ハンドルを渡して CopyToBackBuffer + Present を実行する。
		auto mainCmdList = std::make_unique<engine::rendering::RenderCommandList>();
		mainCmdList->Enqueue<engine::rendering::SetRenderTargetCommand>(
			engine::Engine::Get().GetMainRenderTargetHandle());
		mainCmdList->Enqueue<engine::rendering::ClearRenderTargetCommand>(0u, clearColor);
		mainCmdList->Enqueue<engine::rendering::SetViewportCommand>(0.0f, 0.0f, renderW, renderH);
		engine::rendering::RenderFrame mainFrame;
		mainFrame.lighting = engine::graphics::LightManager::Get().GetLightingData();
		engine::ecs::RenderSystem::Get().BuildRenderFrame(mainFrame);
		renderer_.BuildCommandList(mainFrame, *mainCmdList);
		renderThread_.Submit(std::move(mainCmdList), engine::Engine::Get().GetMainRenderTargetHandle(), mainFrame.lighting, mainFrame.shadow);
	}


	void Application::FlushRender()
	{
		if (!renderThreadReady_) return;

		// 単一RTのため WaitForCompletion() を使う。
		// レンダースレッドが CopyToBackBuffer + Present を完了してから復帰する。
		// ダブルバッファRT が揃ったら WaitForPreviousFrame() に切り替えると
		// 1フレーム分の CPU/GPU 重複が得られる。
		renderThread_.WaitForCompletion();
	}
}
