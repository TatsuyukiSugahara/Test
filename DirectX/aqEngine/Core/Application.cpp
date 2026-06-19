#include "aq.h"
#include "Application.h"
#include "Engine.h"
#include "Resource/Resource.h"
#include "ECS/EntityContext.h"
#include "HID/Input.h"
#include "Graphics/Camera.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/LightManager.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/FrameCommands.h"
#include "Component/TransformComponentSystem.h"
#include "Component/BodyComponentSystem.h"
#include "Component/AnimationComponentSystem.h"


namespace engine
{
	bool Application::Initialize(aq::graphics::RenderContext& renderContext)
	{
		renderThread_.Initialize(&renderContext);
		renderThreadReady_ = true;

		aq::res::ResourceManager::Initialize();
		aq::ecs::EntityContext::Initialize();
		aq::hid::InputManager::Initialize();
		if (FAILED(aq::hid::InputManager::Get().Setup()))
		{
			EngineAssertMsg(false, "InputManager::Setup failed: DirectInput or device initialization failed");
			return false;
		}
		aq::CameraManager::Initialize();
		aq::graphics::LightManager::Initialize();

		return OnInitialize();
	}


	void Application::Finalize()
	{
		OnFinalize();

		if (renderThreadReady_)
		{
			renderThread_.Finalize();
			renderThreadReady_ = false;
		}
		aq::graphics::LightManager::Finalize();
		aq::ecs::EntityContext::Finalize();
		aq::res::ResourceManager::Finalize();
		aq::hid::InputManager::Finalize();
	}


	void Application::Update()
	{
		aq::hid::InputManager::Get().Update();
		aq::ecs::EntityContext::Get().Update();
		aq::res::ResourceManager::Get().Update();
		OnUpdate();
		aq::CameraManager::Get().UpdateAll();
		Render();
	}


	void Application::FlushRender()
	{
		if (!renderThreadReady_) return;
		renderThread_.WaitForCompletion();
	}


	void Application::Register()
	{
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::HierarcicalTransformSystem>();
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::AnimationSystem>();
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::RenderSystem, aq::ecs::AnimationSystem>();
		OnRegister();
	}


	void Application::Render()
	{
		const float renderW = static_cast<float>(Engine::Get().GetRenderWidth());
		const float renderH = static_cast<float>(Engine::Get().GetRenderHeight());
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		auto* mainCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
		if (mainCamera)
			aq::graphics::LightManager::Get().SetCameraPosition(mainCamera->GetPosition());

		OnPreRender();

		auto mainCmdList = std::make_unique<aq::rendering::RenderCommandList>();
		mainCmdList->Enqueue<aq::rendering::SetRenderTargetCommand>(Engine::Get().GetMainRenderTargetHandle());
		mainCmdList->Enqueue<aq::rendering::ClearRenderTargetCommand>(0u, clearColor);
		mainCmdList->Enqueue<aq::rendering::SetViewportCommand>(0.0f, 0.0f, renderW, renderH);
		aq::rendering::RenderFrame mainFrame;
		mainFrame.lighting = aq::graphics::LightManager::Get().GetLightingData();
		aq::ecs::RenderSystem::Get().BuildRenderFrame(mainFrame);
		renderer_.BuildCommandList(mainFrame, *mainCmdList,
		                          Engine::Get().GetMainRenderTargetHandle(), renderW, renderH);
		renderThread_.Submit(std::move(mainCmdList), Engine::Get().GetMainRenderTargetHandle(),
		                    mainFrame.lighting, mainFrame.shadow);
	}
}
