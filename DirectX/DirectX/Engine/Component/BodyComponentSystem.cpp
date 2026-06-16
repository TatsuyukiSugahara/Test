#include "EnginePreCompile.h"
#include "Engine.h"
#include "BodyComponentSystem.h"
#include "TransformComponentSystem.h"
#include "Graphics/Camera.h"

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

			engine::math::Matrix4x4 MakeZUpModelLocalMatrix(float modelScale)
			{
				engine::math::Matrix4x4 axisMatrix, scaleMatrix, localMatrix;
				axisMatrix.MakeRotationX(-1.57079632679f);
				scaleMatrix.MakeScaling(engine::math::Vector3(modelScale));
				localMatrix.Mull(axisMatrix, scaleMatrix);
				return localMatrix;
			}
		}


		BoxStaticMeshComponent::BoxStaticMeshComponent()
			: componentState_(ComponentState::Loading)
		{
		}


		BoxStaticMeshComponent::~BoxStaticMeshComponent()
		{
		}
		void BoxStaticMeshComponent::Update()
		{
			switch (componentState_)
			{
				case ComponentState::Loading:
				{
					staticMesh_.Initialize(BOX_VERTEX_BUFFER, ArraySize(BOX_VERTEX_BUFFER), BOX_INDEX_BUFFER, ArraySize(BOX_INDEX_BUFFER), engine::graphics::StaticMesh::ShaderType::SimpleBox);
					componentState_ = ComponentState::Completed;
					break;
				}
				case ComponentState::Completed:
				{
					break;
				}
			}
		}

		/*******************************************/
		StaticMeshComponent::StaticMeshComponent()
			: componentState_(ComponentState::Invalid)
			, modelPath_()
			, texturePath_()
			, modelLocalMatrix_(MakeZUpModelLocalMatrix(0.05f))
			, textureLoadRequested_(false)
			, shaderType_(engine::graphics::StaticMesh::ShaderType::ModelLit)
		{
		}


		StaticMeshComponent::~StaticMeshComponent()
		{
		}

		void StaticMeshComponent::SetModelPath(const char* modelPath, const char* texturePath)
		{
			modelPath_ = modelPath ? modelPath : "";
			texturePath_ = texturePath ? texturePath : "";
			meshResouce_.reset();
			for (auto& r : gpuResources_) r.reset();
			textureLoadRequested_ = false;
			componentState_ = modelPath_.empty() ? ComponentState::Invalid : ComponentState::LoadRequest;
		}
		void StaticMeshComponent::SetModelLocalMatrix(const engine::math::Matrix4x4& localMatrix)
		{
			modelLocalMatrix_ = localMatrix;
			if (componentState_ == ComponentState::Completed) {
				staticMesh_.SetLocalMatrix(modelLocalMatrix_);
			}
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
					meshResouce_ = engine::res::ResourceManager::Get().Load<engine::res::MeshResource>(modelPath_.c_str());
					for (auto& r : gpuResources_) r.reset();
					textureLoadRequested_ = false;
					componentState_ = ComponentState::Loading;
					[[fallthrough]];
				}
				case ComponentState::Loading:
				{
					if (!meshResouce_ || meshResouce_->IsFailed()) {
						componentState_ = ComponentState::Invalid;
						break;
					}
					if (!meshResouce_->IsCompleted()) {
						break;
					}

					// メッシュ完了後にテクスチャリクエストを一括発行
					if (!textureLoadRequested_) {
						using Slot = engine::rendering::TextureSlot;
						auto& rm = engine::res::ResourceManager::Get();
						const auto& mat = meshResouce_->GetMaterial();

						// アルベド: SetModelPath で上書き指定があればそちらを優先
						const std::string& albedoPath = texturePath_.empty() ? mat.albedo : texturePath_;
						if (!albedoPath.empty())
							gpuResources_[static_cast<uint32_t>(Slot::Albedo)] = rm.Load<engine::res::GPUResource>(albedoPath.c_str());

						if (!mat.normal.empty())
							gpuResources_[static_cast<uint32_t>(Slot::Normal)] = rm.Load<engine::res::GPUResource>(mat.normal.c_str());

						if (!mat.specular.empty())
							gpuResources_[static_cast<uint32_t>(Slot::Specular)] = rm.Load<engine::res::GPUResource>(mat.specular.c_str());

						textureLoadRequested_ = true;
					}

					// 全テクスチャの完了を待つ
					bool anyPending = false;
					for (auto& r : gpuResources_) {
						if (!r) continue;
						if (r->IsFailed()) { r.reset(); continue; }
						if (!r->IsCompleted()) { anyPending = true; }
					}
					if (anyPending) break;

					// 初期化: アルベドで Initialize → 追加スロットを SetTexture
					using Slot = engine::rendering::TextureSlot;
					staticMesh_.SetLocalMatrix(modelLocalMatrix_);
					staticMesh_.Initialize(meshResouce_, gpuResources_[static_cast<uint32_t>(Slot::Albedo)], shaderType_);

					for (uint32_t i = static_cast<uint32_t>(Slot::Normal);
					     i < static_cast<uint32_t>(Slot::Count); ++i)
					{
						if (gpuResources_[i])
							staticMesh_.SetTexture(static_cast<Slot>(i), gpuResources_[i]);
					}

					componentState_ = ComponentState::Completed;
					break;
				}
				case ComponentState::Completed:
				{
					break;
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
			engine::ecs::Foreach<TransformComponent, BoxStaticMeshComponent>([](const engine::ecs::Entity& entity, TransformComponent* transformComponent, BoxStaticMeshComponent* boxStaticMeshComponent)
				{
					boxStaticMeshComponent->Update();
					boxStaticMeshComponent->GetStaticMesh()->Update(transformComponent->transform.position, transformComponent->transform.rotation, transformComponent->transform.scale);
				});

			engine::ecs::Foreach<TransformComponent, StaticMeshComponent>([](const engine::ecs::Entity& entity, TransformComponent* trasnformComponent, StaticMeshComponent* staticMeshComponent)
				{
					// リソース読み込み
					staticMeshComponent->Update();
					if (staticMeshComponent->IsCompleted()) {
						staticMeshComponent->GetStaticMesh()->Update(trasnformComponent->transform.position, trasnformComponent->transform.rotation, trasnformComponent->transform.scale);
					}
				});
		}


		void RenderSystem::BuildRenderFrame(engine::rendering::RenderFrame& frame)
		{
			BuildRenderFrame(frame, CameraType::Main);
		}


		void RenderSystem::BuildRenderFrame(engine::rendering::RenderFrame& frame, engine::CameraType cameraType)
		{
			const auto* camera = CameraManager::Get().GetCamera(cameraType);
			frame.camera.viewMatrix       = camera->GetViewMatrix();
			frame.camera.projectionMatrix = camera->GetProjectionMatrix();
			frame.camera.position         = camera->GetPosition();

			// 現在の方針: 同じ描画対象を cameraType で指定した別カメラから映す。
			// カメラごとに描画対象を絞りたい場合は RenderItem / Component 側に
			// RenderLayer 等を追加して検討する。
			engine::ecs::Foreach<BoxStaticMeshComponent>([&frame](const engine::ecs::Entity&, BoxStaticMeshComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					engine::rendering::RenderItem item;
					if (comp->GetStaticMesh()->FillRenderItem(item)) {
						frame.items.push_back(item);
					}
				});

			engine::ecs::Foreach<StaticMeshComponent>([&frame](const engine::ecs::Entity&, StaticMeshComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					engine::rendering::RenderItem item;
					if (comp->GetStaticMesh()->FillRenderItem(item)) {
						frame.items.push_back(item);
					}
				});
		}
	}
}
