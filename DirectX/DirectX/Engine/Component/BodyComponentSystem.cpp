#include "../EnginePreCompile.h"
#include "../Engine.h"
#include "BodyComponentSystem.h"
#include "TransformComponentSystem.h"
#include "../Graphics/Camera.h"

namespace engine
{
	namespace ecs
	{
		namespace
		{
			// ボックスの頂点データ
			//const engine::math::Vector3 BOX_VERTEX_BUFFER[] = {
			//	engine::math::Vector3(0.5f, 0.5f, 0.5f),
			//	engine::math::Vector3(0.5f, 0.5f, -0.5f),
			//	engine::math::Vector3(0.5f, -0.5f, 0.5f),
			//	engine::math::Vector3(0.5f, -0.5f, -0.5f),
			//	engine::math::Vector3(-0.5f, 0.5f, 0.5f),
			//	engine::math::Vector3(-0.5f, 0.5f, -0.5f),
			//	engine::math::Vector3(-0.5f, -0.5f, 0.5f),
			//	engine::math::Vector3(-0.5f, -0.5f, -0.5f),
			//};

			const engine::graphics::VertexData BOX_VERTEX_BUFFER[] = {
				{ engine::math::Vector3(0.5f, 0.5f, 0.5f), engine::math::Vector3(0.0f, 0.0f, 0.0f), engine::math::Vector2(0.0f, 0.0f) },
				{ engine::math::Vector3(0.5f, 0.5f, -0.5f), engine::math::Vector3(0.0f, 0.0f, 0.0f), engine::math::Vector2(0.0f, 0.0f) },
				{ engine::math::Vector3(0.5f, -0.5f, 0.5f), engine::math::Vector3(0.0f, 0.0f, 0.0f), engine::math::Vector2(0.0f, 0.0f) },
				{ engine::math::Vector3(0.5f, -0.5f, -0.5f), engine::math::Vector3(0.0f, 0.0f, 0.0f), engine::math::Vector2(0.0f, 0.0f) },
				{ engine::math::Vector3(-0.5f, 0.5f, 0.5f), engine::math::Vector3(0.0f, 0.0f, 0.0f), engine::math::Vector2(0.0f, 0.0f) },
				{ engine::math::Vector3(-0.5f, 0.5f, -0.5f), engine::math::Vector3(0.0f, 0.0f, 0.0f), engine::math::Vector2(0.0f, 0.0f) },
				{ engine::math::Vector3(-0.5f, -0.5f, 0.5f), engine::math::Vector3(0.0f, 0.0f, 0.0f), engine::math::Vector2(0.0f, 0.0f) },
				{ engine::math::Vector3(-0.5f, -0.5f, -0.5f), engine::math::Vector3(0.0f, 0.0f, 0.0f), engine::math::Vector2(0.0f, 0.0f) },
			};

			constexpr uint32_t BOX_INDEX_BUFFER[] = {
				0,1,3, 1,3,2,	// 面1
				4,5,6, 5,6,7,	// 面2
				0,4,6, 0,6,2,	// 面3
				1,3,7, 1,7,5,	// 面4
				0,1,4, 1,5,4,	// 面5
				2,3,6, 3,7,6,	// 面6
			};
		}

		void BoxStaticMeshComponent::Initialize()
		{
			componentState_ = ComponentState::Loading;
		}


		void BoxStaticMeshComponent::Update()
		{
			switch (componentState_)
			{
				case ComponentState::Loading:
				{
					componentState_ = ComponentState::Completed;
					/** FALL THROUGH */
				}
				case ComponentState::Completed:
				{
					staticMesh_.Initialize(BOX_VERTEX_BUFFER, ArraySize(BOX_VERTEX_BUFFER), BOX_INDEX_BUFFER, ArraySize(BOX_INDEX_BUFFER), engine::graphics::StaticMesh::ShaderType::SimpleBox);
				}

			}
		}


		/*******************************************/




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
					meshResouce_ = engine::res::ResourceManager::Get().Load<engine::res::MeshResource>("Assets/Character/SmallFish.fbx");
					gpuResource_ = engine::res::ResourceManager::Get().Load<engine::res::GPUResource>("Assets/Character/SmallFish.png");
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
					// 読み込み完了
				}
			}
		}


		/*******************************************/




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
			engine::ecs::Foreach<TransformComponent, BoxStaticMeshComponent>([](TransformComponent* transformComponent, BoxStaticMeshComponent* boxStaticMeshComponent)
				{
					boxStaticMeshComponent->Update();
					boxStaticMeshComponent->GetStaticMesh()->Update(transformComponent->transform.position, transformComponent->transform.rotation, transformComponent->transform.scale);
				});

			engine::ecs::Foreach<TransformComponent, StaticMeshComponent>([](TransformComponent* trasnformComponent, StaticMeshComponent* staticMeshComponent)
				{
					// リソース読み込み
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
			engine::ecs::Foreach<BoxStaticMeshComponent>([&context, &camera](BoxStaticMeshComponent* staticMeshComponent)
				{
					if (!staticMeshComponent->IsCompleted()) return;
					staticMeshComponent->GetStaticMesh()->Render(context, camera->GetViewMatrix(), camera->GetProjectionMatrix());
				});
			engine::ecs::Foreach<StaticMeshComponent>([&context, &camera](StaticMeshComponent* staticMeshComponent)
				{
					if (!staticMeshComponent->IsCompleted()) return;
					staticMeshComponent->GetStaticMesh()->Render(context, camera->GetViewMatrix(), camera->GetProjectionMatrix());
				});
		}
	}
}