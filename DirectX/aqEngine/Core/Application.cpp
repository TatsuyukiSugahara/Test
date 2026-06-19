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
#ifdef AQ_IMGUI
#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include "Rendering/ImGuiRenderCommand.h"
#ifdef ENGINE_GRAPHICS_D3D11
#include "Graphics/D3D11/D3D11GraphicsDeviceImpl.h"
#include <imgui/imgui_impl_dx11.h>
#endif
#endif


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

#ifdef AQ_IMGUI
#ifdef ENGINE_GRAPHICS_D3D11
		{
			auto* d3d = dynamic_cast<aq::graphics::D3D11GraphicsDeviceImpl*>(
				aq::graphics::GraphicsDevice::Get().GetImplRaw());
			EngineAssertMsg(d3d != nullptr, "AQ_IMGUI requires D3D11GraphicsDeviceImpl");
			if (d3d)
			{
				ImGui::CreateContext();
				const bool winOk  = ImGui_ImplWin32_Init(Engine::Get().GetHWND());
				const bool dx11Ok = winOk && ImGui_ImplDX11_Init(d3d->GetDevice(), d3d->GetDeviceContext());
				if (dx11Ok)
				{
					imguiReady_ = true;
				}
				else
				{
					if (winOk) ImGui_ImplWin32_Shutdown();
					ImGui::DestroyContext();
					EngineAssertMsg(false, "ImGui backend initialization failed");
				}
			}
		}
#endif
#endif

		return OnInitialize();
	}


	void Application::Finalize()
	{
		OnFinalize();

		if (renderThreadReady_)
		{
			FlushRender();             // 最後のフレームを描き切ってから停止
			renderThread_.Finalize();
			renderThreadReady_ = false;
		}

#ifdef AQ_IMGUI
		if (imguiReady_)
		{
#ifdef ENGINE_GRAPHICS_D3D11
			ImGui_ImplDX11_Shutdown();
#endif
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			imguiReady_ = false;
		}
#endif

		aq::graphics::LightManager::Finalize();
		aq::ecs::EntityContext::Finalize();
		aq::res::ResourceManager::Finalize();
		aq::hid::InputManager::Finalize();
	}


	void Application::Update()
	{
#ifdef AQ_IMGUI
		if (imguiReady_)
		{
			const auto& io = ImGui::GetIO();
			aq::hid::InputManager::Get().SuppressKeyboard(io.WantCaptureKeyboard);
			aq::hid::InputManager::Get().SuppressMouse(io.WantCaptureMouse);
		}
#endif
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
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::RenderSystem>();

		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, aq::ecs::HierarcicalTransformSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, aq::ecs::AnimationSystem>();

		OnRegister();

		aq::ecs::EntityContext::Get().FinalizeRegistration();
	}


	void Application::Render()
	{
		const float renderW = static_cast<float>(Engine::Get().GetRenderWidth());
		const float renderH = static_cast<float>(Engine::Get().GetRenderHeight());
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		auto* mainCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
		if (mainCamera)
			aq::graphics::LightManager::Get().SetCameraPosition(mainCamera->GetPosition());

#ifdef AQ_IMGUI
		ImDrawData* imguiDrawData = nullptr;
		if (imguiReady_)
		{
			ImGui_ImplWin32_NewFrame();
#ifdef ENGINE_GRAPHICS_D3D11
			ImGui_ImplDX11_NewFrame();
#endif
			ImGui::NewFrame();

#ifdef AQ_DEBUG_IMGUI
			if (ImGui::BeginMainMenuBar())
			{
				aq::ecs::EntityContext::Get().DebugRenderMenu();
				aq::ecs::EntityContext::Get().DebugRenderSystemMenus();
				OnDebugRenderMenu();
				ImGui::EndMainMenuBar();
			}
			aq::ecs::EntityContext::Get().DebugRender();
			OnDebugRender();
#endif

			ImGui::Render();
			imguiDrawData = ImGui::GetDrawData();
		}
#endif

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
#ifdef AQ_IMGUI
		if (imguiDrawData)
			mainCmdList->Enqueue<aq::rendering::ImGuiRenderCommand>(imguiDrawData);
#endif
		renderThread_.Submit(std::move(mainCmdList), Engine::Get().GetMainRenderTargetHandle(),
		                    mainFrame.lighting, mainFrame.shadow);
	}
}
