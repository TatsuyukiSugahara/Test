#include "Utility.h"
#include "Application.h"
#include "Scene/Scene.h"
#include "../Engine/GameObject/GameObject.h"
#include "../Engine/ECS/ECS.h"

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
		// ���\�[�X�Ǘ�����
		engine::res::ResourceManager::Initialize();
		// ECS�֘A����
		engine::ecs::EntityManager::Initialize();
		engine::ecs::SystemManager::Initialize();
		// ���͊Ǘ�����
		engine::hid::InputManager::Initialize();
		engine::hid::InputManager::Get().Setup();
		// �J�����Ǘ�����
		engine::CameraManager::Initialize();

		// @todo for test
		engine::Camera* camera = engine::CameraManager::Get().GetCamera(engine::CameraType::Main);
		camera->SetPosition(engine::math::Vector3(0.0f, 0.0f, 1.0f));
		camera->SetTarget(engine::math::Vector3(0.0f, 0.0f, 0.0f));
		camera->SetNear(0.01f);
		camera->Update();

		//app::scene::SceneManager::Create();
		//engine::GameObjectManager::Create();

		return true;
	}


	void Application::Finalize()
	{
		//app::scene::SceneManager::Release();
		//engine::GameObjectManager::Release();

		// ECS�֘A�j��
		engine::ecs::EntityManager::Finalize();
		engine::ecs::SystemManager::Finalize();
		// ���\�[�X�Ǘ��j��
		engine::res::ResourceManager::Finalize();
		// ���͊Ǘ��j��
		engine::hid::InputManager::Finalize();
	}


	void Application::Update(engine::graphics::RenderContext& context)
	{
		if (hoge_ == nullptr) {
			hoge_ = new Hoge();
		}
		hoge_->Update();

		//app::scene::SceneManager::Get().Update();
		//engine::GameObjectManager::Get().Execute(context);
		hoge_->Render(context);

		// �V�X�e���X�V
		engine::ecs::SystemManager::Get().Update();

		// ���\�[�X�Ǘ��X�V
		engine::res::ResourceManager::Get().Update();
		// ���͊Ǘ��X�V
		engine::hid::InputManager::Get().Update();


		// �`��
		Render(context);
	}


	void Application::Register()
	{
		engine::res::ResourceManager::Get().RegisterBank<engine::res::TResourceBank<engine::res::RefGPUResource>>(engine::res::TextureLoader::ResourceBankID());
		engine::res::ResourceManager::Get().RegisterBank<engine::res::TResourceBank<engine::res::RefMeshResource>>(engine::res::FbxLoader::ResourceBankID());
		engine::res::ResourceManager::Get().RegisterBank<engine::res::TResourceBank<engine::res::RefPMDResource>>(engine::res::PMDLoader::ResourceBankID());
	}


	void Application::Render(engine::graphics::RenderContext& context)
	{

	}
}