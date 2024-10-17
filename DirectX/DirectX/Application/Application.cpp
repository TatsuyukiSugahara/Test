#include "Utility.h"
#include "Application.h"
#include "Scene/Scene.h"
#include "../Engine/HID/Input.h"
#include "../Engine/Graphics/Camera.h"
#include "../Engine/Resource/Resource.h"

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


	bool Application::Initialize()
	{
		// リソース管理生成
		engine::res::ResourceManager::Initialize();
		// ECS関連生成
		engine::ecs::EntityManager::Initialize();
		engine::ecs::SystemManager::Initialize();
		// 入力管理生成
		engine::hid::InputManager::Initialize();
		engine::hid::InputManager::Get().Setup();
		// カメラ管理生成
		engine::CameraManager::Initialize();
		// シーン管理生成
		app::SceneManager::Create();

		return true;
	}


	void Application::Finalize()
	{
		// シーン管理破棄
		app::SceneManager::Release();
		// ECS関連破棄
		engine::ecs::EntityManager::Finalize();
		engine::ecs::SystemManager::Finalize();
		// リソース管理破棄
		engine::res::ResourceManager::ClearBank();
		engine::res::ResourceManager::ClearReflection();
		engine::res::ResourceManager::Finalize();
		// 入力管理破棄
		engine::hid::InputManager::Finalize();
	}


	void Application::Update(engine::graphics::RenderContext& context)
	{
		// システム更新
		engine::ecs::SystemManager::Get().Update();

		// リソース管理更新
		engine::res::ResourceManager::Get().Update();
		// 入力管理更新
		engine::hid::InputManager::Get().Update();
		// シーン管理更新
		app::SceneManager::Get().Update();


		// 描画
		Render(context);
	}


	void Application::Register()
	{
		// リソース登録
		engine::res::ResourceManager::RegisterBank<engine::res::GPUResource, engine::res::TResourceBank<engine::res::GPUResource>>();
		engine::res::ResourceManager::RegisterBank<engine::res::MeshResource, engine::res::TResourceBank<engine::res::MeshResource>>();
		engine::res::ResourceManager::RegisterBank<engine::res::PMDResource, engine::res::TResourceBank<engine::res::PMDResource>>();

		engine::res::ResourceManager::Reflection<engine::res::GPUResource, engine::res::TextureLoader>();
		engine::res::ResourceManager::Reflection<engine::res::MeshResource, engine::res::FbxLoader>();
		engine::res::ResourceManager::Reflection<engine::res::PMDResource, engine::res::PMDLoader>();


		// システム登録
		engine::ecs::SystemManager::Get().AddSystem<app::ecs::CharacterSteeringSystem>();
		engine::ecs::SystemManager::Get().AddSystem<app::ecs::ActorStateMachineSystem>();
		engine::ecs::SystemManager::Get().AddSystem<engine::ecs::HierarcicalTransformSystem>();
		engine::ecs::SystemManager::Get().AddSystem<engine::ecs::RenderSystem>();
	}


	void Application::Render(engine::graphics::RenderContext& context)
	{
		// 画面クリア
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context.OMSetRenderTargets(1, &engine::Engine::Get().GetMainRenderTarget());
		context.ClearRenderTargetView(0, clearColor);
		context.RSSetViewport(0.0f, 0.0f, static_cast<float>(engine::Engine::Get().GetRenderWidth()), static_cast<float>(engine::Engine::Get().GetRenderHeight()));

		engine::ecs::RenderSystem::Get().Render(context);
	}
}