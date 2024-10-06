#include "../EnginePreCompile.h"
#include "../Engine.h"
#include "BodyComponentSystem.h"
#include "TransformComponent.h"
#include "../Graphics/Camera.h"

namespace engine
{
	namespace ecs
	{
		void StaticMeshComponent::Initialize()
		{
			componentState_ = ComponentState::LoadRequest;
		}


		void StaticMeshComponent::Update()
		{
			switch (componentState_)
			{
				case ComponentState::Invalid:
				{
					break;
				}
				case ComponentState::LoadRequest:
				{
					// @todo for test
					meshResouce_ = engine::res::ResourceManager::Get().Load<engine::res::RefMeshResource, engine::res::FbxLoader>("Assets/Character/SmallFish.fbx");
					gpuResource_ = engine::res::ResourceManager::Get().Load<engine::res::RefGPUResource, engine::res::TextureLoader>("Assets/Character/SmallFish.png");
					componentState_ = ComponentState::Loading;
					/** FALL THROUGH */
				}
				case ComponentState::Loading:
				{
					if (!meshResouce_->IsCompleted()) {
						break;
					}
					if (!gpuResource_->IsCompleted()) {
						break;
					}

					staticMesh_.Initialize(meshResouce_, gpuResource_, engine::graphics::StaticMesh::ShaderType::NormalModel);
					componentState_ = ComponentState::Completed;
					/** FALL THROUGH */
				}
				case ComponentState::Completed:
				{
					// ì«Ç›çûÇ›äÆóπ
				}
			}
		}




		RenderSystem* RenderSystem::instance_ = nullptr;


		RenderSystem::RenderSystem()
		{
			instance_ = this;
		}


		RenderSystem::~RenderSystem()
		{
			instance_ = nullptr;
		}


		void RenderSystem::Update()
		{
			engine::ecs::Foreach<TransformComponent, StaticMeshComponent>([](TransformComponent* trasnformComponent, StaticMeshComponent* staticMeshComponent)
				{
					// ÉäÉ\Å[ÉXì«Ç›çûÇ›
					staticMeshComponent->Update();
					if (staticMeshComponent->IsCompleted()) {
						//staticMeshComponent->GetStaticMesh()->Update(trasnformComponent->transform.position, trasnformComponent->transform.rotation, trasnformComponent->transform.scale);
						// @todo for test
						staticMeshComponent->GetStaticMesh()->Update(trasnformComponent->transform.position, trasnformComponent->transform.rotation, 1.0f);
					}
				});
		}


		void RenderSystem::Render(engine::graphics::RenderContext& context)
		{
			const auto* camera = CameraManager::Get().GetCamera(CameraType::Main);
			engine::ecs::Foreach<StaticMeshComponent>([&context, &camera](StaticMeshComponent* staticMeshComponent)
				{
					if (!staticMeshComponent->IsCompleted()) return;
					staticMeshComponent->GetStaticMesh()->Render(context, camera->GetViewMatrix(), camera->GetProjectionMatrix());
				});
		}
	}
}