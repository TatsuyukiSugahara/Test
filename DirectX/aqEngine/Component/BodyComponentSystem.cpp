#include "aq.h"
#include "BodyComponentSystem.h"
#include "TransformComponentSystem.h"
#include "HierarchicalTransformComponent.h"
#include "Graphics/Camera.h"
#include "Component/TerrainComponent.h"
#include "Component/DecalComponent.h"
#include "Rendering/Occlusion/IOcclusionTester.h"
#include "Resource/Resource.h"
#include "Engine.h"

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

			// 頂点 0..7 は (x,y,z) の符号ビット。全面を外向き（CCW/右手系＝このエンジンの表面）に統一する。
			// 旧データは 6 面中 2 面（+X / -Y）の巻き順が逆でカリングされ、箱が欠けて見えていた。
			constexpr uint32_t BOX_INDEX_BUFFER[] = {
				2,3,1, 2,1,0,	// +X 面 (x=+0.5)
				4,5,7, 4,7,6,	// -X 面 (x=-0.5)
				0,1,5, 0,5,4,	// +Y 面 (y=+0.5)
				2,6,7, 2,7,3,	// -Y 面 (y=-0.5)
				0,4,6, 0,6,2,	// +Z 面 (z=+0.5)
				1,3,7, 1,7,5,	// -Z 面 (z=-0.5)
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

						// slot 2: PBR は metallicRoughness、forward は specular
						const bool isPBR = shaderType_ == aq::graphics::StaticMesh::ShaderType::PBRLit ||
						                   shaderType_ == aq::graphics::StaticMesh::ShaderType::TerrainPBRLit;
						if (isPBR) {
							if (!metallicRoughnessPath_.empty())
								gpuResources_[static_cast<uint32_t>(Slot::MetallicRoughness)] =
									rm.Load<aq::res::GPUResource>(metallicRoughnessPath_.c_str());
						} else {
							if (!mat.specular.empty())
								gpuResources_[static_cast<uint32_t>(Slot::Specular)] =
									rm.Load<aq::res::GPUResource>(mat.specular.c_str());
						}

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




		/*******************************************/


		namespace
		{
			aq::math::Matrix4x4 MakeZUpSkeletalLocalMatrix(float modelScale)
			{
				aq::math::Matrix4x4 axisMatrix, scaleMatrix, localMatrix;
				axisMatrix.MakeRotationX(-1.57079632679f);
				scaleMatrix.MakeScaling(aq::math::Vector3(modelScale));
				localMatrix.Mull(axisMatrix, scaleMatrix);
				return localMatrix;
			}
		}


		SkeletalMeshComponent::SkeletalMeshComponent()
			: componentState_(ComponentState::Invalid)
			, modelPath_()
			, texturePath_()
			, modelLocalMatrix_(MakeZUpSkeletalLocalMatrix(0.05f))
			, textureLoadRequested_(false)
		{
		}


		SkeletalMeshComponent::~SkeletalMeshComponent()
		{
		}


		void SkeletalMeshComponent::SetModelPath(const char* modelPath, const char* texturePath)
		{
			modelPath_   = modelPath  ? modelPath  : "";
			texturePath_ = texturePath ? texturePath : "";
			skeletalMeshResource_.reset();
			for (auto& r : gpuResources_) r.reset();
			textureLoadRequested_ = false;
			componentState_ = modelPath_.empty() ? ComponentState::Invalid : ComponentState::LoadRequest;
		}


		void SkeletalMeshComponent::SetModelLocalMatrix(const aq::math::Matrix4x4& localMatrix)
		{
			modelLocalMatrix_ = localMatrix;
			if (componentState_ == ComponentState::Completed) {
				skeletalMesh_.SetLocalMatrix(modelLocalMatrix_);
			}
		}


		void SkeletalMeshComponent::Update()
		{
			switch (componentState_)
			{
				case ComponentState::Invalid:
					break;

				case ComponentState::LoadRequest:
				{
					skeletalMeshResource_ = aq::res::ResourceManager::Get().Load<aq::res::SkeletalMeshResource>(modelPath_.c_str());
					for (auto& r : gpuResources_) r.reset();
					textureLoadRequested_ = false;
					componentState_ = ComponentState::Loading;
					/* Fallthrough */
				}
				case ComponentState::Loading:
				{
					if (!skeletalMeshResource_ || skeletalMeshResource_->IsFailed()) {
						componentState_ = ComponentState::Invalid;
						break;
					}
					if (!skeletalMeshResource_->IsCompleted()) break;

					if (!textureLoadRequested_) {
						using Slot = aq::rendering::TextureSlot;
						auto& rm = aq::res::ResourceManager::Get();
						const auto& mat = skeletalMeshResource_->GetMaterial();

						const std::string& albedoPath = texturePath_.empty() ? mat.albedo : texturePath_;
						if (!albedoPath.empty())
							gpuResources_[static_cast<uint32_t>(Slot::Albedo)] = rm.Load<aq::res::GPUResource>(albedoPath.c_str());
						if (!mat.normal.empty())
							gpuResources_[static_cast<uint32_t>(Slot::Normal)] = rm.Load<aq::res::GPUResource>(mat.normal.c_str());

						// slot 2: PBR は metallicRoughness、forward は specular
						if (shaderType_ == aq::graphics::SkeletalMesh::ShaderType::SkeletalPBRLit) {
							if (!metallicRoughnessPath_.empty())
								gpuResources_[static_cast<uint32_t>(Slot::MetallicRoughness)] =
									rm.Load<aq::res::GPUResource>(metallicRoughnessPath_.c_str());
						} else {
							if (!mat.specular.empty())
								gpuResources_[static_cast<uint32_t>(Slot::Specular)] =
									rm.Load<aq::res::GPUResource>(mat.specular.c_str());
						}

						textureLoadRequested_ = true;
					}

					bool anyPending = false;
					for (auto& r : gpuResources_) {
						if (!r) continue;
						if (r->IsFailed()) { r.reset(); continue; }
						if (!r->IsCompleted()) anyPending = true;
					}
					if (anyPending) break;

					using Slot = aq::rendering::TextureSlot;
					skeletalMesh_.SetLocalMatrix(modelLocalMatrix_);
					skeletalMesh_.Initialize(
						skeletalMeshResource_,
						gpuResources_[static_cast<uint32_t>(Slot::Albedo)],
						shaderType_);

					for (uint32_t i = static_cast<uint32_t>(Slot::Normal);
					     i < static_cast<uint32_t>(Slot::Count); ++i)
					{
						if (gpuResources_[i])
							skeletalMesh_.SetTexture(static_cast<Slot>(i), gpuResources_[i]);
					}

					componentState_ = ComponentState::Completed;
					break;
				}
				case ComponentState::Completed:
					break;
			}
		}


		/*******************************************/


		RenderSystem* RenderSystem::instance_ = nullptr;
		bool          RenderSystem::frustumCullingEnabled_ = true;
		uint32_t      RenderSystem::cullingTotalCount_     = 0;
		uint32_t      RenderSystem::cullingVisibleCount_   = 0;

		const aq::rendering::IOcclusionTester* RenderSystem::occlusionTester_ = nullptr;
		bool          RenderSystem::occlusionCullingEnabled_ = true;
		uint32_t      RenderSystem::occludedCount_           = 0;

		bool          RenderSystem::clusterStatsEnabled_  = false;  // 既定OFF: CPU方式は小メッシュで割に合わない
		uint32_t      RenderSystem::clusterTotal_         = 0;
		uint32_t      RenderSystem::clusterVisible_       = 0;
		uint32_t      RenderSystem::clusterTriTotal_      = 0;
		uint32_t      RenderSystem::clusterTriVisible_    = 0;
		uint32_t      RenderSystem::clusterConeUsable_    = 0;
		uint32_t      RenderSystem::clusterBackface_      = 0;


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
			aq::ecs::Foreach<TransformComponent, HierarchicalTransformComponent, BoxStaticMeshComponent>(
				[](const aq::ecs::Entity&, TransformComponent*, HierarchicalTransformComponent* hierarchicalTransformComponent, BoxStaticMeshComponent* boxStaticMeshComponent)
				{
					boxStaticMeshComponent->Update();
					boxStaticMeshComponent->GetStaticMesh()->Update(
						hierarchicalTransformComponent->transform.position,
						hierarchicalTransformComponent->transform.rotation,
						hierarchicalTransformComponent->transform.scale);
				});

			aq::ecs::Foreach<TransformComponent, HierarchicalTransformComponent, StaticMeshComponent>(
				[](const aq::ecs::Entity&, TransformComponent*, HierarchicalTransformComponent* hierarchicalTransformComponent, StaticMeshComponent* staticMeshComponent)
				{
					staticMeshComponent->Update();
					if (staticMeshComponent->IsCompleted()) {
						staticMeshComponent->GetStaticMesh()->Update(
							hierarchicalTransformComponent->transform.position,
							hierarchicalTransformComponent->transform.rotation,
							hierarchicalTransformComponent->transform.scale);
					}
				});

			aq::ecs::Foreach<TransformComponent, HierarchicalTransformComponent, SkeletalMeshComponent>(
				[](const aq::ecs::Entity&, TransformComponent*, HierarchicalTransformComponent* hierarchicalTransformComponent, SkeletalMeshComponent* skeletalMeshComponent)
				{
					skeletalMeshComponent->Update();
					if (skeletalMeshComponent->IsCompleted()) {
						skeletalMeshComponent->GetSkeletalMesh()->Update(
							hierarchicalTransformComponent->transform.position,
							hierarchicalTransformComponent->transform.rotation,
							hierarchicalTransformComponent->transform.scale);
					}
				});

			aq::ecs::Foreach<TransformComponent, HierarchicalTransformComponent, TerrainComponent>(
				[](const aq::ecs::Entity&, TransformComponent*, HierarchicalTransformComponent* hierarchicalTransformComponent, TerrainComponent* terrainComponent)
				{
					if (terrainComponent->IsCompleted()) {
						terrainComponent->GetChunk()->Update(
							hierarchicalTransformComponent->transform.position,
							hierarchicalTransformComponent->transform.rotation,
							hierarchicalTransformComponent->transform.scale);
					}
				});

			aq::ecs::Foreach<TransformComponent, HierarchicalTransformComponent, OceanComponent>(
				[](const aq::ecs::Entity&, TransformComponent*, HierarchicalTransformComponent* hierarchicalTransformComponent, OceanComponent* oceanComponent)
				{
					if (oceanComponent->IsCompleted()) {
						oceanComponent->GetMesh()->Update(
							hierarchicalTransformComponent->transform.position,
							hierarchicalTransformComponent->transform.rotation,
							hierarchicalTransformComponent->transform.scale);
					}
				});

			aq::ecs::Foreach<DecalComponent>(
				[](const aq::ecs::Entity&, DecalComponent* decalComponent)
				{
					decalComponent->Update();
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
			frame.camera.nearZ            = camera->GetNear();
			frame.camera.farZ             = camera->GetFar();

			// 現在の方針: 同じ描画対象を cameraType で指定した別カメラから映す。
			// カメラごとに描画対象を絞りたい場合は RenderItem / Component 側に
			// RenderLayer 等を追加して検討する。

			// --- フラスタムカリング 準備 ---
			// メインカメラの視錐台でアイテムを絞る。バウンディング未確定 (hasBounds==false)
			// のアイテムは保守的に常に可視とする。影 (castShadow) はライト視錐台で別途
			// 判定すべきため、ここではメインカメラ視錐台で切らない。
			math::Frustum frustum;
			frustum.FromViewProjection(camera->GetViewProjectionMatrix());
			const bool cullEnabled = frustumCullingEnabled_ && (cameraType == CameraType::Main);

			// オクリュージョン用 viewProj (= view * projection) と near/far
			const math::Matrix4x4 viewProj = camera->GetViewProjectionMatrix();
			const float occNear = camera->GetNear();
			const float occFar  = camera->GetFar();
			const bool occEnabled = occlusionCullingEnabled_ && (occlusionTester_ != nullptr)
			                      && (cameraType == CameraType::Main);

			uint32_t cullTotal   = 0;
			uint32_t cullVisible = 0;
			uint32_t occluded    = 0;
			auto isVisible = [&](const aq::rendering::RenderItem& item) -> bool
			{
				if (!cullEnabled || !item.hasBounds) return true;
				if (item.castShadow) return true;  // 影はライト視錐台で別途判定するため切らない
				++cullTotal;
				const math::AABB worldBox = item.localBounds.Transformed(item.worldMatrix);
				if (!frustum.Intersects(worldBox)) return false;  // 視錐台外
				// 視錐台内 → オクリュージョン判定
				if (occEnabled && occlusionTester_->IsOccluded(worldBox, viewProj, occNear, occFar))
				{
					++occluded;
					return false;
				}
				++cullVisible;
				return true;
			};

			// --- クラスタ(メッシュレット)カリング統計 ---
			// 可視アイテムのクラスタを集計し、フラスタム+バックフェース錐で削減可能な
			// 三角形量を計測する (まだ描画は減らさない・潜在効果の可視化)。
			const math::Vector3 camPos = camera->GetPosition();
			const bool clusterStats = clusterStatsEnabled_ && (cameraType == CameraType::Main);
			uint32_t clTotal = 0, clVis = 0, triTotal = 0, triVis = 0;
			uint32_t coneUsable = 0, backfaceCulled = 0;
			auto accumClusters = [&](const aq::rendering::RenderItem& item,
			                         const std::vector<aq::graphics::MeshCluster>* clusters)
			{
				if (!clusterStats || !clusters) return;
				for (const aq::graphics::MeshCluster& cl : *clusters)
				{
					++clTotal;
					triTotal += cl.triCount;
					if (cl.coneCutoff < 2.0f) ++coneUsable;

					const math::AABB worldBox = aq::math::AABB(cl.center, cl.extent).Transformed(item.worldMatrix);
					const bool frustumIn = frustum.Intersects(worldBox);
					bool backface = false;
					if (frustumIn)
					{
						DirectX::XMVECTOR axis = DirectX::XMVector3Normalize(
							DirectX::XMVector3TransformNormal(cl.coneAxis, item.worldMatrix));
						math::Vector3 viewDir(worldBox.center.x - camPos.x,
						                      worldBox.center.y - camPos.y,
						                      worldBox.center.z - camPos.z);
						const float d = DirectX::XMVectorGetX(
							DirectX::XMVector3Dot(DirectX::XMVector3Normalize(viewDir), axis));
						backface = (d >= cl.coneCutoff);
					}
					if (frustumIn && !backface)
					{
						++clVis;
						triVis += cl.triCount;
					}
					else if (backface)
					{
						++backfaceCulled;
					}
				}
			};

			// SimpleBox は forward-only（G-Buffer パスなし）
			aq::ecs::Foreach<BoxStaticMeshComponent>([&frame, &isVisible](const aq::ecs::Entity&, BoxStaticMeshComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					aq::rendering::RenderItem item;
					if (comp->GetStaticMesh()->FillRenderItem(item) && isVisible(item)) {
						frame.forwardItems.push_back(item);
					}
				});

			// ShaderType で deferred / forward を振り分け
			aq::ecs::Foreach<StaticMeshComponent>([&frame, &isVisible, &accumClusters](const aq::ecs::Entity&, StaticMeshComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					aq::rendering::RenderItem item;
					if (!comp->GetStaticMesh()->FillRenderItem(item)) return;
					if (!isVisible(item)) return;
					accumClusters(item, comp->GetStaticMesh()->GetClusters());

					using ShaderType = aq::graphics::StaticMesh::ShaderType;
					switch (comp->GetStaticMesh()->GetShaderType())
					{
					case ShaderType::ModelLit:
					case ShaderType::TerrainLit:
						// gbufferPS が nullptr になったため常に forward
						frame.forwardItems.push_back(item);
						break;
					case ShaderType::PBRLit:
					case ShaderType::TerrainPBRLit:
						// gbufferPS がない間はスキップ（forward fallback は MaterialCB 誤解釈を招くため禁止）
						if (item.gbufferPS)
							frame.items.push_back(item);
						break;
					case ShaderType::NormalModel:
					case ShaderType::SimpleBox:
					case ShaderType::OceanLit:
					default:
						frame.forwardItems.push_back(item);
						break;
					}
				});

			// Terrain は deferred（ステージオブジェクトのため G-Buffer に書き込む）
			aq::ecs::Foreach<TerrainComponent>([&frame, &isVisible](const aq::ecs::Entity&, TerrainComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					aq::rendering::RenderItem item;
					if (comp->GetChunk()->FillRenderItem(item) && isVisible(item)) {
						if (item.gbufferPS)
							frame.items.push_back(item);
						// gbufferPS 未ロード中はスキップ（MaterialCB 誤解釈を防ぐ）
					}
				});

			// SkeletalMesh: PBR は gbufferPS がない間スキップ、それ以外は gbufferPS 有無で振り分け
			aq::ecs::Foreach<SkeletalMeshComponent>([&frame, &isVisible, &accumClusters](const aq::ecs::Entity&, SkeletalMeshComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					aq::rendering::RenderItem item;
					if (!comp->GetSkeletalMesh()->FillRenderItem(item)) return;
					if (!isVisible(item)) return;
					accumClusters(item, comp->GetSkeletalMesh()->GetClusters());

					using ShaderType = aq::graphics::SkeletalMesh::ShaderType;
					const bool isPBR = comp->GetSkeletalMesh()->GetShaderType() == ShaderType::SkeletalPBRLit;

					if (item.gbufferPS)
						frame.items.push_back(item);
					else if (!isPBR)
						frame.forwardItems.push_back(item);
					// isPBR && !gbufferPS: gbufferPS ロード中はスキップ（MaterialCB 誤解釈を防ぐ）
				});

			// 海描画アイテム収集
			const float totalTime = aq::Engine::GetTotalTime();
			aq::ecs::Foreach<OceanComponent>([&frame, totalTime, &frustum, cullEnabled](const aq::ecs::Entity&, OceanComponent* comp)
				{
					if (!comp->IsCompleted()) return;
					aq::rendering::OceanRenderItem item;
					if (!comp->GetMesh()->FillRenderItem(item, comp->GetParams(), totalTime))
						return;

					// オブジェクト単位フラスタムカリング。海は meshResource_ を持たず hasBounds=false の
					// ため isVisible を使えない。ローカルグリッド [0,size]x{0}x[0,size] を、波の最大振幅で
					// Y を、Gerstner 水平変位の保険で XZ をわずかに膨らませた保守 AABB で視錐台判定する。
					if (cullEnabled)
					{
						const auto& p = comp->GetParams();
						float ampMax = 0.0f;
						for (int i = 0; i < 4; ++i)
						{
							const float a = p.waves[i].amplitude;
							ampMax += (a < 0.0f) ? -a : a;
						}
						const float half = p.size * 0.5f;
						const math::AABB local(
							math::Vector3(half, 0.0f, half),
							math::Vector3(half + ampMax, ampMax + 0.01f, half + ampMax));
						const math::AABB worldBox = local.Transformed(item.base.worldMatrix);
						if (!frustum.Intersects(worldBox))
							return;  // 視錐台外 → 描画しない
					}

					frame.oceanItems.push_back(item);
				});

			// 投影デカール収集 (ワールド変換を使って GBuffer へ投影)
			aq::ecs::Foreach<HierarchicalTransformComponent, DecalComponent>(
				[&frame](const aq::ecs::Entity&, HierarchicalTransformComponent* hierarchicalTransformComponent, DecalComponent* decalComponent)
				{
					if (!decalComponent->IsReady()) return;
					aq::rendering::DecalRenderItem item;
					if (decalComponent->FillDecalItem(item, hierarchicalTransformComponent->transform)) {
						frame.decalItems.push_back(item);
					}
				});

			// メインカメラのカリング統計を記録 (デバッグ表示用)
			if (cameraType == CameraType::Main)
			{
				cullingTotalCount_   = cullTotal;
				cullingVisibleCount_ = cullVisible;
				occludedCount_       = occluded;
				clusterTotal_        = clTotal;
				clusterVisible_      = clVis;
				clusterTriTotal_     = triTotal;
				clusterTriVisible_   = triVis;
				clusterConeUsable_   = coneUsable;
				clusterBackface_     = backfaceCulled;
			}
		}
	}
}
