#include "aq.h"
#include "Engine.h"
#include "BodyComponentSystem.h"
#include "TransformComponentSystem.h"
#include "Graphics/Camera.h"
#include "Terrain/TerrainComponent.h"

namespace aq
{
	namespace ecs
	{
		namespace
		{
			// ボックスの頂点データ
			//const aq::math::Vector3 BOX_VERTEX_BUFFER[] = {
			//	aq::math::Vector3(0.5f, 0.5f, 0.5f),
			//	aq::math::Vector3(0.5f, 0.5f, -0.5f),
			//	aq::math::Vector3(0.5f, -0.5f, 0.5f),
			//	aq::math::Vector3(0.5f, -0.5f, -0.5f),
			//	aq::math::Vector3(-0.5f, 0.5f, 0.5f),
			//	aq::math::Vector3(-0.5f, 0.5f, -0.5f),
			//	aq::math::Vector3(-0.5f, -0.5f, 0.5f),
			//	aq::math::Vector3(-0.5f, -0.5f, -0.5f),
			//};

			const aq::graphics::VertexData BOX_VERTEX_BUFFER[] = {
				{ aq::math::Vector3(0.5f, 0.5f, 0.5f), aq::math::Vector3(0.0f, 0.0f, 0.0f), aq::math::Vector2(0.0f, 0.0f) },
				{ aq::math::Vector3(0.5f, 0.5f, -0.5f), aq::math::Vector3(0.0f, 0.0f, 0.0f), aq::math::Vector2(0.0f, 0.0f) },
				{ aq::math::Vector3(0.5f, -0.5f, 0.5f), aq::math::Vector3(0.0f, 0.0f, 0.0f), aq::math::Vector2(0.0f, 0.0f) },
				{ aq::math::Vector3(0.5f, -0.5f, -0.5f), aq::math::Vector3(0.0f, 0.0f, 0.0f), aq::math::Vector2(0.0f, 0.0f) },
				{ aq::math::Vector3(-0.5f, 0.5f, 0.5f), aq::math::Vector3(0.0f, 0.0f, 0.0f), aq::math::Vector2(0.0f, 0.0f) },
				{ aq::math::Vector3(-0.5f, 0.5f, -0.5f), aq::math::Vector3(0.0f, 0.0f, 0.0f), aq::math::Vector2(0.0f, 0.0f) },
				{ aq::math::Vector3(-0.5f, -0.5f, 0.5f), aq::math::Vector3(0.0f, 0.0f, 0.0f), aq::math::Vector2(0.0f, 0.0f) },
				{ aq::math::Vector3(-0.5f, -0.5f, -0.5f), aq::math::Vector3(0.0f, 0.0f, 0.0f), aq::math::Vector2(0.0f, 0.0f) },
			};

			constexpr uint32_t BOX_INDEX_BUFFER[] = {
				0,1,3, 1,3,2,	// 面1
				4,5,6, 5,6,7,	// 面2
				0,4,6, 0,6,2,	// 面3
				1,3,7, 1,7,5,	// 面4
				0,1,4, 1,5,4,	// 面5
				2,3,6, 3,7,6,	// 面6
			};

