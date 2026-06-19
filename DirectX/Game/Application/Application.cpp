#include "stdafx.h"
#include "Utility.h"
#include "Application.h"
#include "Scene/Scene.h"
#include "GameInput.h"
#include "Engine.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/LightManager.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/FrameCommands.h"
#include "Rendering/Shadow/HardShadowRenderer.h"
#include "Resource/Resource.h"
#include "ECS/ECS.h"
#include "ECS/ActorComponentSystem.h"
#include "ECS/ActorSteeringComponentSystem.h"
#include "ECS/CameraSteeringComponentSystem.h"
#include "Component/TransformComponentSystem.h"
#include "Graphics/Camera.h"
#include "Component/BodyComponentSystem.h"

namespace app
{
	bool Application::OnInitialize()
	{
		// オフスクリーン RT を生成してオフスクリーンカメラのアスペクト比を設定する。
		offscreenRTHandle_ = aq::graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(
			static_cast<uint32_t>(kOffscreenRTWidth), static_cast<uint32_t>(kOffscreenRTHeight));
		EngineAssertMsg(offscreenRTHandle_.IsValid(), "Failed to create offscreen render target");
		aq::CameraManager::Get().GetCamera(aq::CameraType::Offscreen)
			->SetViewportSize(kOffscreenRTWidth, kOffscreenRTHeight);

		GameInput::Initialize();
		app::SceneManager::Create();

		// Shadow renderer
		{
			const float renderW = static_cast<float>(engine::Engine::Get().GetRenderWidth());
			const float renderH = static_cast<float>(engine::Engine::Get().GetRenderHeight());

			aq::rendering::ShadowSettings shadowSettings;
			shadowSettings.resolution  = 2048;
			shadowSettings.orthoWidth  = 20.0f;
			shadowSettings.orthoHeight = 20.0f;
			shadowSettings.nearPlane   = 0.1f;
			shadowSettings.farPlane    = 60.0f;
			shadowSettings.sceneCenter = aq::math::Vector3(0.0f, 3.0f, 0.0f);
			shadowSettings.depthBias   = 0.005f;

			auto shadowRenderer = std::make_unique<aq::rendering::HardShadowRenderer>();
			if (shadowRenderer->Create(shadowSettings, "Assets/Shader/ShadowDepth.fx"))
			{
				renderer_.SetShadowRenderer(std::move(shadowRenderer),
				                            engine::Engine::Get().GetMainRenderTargetHandle(),
				                            renderW, renderH);
			}
		}

		return true;
	}


	void Application::OnFinalize()
	{
		app::SceneManager::Release();
		GameInput::Finalize();
	}


	void Application::OnUpdate()
	{
		app::SceneManager::Get().Update();
	}


	void Application::OnRegister()
	{
		aq::res::ResourceManager::RegisterBank<aq::res::GPUResource, aq::res::TResourceBank<aq::res::GPUResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::MeshResource, aq::res::TResourceBank<aq::res::MeshResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::PMDResource, aq::res::TResourceBank<aq::res::PMDResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::ShaderResource, aq::res::TResourceBank<aq::res::ShaderResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::SkeletalMeshResource, aq::res::TResourceBank<aq::res::SkeletalMeshResource>>();
		aq::res::ResourceManager::RegisterBank<aq::res::AnimationResource, aq::res::TResourceBank<aq::res::AnimationResource>>();

		aq::res::ResourceManager::Reflection<aq::res::GPUResource, aq::res::TextureLoader>();
		aq::res::ResourceManager::Reflection<aq::res::MeshResource, aq::res::MeshLoader>();
		aq::res::ResourceManager::Reflection<aq::res::PMDResource, aq::res::PMDLoader>();
		aq::res::ResourceManager::Reflection<aq::res::ShaderResource, aq::res::ShaderLoader>();
		aq::res::ResourceManager::Reflection<aq::res::SkeletalMeshResource, aq::res::SkeletalMeshLoader>();
		aq::res::ResourceManager::Reflection<aq::res::AnimationResource, aq::res::AnimationLoader>();

		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CharacterSteeringSystem>();
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::ActorStateMachineSystem>();

		aq::ecs::EntityContext::Get().AddDependency<app::ecs::ActorStateMachineSystem, app::ecs::CharacterSteeringSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::HierarcicalTransformSystem, app::ecs::ActorStateMachineSystem>();

		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CameraSteeringSystem,
			aq::ecs::HierarcicalTransformSystem>();
		aq::ecs::EntityContext::Get().AddSystem<app::ecs::CameraEffectSystem,
			app::ecs::CameraSteeringSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, app::ecs::CameraEffectSystem>();
	}


	void Application::OnPreRender()
	{
		if (!offscreenRTHandle_.IsValid()) return;

		const float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		auto offscreenCmdList = std::make_unique<aq::rendering::RenderCommandList>();
		offscreenCmdList->Enqueue<aq::rendering::SetRenderTargetCommand>(offscreenRTHandle_);
		offscreenCmdList->Enqueue<aq::rendering::ClearRenderTargetCommand>(0u, clearColor);
		offscreenCmdList->Enqueue<aq::rendering::SetViewportCommand>(
			0.0f, 0.0f, kOffscreenRTWidth, kOffscreenRTHeight);

		aq::rendering::RenderFrame offscreenFrame;
		offscreenFrame.lighting = aq::graphics::LightManager::Get().GetLightingData();
		aq::ecs::RenderSystem::Get().BuildRenderFrame(offscreenFrame, aq::CameraType::Offscreen);
		renderer_.BuildCommandList(offscreenFrame, *offscreenCmdList,
		                          offscreenRTHandle_, kOffscreenRTWidth, kOffscreenRTHeight);
		renderThread_.Submit(std::move(offscreenCmdList), aq::rendering::RenderTargetHandle{},
		                    offscreenFrame.lighting, offscreenFrame.shadow);
	}
}
