#include "aq.h"
#include "SampleModule.h"
#include "ECS/EntityContext.h"
#include "Component/TransformComponentSystem.h"
#include "Component/BodyComponentSystem.h"
#include "Rendering/Pipeline/RenderPipeline.h"
#include "Rendering/Pipeline/Passes/DoFPass.h"

namespace sample
{
	bool SampleModule::OnInitialize(aq::AppHost& host)
	{
		// 影 / ディファード / ポストプロセス / 空を既定構成で組む。
		// 個別に設定したいときは aq::RendererPreset を渡す。ここでは IBL(環境光の反射)と
		// SSR(画面に映っている物の映り込み。IBL が前提)と DoF(被写界深度)と
		// SSAO(物の接地や隙間の環境光の陰)を有効にする。
		aq::RendererPreset preset;
		preset.enableIBL  = true;
		preset.enableSSR  = true;
		preset.enableDoF  = true;
		preset.enableSSAO = true;

		// DoF のピント位置などは preset ではなくパスの setter で決めるので、
		// SetupStandardRenderers を使わずに組み、設定してから渡す。
		{
			auto pipeline = host.BuildStandardPipeline(preset).Build(
				aq::Engine::Get().GetRenderWidth(), aq::Engine::Get().GetRenderHeight());

			// ピントは見本列(原点)に合わせる。カメラから原点までの距離 ≒ 6.67。
			// 列の前後はくっきり、手前の床と奥の空がぼける。
			if (auto* dof = pipeline ? pipeline->Find<aq::rendering::DoFPass>() : nullptr) {
				dof->SetFocusDistance(6.67f);
				dof->SetFocusRange(1.5f);
				dof->SetNearTransition(3.0f);
				dof->SetFarTransition(3.0f);
				dof->SetMaxRadius(12.0f);
			}

			if (!host.SetRenderPipeline(std::move(pipeline))) {
				return false;
			}
		}

		// カメラ。10 個の箱の列全体が収まるよう、斜め上やや遠くから原点を見る。
		aq::Camera* camera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
		camera->SetPosition(aq::math::Vector3(1.5f, 2.5f, -6.0f));
		camera->SetTarget(aq::math::Vector3(0.0f, 0.0f, 0.0f));
		camera->SetViewportSize(
			static_cast<float>(aq::Engine::Get().GetRenderWidth()),
			static_cast<float>(aq::Engine::Get().GetRenderHeight()));

		// PBR マテリアルの見本列。metallic(上段 0 / 下段 1)× roughness(左から 0, 0.25, 0.5, 0.75, 1)の
		// 10 個の箱を並べ、IBL の反射が金属度と粗さでどう変わるかを見る。
		// BoxStaticMeshComponent はエンジンが持つ単位キューブを描くので、アセットを 1 個も用意しなくても絵が出る。
		{
			static constexpr int   ROUGHNESS_COUNT = 5;
			static constexpr float SPACING         = 1.5f;

			for (int row = 0; row < 2; ++row)
			{
				const float metallic = static_cast<float>(row);
				for (int col = 0; col < ROUGHNESS_COUNT; ++col)
				{
					const float roughness = static_cast<float>(col) / static_cast<float>(ROUGHNESS_COUNT - 1);

					auto entity = aq::ecs::EntityContext::Get().CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::BoxStaticMeshComponent>();

					// 原点を中心に 5 列 × 2 段。上段が metallic 0、下段が metallic 1
					auto* transform = entity.GetComponent<aq::ecs::TransformComponent>();
					transform->position.Set(
						(static_cast<float>(col) - 2.0f) * SPACING,
						(row == 0 ? 0.5f : -0.5f) * SPACING,
						0.0f);
					transform->scale.Set(1.0f);

					auto* box = entity.GetComponent<aq::ecs::BoxStaticMeshComponent>();
					box->SetColor(aq::math::Vector4(0.2f, 0.6f, 1.0f, 1.0f));
					box->SetPBRMaterial(metallic, roughness);
				}
			}
		}

		// SSR の見本の床。見本列の下に、ほぼ鏡面の金属の板を敷き、列の箱が床に映るのを見る。
		// 箱(下段の底は y = -1.25)から少し離して置く。単位キューブを平たく伸ばして板にする。
		{
			auto entity = aq::ecs::EntityContext::Get().CreateEntity<
				aq::ecs::TransformComponent,
				aq::ecs::HierarchicalTransformComponent,
				aq::ecs::BoxStaticMeshComponent>();

			auto* transform = entity.GetComponent<aq::ecs::TransformComponent>();
			transform->position.Set(0.0f, -1.6f, 0.0f);
			transform->scale.Set(10.0f, 0.1f, 6.0f);

			auto* box = entity.GetComponent<aq::ecs::BoxStaticMeshComponent>();
			box->SetColor(aq::math::Vector4(0.8f, 0.8f, 0.8f, 1.0f));
			box->SetPBRMaterial(1.0f, 0.05f);
		}

		// SSAO の見本の台。床と下段の箱は金属(拡散の環境光が無い)で AO が見えないので、
		// 見本列の左手前に非金属の台を置き、その上に小さな箱を載せて接地の陰を見る。
		{
			auto pedestal = aq::ecs::EntityContext::Get().CreateEntity<
				aq::ecs::TransformComponent,
				aq::ecs::HierarchicalTransformComponent,
				aq::ecs::BoxStaticMeshComponent>();

			auto* transform = pedestal.GetComponent<aq::ecs::TransformComponent>();
			transform->position.Set(-3.8f, -1.35f, -2.0f);   // 床の上面(y = -1.55)に置く。上面は y = -1.15
			transform->scale.Set(1.6f, 0.4f, 1.6f);

			auto* box = pedestal.GetComponent<aq::ecs::BoxStaticMeshComponent>();
			box->SetColor(aq::math::Vector4(0.8f, 0.75f, 0.7f, 1.0f));
			box->SetPBRMaterial(0.0f, 0.8f);
		}
		{
			auto topBox = aq::ecs::EntityContext::Get().CreateEntity<
				aq::ecs::TransformComponent,
				aq::ecs::HierarchicalTransformComponent,
				aq::ecs::BoxStaticMeshComponent>();

			auto* transform = topBox.GetComponent<aq::ecs::TransformComponent>();
			transform->position.Set(-3.8f, -0.85f, -2.0f);   // 台の上面に接する(底は y = -1.15)
			transform->scale.Set(0.6f);

			auto* box = topBox.GetComponent<aq::ecs::BoxStaticMeshComponent>();
			box->SetColor(aq::math::Vector4(0.8f, 0.75f, 0.7f, 1.0f));
			box->SetPBRMaterial(0.0f, 0.8f);
		}

		return true;
	}


	void RegisterAppModules(aq::AppModuleRegistry& registry)
	{
		// 名前は SampleModule::GetName と揃える(起動引数 -app= と照合する)
		registry.Register("Sample", []
			{
				return std::make_unique<SampleModule>();
			});
	}
}
