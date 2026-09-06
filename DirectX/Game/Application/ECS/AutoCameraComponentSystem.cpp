#include "stdafx.h"
#include "AutoCameraComponentSystem.h"
#include "SpeedCharacterComponentSystem.h"
#include "GameFlow.h"
#include "Stage/StageData.h"


namespace app
{
	namespace ecs
	{
		namespace
		{
			// 指数平滑の補間係数 (フレームレート非依存)。
			float SmoothFactor(const float sharpness, const float dt)
			{
				return 1.0f - expf(-sharpness * dt);
			}
		}


		void AutoCameraSystem::Update()
		{
			const auto stageData = GameFlow::Get().Context().activeStage;
			if (!stageData || !stageData->spline.IsValid()) { return; }

			const float dt = aq::Engine::GetDeltaTime();

			aq::ecs::Foreach<AutoCameraComponent>(
				[&](const aq::ecs::Entity& /*entity*/, AutoCameraComponent* autoCam)
				{
					auto& ctx = aq::ecs::EntityContext::Get();
					if (!ctx.IsValid(autoCam->targetHandle)) { return; }
					const auto* character = ctx.GetComponent<SpeedCharacterComponent>(autoCam->targetHandle);
					if (!character) { return; }

					// カメラ位置: 対象の少し後方のスプライン点 + up 方向オフセット。
					// 注視点: 少し前方のスプライン点。ループ中も up がスプライン準拠なので自然にロールする。
					const stage::CourseSpline::Frame backFrame =
						stageData->spline.Evaluate(character->distance - autoCam->backDistance);
					const stage::CourseSpline::Frame aheadFrame =
						stageData->spline.Evaluate(character->distance + autoCam->aheadDistance);

					const aq::math::Vector3 desiredPosition =
						backFrame.position + backFrame.up * autoCam->cameraHeight
						+ backFrame.right * (character->lateral * 0.5f);
					const aq::math::Vector3 desiredTarget =
						aheadFrame.position + aheadFrame.up * autoCam->lookHeight;

					if (!autoCam->initialized) {
						autoCam->smoothedPosition = desiredPosition;
						autoCam->smoothedTarget   = desiredTarget;
						autoCam->initialized      = true;
					} else {
						const float factor = SmoothFactor(autoCam->sharpness, dt);
						autoCam->smoothedPosition += (desiredPosition - autoCam->smoothedPosition) * factor;
						autoCam->smoothedTarget   += (desiredTarget   - autoCam->smoothedTarget)   * factor;
					}

					// 注: Camera に up 指定 API が無いため、ループ中のロール (up 反映) は
					//     P4 でエンジン側に SetUp を足してから対応する。P1 は平坦コースなので影響なし。
					aq::Camera* const camera = aq::CameraManager::Get().GetCamera(autoCam->cameraType);
					if (camera) {
						camera->SetPosition(autoCam->smoothedPosition);
						camera->SetTarget(autoCam->smoothedTarget);
					}
				});
		}
	}
}
