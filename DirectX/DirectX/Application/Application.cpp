#include "Utility.h"
#include "Application.h"
#include "Scene/Scene.h"
#include "../Engine/Engine.h"
#include "../Engine/HID/Input.h"
#include "../Engine/Graphics/Camera.h"
#include "../Engine/Resource/Resource.h"
#include "../Engine/Rendering/RenderFrame.h"
#include "../Engine/Rendering/RenderCommandList.h"
#include "../Engine/Rendering/FrameCommands.h"

#include "../Engine/ECS/ECS.h"
#include "../Engine/Component/TransformComponentSystem.h"
#include "../Engine/Component/BodyComponentSystem.h"
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
		engine::ecs::EntityManager::Initialize();
		engine::ecs::SystemManager::Initialize();
		// Input
		engine::hid::InputManager::Initialize();
		engine::hid::InputManager::Get().Setup();
		// Camera
		engine::CameraManager::Initialize();
		// Scene
		app::SceneManager::Create();

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
		// ECS
		engine::ecs::EntityManager::Finalize();
		engine::ecs::SystemManager::Finalize();
		// Resources
		engine::res::ResourceManager::Finalize();
		// Input
		engine::hid::InputManager::Finalize();
	}


	void Application::Update()
	{
		// System update
		engine::ecs::SystemManager::Get().Update();

		// Resource manager update
		engine::res::ResourceManager::Get().Update();
		// Input update
		engine::hid::InputManager::Get().Update();
		// Scene update
		app::SceneManager::Get().Update();

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
		engine::ecs::SystemManager::Get().AddSystem<app::ecs::CharacterSteeringSystem>();
		engine::ecs::SystemManager::Get().AddSystem<app::ecs::ActorStateMachineSystem>();
		engine::ecs::SystemManager::Get().AddSystem<engine::ecs::HierarcicalTransformSystem>();
		engine::ecs::SystemManager::Get().AddSystem<engine::ecs::RenderSystem>();
	}


	void Application::Render()
	{
		auto cmdList = std::make_unique<engine::rendering::RenderCommandList>();

		// --- フレームセットアップ ---
		// DX12 ではリソースバリア・デスクリプタ・ヒープバインド・クリアコマンドになる。
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		cmdList->Enqueue<engine::rendering::SetRenderTargetCommand>(
			engine::Engine::Get().GetMainRenderTargetHandle());
		cmdList->Enqueue<engine::rendering::ClearRenderTargetCommand>(0u, clearColor);
		cmdList->Enqueue<engine::rendering::SetViewportCommand>(
			0.0f, 0.0f,
			static_cast<float>(engine::Engine::Get().GetRenderWidth()),
			static_cast<float>(engine::Engine::Get().GetRenderHeight()));

		// --- シーンジオメトリ ---
		// ECS システムは SystemManager::Update() 内で実行済み。
		// BuildRenderFrame はすべての ECS 並列処理が終わった後のスナップショットパス。
		engine::rendering::RenderFrame frame;
		engine::ecs::RenderSystem::Get().BuildRenderFrame(frame);
		renderer_.BuildCommandList(frame, *cmdList);

		// コマンドリストを displayRT ハンドルとともにレンダースレッドへ渡す。
		// レンダースレッドが同じスレッドで描画・CopyToBackBuffer・Present を行うため、
		// D3D11 immediate context の単一スレッド化が保たれる。
		renderThread_.Submit(std::move(cmdList), engine::Engine::Get().GetMainRenderTargetHandle());
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
