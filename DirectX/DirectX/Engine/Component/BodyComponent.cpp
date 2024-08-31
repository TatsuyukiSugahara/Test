#include "../EnginePreCompile.h"
#include "../Engine.h"
#include "BodyComponent.h"
#include "../GameObject/GameObject.h"
#include "../Graphics/Camera.h"

namespace engine
{
	namespace component
	{
		StaticMeshComponent::StaticMeshComponent(engine::IGameObject* gameObject)
			: IComponent(gameObject)
			, componentState_(ComponentState::LoadRequest)
		{

		}
		
		
		StaticMeshComponent::~StaticMeshComponent()
		{

		}

		void StaticMeshComponent::Start()
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
					// @todo for test
					engine::math::Quaternion q;
					q.SetEuler(engine::math::Vector3(90.0f, 90.0f, 90.0f));
					staticMesh_.Update(engine::math::Vector3(0, 0, 0), q, engine::math::Vector3(1.0f));
				}
			}
		}


		void StaticMeshComponent::Render(graphics::RenderContext& context)
		{
			if (componentState_ != ComponentState::Completed) {
				return;
			}

			engine::Camera* camera = engine::CameraManager::Get().GetCamera(CameraType::Main);
			staticMesh_.Render(context, camera->GetViewMatrix(), camera->GetProjectionMatrix());
		}
	}
}