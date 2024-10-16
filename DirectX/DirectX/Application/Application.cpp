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
		// �V�[���Ǘ�����
		app::SceneManager::Create();

		return true;
	}


	void Application::Finalize()
	{
		// �V�[���Ǘ��j��
		app::SceneManager::Release();
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
		// �V�X�e���X�V
		engine::ecs::SystemManager::Get().Update();

		// ���\�[�X�Ǘ��X�V
		engine::res::ResourceManager::Get().Update();
		// ���͊Ǘ��X�V
		engine::hid::InputManager::Get().Update();
		// �V�[���Ǘ��X�V
		app::SceneManager::Get().Update();


		// �`��
		Render(context);
	}


	void Application::Register()
	{
		// ���\�[�X�o�^
		engine::res::ResourceManager::Get().RegisterBank<engine::res::TResourceBank<engine::res::RefGPUResource>>(engine::res::TextureLoader::ResourceBankID());
		engine::res::ResourceManager::Get().RegisterBank<engine::res::TResourceBank<engine::res::RefMeshResource>>(engine::res::FbxLoader::ResourceBankID());
		engine::res::ResourceManager::Get().RegisterBank<engine::res::TResourceBank<engine::res::RefPMDResource>>(engine::res::PMDLoader::ResourceBankID());


		// �V�X�e���o�^
		engine::ecs::SystemManager::Get().AddSystem<app::ecs::CharacterSteeringSystem>();
		engine::ecs::SystemManager::Get().AddSystem<app::ecs::ActorStateMachineSystem>();
		engine::ecs::SystemManager::Get().AddSystem<engine::ecs::HierarcicalTransformSystem>();
		engine::ecs::SystemManager::Get().AddSystem<engine::ecs::RenderSystem>();
	}


	void Application::Render(engine::graphics::RenderContext& context)
	{
		// ��ʃN���A
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context.OMSetRenderTargets(1, &engine::Engine::Get().GetMainRenderTarget());
		context.ClearRenderTargetView(0, clearColor);
		context.RSSetViewport(0.0f, 0.0f, static_cast<float>(engine::Engine::Get().GetRenderWidth()), static_cast<float>(engine::Engine::Get().GetRenderHeight()));

		engine::ecs::RenderSystem::Get().Render(context);
	}
}