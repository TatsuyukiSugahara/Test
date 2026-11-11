#include "aq.h"
#include "SampleModule.h"
#include "ECS/EntityContext.h"
#include "Component/TransformComponentSystem.h"
#include "Component/BodyComponentSystem.h"

namespace sample
{
	bool SampleModule::OnInitialize(aq::AppHost& host)
	{
		// 影 / ディファード / ポストプロセス / 空を既定構成で組む。
		// 個別に設定したいときは aq::RendererPreset を渡す。ここでは IBL(環境光の反射)と
		// SSR(画面に映っている物の映り込み。IBL が前提)を有効にする。
		aq::RendererPreset preset;
		preset.enableIBL = true;
		preset.enableSSR = true;
		if (!host.SetupStandardRenderers(preset)) {
			return false;
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
