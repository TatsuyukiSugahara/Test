#include "Utility.h"
#include "Application.h"
#include "Scene/Scene.h"
#include "../Engine/GameObject/GameObject.h"
#include "../Engine/ECS/ECS.h"

// @todo for test
#include "../Engine/Component/TransformComponent.h"
#include "../Engine/Component/BodyComponentSystem.h"
#include "../Engine/Resource/Resource.h"
#include "../Engine/Graphics/Camera.h"
#include "../Engine/HID/Input.h"

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

		// @todo for test
		engine::Camera* camera = engine::CameraManager::Get().GetCamera(engine::CameraType::Main);
		camera->SetPosition(engine::math::Vector3(0.0f, 0.0f, 1.0f));
		camera->SetTarget(engine::math::Vector3(0.0f, 0.0f, 0.0f));
		camera->SetNear(0.01f);
		camera->Update();

		// @todo for test
		engine::ecs::EntityManager::Get().CreateEntity<engine::ecs::TransformComponent, engine::ecs::StaticMeshComponent>();

		//app::scene::SceneManager::Create();
		//engine::GameObjectManager::Create();

		return true;
	}


	void Application::Finalize()
	{
		//app::scene::SceneManager::Release();
		//engine::GameObjectManager::Release();

		// ECS関連破棄
		engine::ecs::EntityManager::Finalize();
		engine::ecs::SystemManager::Finalize();
		// リソース管理破棄
		engine::res::ResourceManager::Finalize();
		// 入力管理破棄
		engine::hid::InputManager::Finalize();
	}


	void Application::Update(engine::graphics::RenderContext& context)
	{
		//app::scene::SceneManager::Get().Update();
		//engine::GameObjectManager::Get().Execute(context);

		// システム更新
		engine::ecs::SystemManager::Get().Update();

		// リソース管理更新
		engine::res::ResourceManager::Get().Update();
		// 入力管理更新
		engine::hid::InputManager::Get().Update();


		// 描画
		Render(context);
	}


	void Application::Register()
	{
		// リソース登録
		engine::res::ResourceManager::Get().RegisterBank<engine::res::TResourceBank<engine::res::RefGPUResource>>(engine::res::TextureLoader::ResourceBankID());
		engine::res::ResourceManager::Get().RegisterBank<engine::res::TResourceBank<engine::res::RefMeshResource>>(engine::res::FbxLoader::ResourceBankID());
		engine::res::ResourceManager::Get().RegisterBank<engine::res::TResourceBank<engine::res::RefPMDResource>>(engine::res::PMDLoader::ResourceBankID());


		// システム登録
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