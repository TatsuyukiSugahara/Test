#include "aq.h"
#include "Application.h"
#include "ECS/EntityContext.h"
#include "Component/TransformComponentSystem.h"
#include "Component/BodyComponentSystem.h"

namespace sample
{
	bool Application::OnInitialize()
	{
		// 影 / ディファード / ポストプロセス / 空を既定構成で組む。
		// 個別に設定したいときは aq::RendererPreset を渡す。
		SetupStandardRenderers();

		// カメラ。斜め上から原点を見る。
		aq::Camera* camera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
		camera->SetPosition(aq::math::Vector3(3.0f, 3.0f, -5.0f));
		camera->SetTarget(aq::math::Vector3(0.0f, 0.0f, 0.0f));
		camera->SetViewportSize(
			static_cast<float>(aq::Engine::Get().GetRenderWidth()),
			static_cast<float>(aq::Engine::Get().GetRenderHeight()));

		// 箱を 1 個。BoxStaticMeshComponent はエンジンが持つ単位キューブを描くので、
		// アセットを 1 個も用意しなくても絵が出る。
		{
			auto entity = aq::ecs::EntityContext::Get().CreateEntity<
				aq::ecs::TransformComponent,
				aq::ecs::HierarchicalTransformComponent,
				aq::ecs::BoxStaticMeshComponent>();

			auto* transform = entity.GetComponent<aq::ecs::TransformComponent>();
			transform->position.Set(0.0f, 0.0f, 0.0f);
			transform->scale.Set(1.0f);

			entity.GetComponent<aq::ecs::BoxStaticMeshComponent>()
				->SetColor(aq::math::Vector4(0.2f, 0.6f, 1.0f, 1.0f));
		}

		return true;
	}
}