			aq::math::Matrix4x4 MakeZUpModelLocalMatrix(float modelScale)
			{
				aq::math::Matrix4x4 axisMatrix, scaleMatrix, localMatrix;
				axisMatrix.MakeRotationX(-1.57079632679f);
				scaleMatrix.MakeScaling(aq::math::Vector3(modelScale));
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
					staticMesh_.Initialize(BOX_VERTEX_BUFFER, ArraySize(BOX_VERTEX_BUFFER), BOX_INDEX_BUFFER, ArraySize(BOX_INDEX_BUFFER), aq::graphics::StaticMesh::ShaderType::SimpleBox);
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
			, shaderType_(aq::graphics::StaticMesh::ShaderType::ModelLit)
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
		void StaticMeshComponent::SetModelLocalMatrix(const aq::math::Matrix4x4& localMatrix)
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
					meshResouce_ = aq::res::ResourceManager::Get().Load<aq::res::MeshResource>(modelPath_.c_str());
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
						using Slot = aq::rendering::TextureSlot;
						auto& rm = aq::res::ResourceManager::Get();
						const auto& mat = meshResouce_->GetMaterial();

						// アルベド: SetModelPath で上書き指定があればそちらを優先
						const std::string& albedoPath = texturePath_.empty() ? mat.albedo : texturePath_;
						if (!albedoPath.empty())
							gpuResources_[static_cast<uint32_t>(Slot::Albedo)] = rm.Load<aq::res::GPUResource>(albedoPath.c_str());

						if (!mat.normal.empty())
							gpuResources_[static_cast<uint32_t>(Slot::Normal)] = rm.Load<aq::res::GPUResource>(mat.normal.c_str());

						if (!mat.specular.empty())
							gpuResources_[static_cast<uint32_t>(Slot::Specular)] = rm.Load<aq::res::GPUResource>(mat.specular.c_str());

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
					using Slot = aq::rendering::TextureSlot;
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
			aq::ecs::Foreach<TransformComponent, BoxStaticMeshComponent>([](const aq::ecs::Entity& entity, TransformComponent* transformComponent, BoxStaticMeshComponent* boxStaticMeshComponent)
				{
					boxStaticMeshComponent->Update();
					boxStaticMeshComponent->GetStaticMesh()->Update(transformComponent->transform.position, transformComponent->transform.rotation, transformComponent->transform.scale);
				});

			aq::ecs::Foreach<TransformComponent, StaticMeshComponent>([](const aq::ecs::Entity& entity, TransformComponent* trasnformComponent, StaticMeshComponent* staticMeshComponent)
				{
					// リソース読み込み
					staticMeshComponent->Update();
					if (staticMeshComponent->IsCompleted()) {
						staticMeshComponent->GetStaticMesh()->Update(trasnformComponent->transform.position, trasnformComponent->transform.rotation, trasnformComponent->transform.scale);
					}
				});

			aq::ecs::Foreach<TransformComponent, TerrainComponent>([](const aq::ecs::Entity&, TransformComponent* tc, TerrainComponent* terrain)
				{
					if (terrain->IsCompleted()) {
						terrain->GetChunk()->Update(tc->transform.position, tc->transform.rotation, tc->transform.scale);
					}
				});
		}


		void RenderSystem::BuildRenderFrame(aq::rendering::RenderFrame& frame)
		{
			BuildRenderFrame(frame, CameraType::Main);
		}


		void RenderSystem::BuildRenderFrame(aq::rendering::RenderFrame& frame, aq::CameraType cameraType)
		{
			const auto* camera = CameraManager::Get().GetCamera(cameraType);
			frame.camera.viewMatrix       = camera->GetViewMatrix();
			frame.camera.projectionMatrix = camera->GetProjectionMatrix();
			frame.camera.position         = camera->GetPosition();

			// 現在の方針: 同じ描画対象を cameraType で指定した別カメラから映す。
			// カメラごとに描画対象を絞りたい場合は RenderItem / Component 側に
			// RenderLayer 等を追加して検討する。
			aq::ecs::Foreach<BoxStaticMeshComponent>([&frame](const aq::ecs::Entity&, BoxStaticMeshComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					aq::rendering::RenderItem item;
					if (comp->GetStaticMesh()->FillRenderItem(item)) {
						frame.items.push_back(item);
					}
				});

			aq::ecs::Foreach<StaticMeshComponent>([&frame](const aq::ecs::Entity&, StaticMeshComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					aq::rendering::RenderItem item;
					if (comp->GetStaticMesh()->FillRenderItem(item)) {
						frame.items.push_back(item);
					}
				});

			aq::ecs::Foreach<TerrainComponent>([&frame](const aq::ecs::Entity&, TerrainComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					aq::rendering::RenderItem item;
					if (comp->GetChunk()->FillRenderItem(item)) {
						frame.items.push_back(item);
					}
				});
		}
	}
}
