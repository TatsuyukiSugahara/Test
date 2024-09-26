#include "../EnginePreCompile.h"
#include "../Engine.h"
#include "BodyComponent.h"

namespace engine
{
	namespace ecs
	{
		StaticMeshComponent::StaticMeshComponent()
			:componentState_(ComponentState::LoadRequest)
		{

		}
		
		
		StaticMeshComponent::~StaticMeshComponent()
		{

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

					if (!staticMesh_.Initialize(meshResouce_, gpuResource_, engine::graphics::StaticMesh::ShaderType::NormalModel)) {
						break;
					}
					componentState_ = ComponentState::Completed;
					/** FALL THROUGH */
				}
				case ComponentState::Completed:
				{
					// ì«Ç›çûÇ›äÆóπ
				}
			}
		}
	}
}