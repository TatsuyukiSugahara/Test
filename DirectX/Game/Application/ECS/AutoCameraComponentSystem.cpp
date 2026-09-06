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
			/** 速度連動 FOV (スピード感演出)。エンジンのカメラ既定は 90° */
			static constexpr float BASE_FOV_DEG    = 90.0f;
			static constexpr float MAX_FOV_ADD_DEG = 13.0f;   // 最高速時の加算量
			static constexpr float FOV_SPEED_MIN   = 30.0f;   // [m/s] これ以下は基準 FOV
			static constexpr float FOV_SPEED_MAX   = 83.0f;   // [m/s] 最高速


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
					aq::math::Vector3 desiredTarget =
						aheadFrame.position + aheadFrame.up * autoCam->lookHeight;

					// ループ脱落中は distance が止まりカメラも脱落地点に残る。
					// 位置はそのままに注視点だけ落ちていくキャラへ向け、落下を見送る画にする。
					if (character->fallen) {
						if (const auto* targetTc =
								ctx.GetComponent<aq::ecs::TransformComponent>(autoCam->targetHandle)) {
							desiredTarget = targetTc->position;
						}
					}

					if (!autoCam->initialized) {
						autoCam->smoothedPosition = desiredPosition;
						autoCam->smoothedTarget   = desiredTarget;
						autoCam->smoothedUp       = backFrame.up;
						autoCam->initialized      = true;
					} else {
						const float factor = SmoothFactor(autoCam->sharpness, dt);
						autoCam->smoothedPosition += (desiredPosition - autoCam->smoothedPosition) * factor;
						autoCam->smoothedTarget   += (desiredTarget   - autoCam->smoothedTarget)   * factor;

						// up も平滑してループ中のロールを滑らかにする (補間後は正規化して長さを保つ)。
						autoCam->smoothedUp += (backFrame.up - autoCam->smoothedUp) * factor;
						if (!autoCam->smoothedUp.TryNormalize()) {
							autoCam->smoothedUp = backFrame.up;
						}
					}

					// 速度連動 FOV (リザルト中は基準へ戻す)。急変を避けて平滑する。
					{
						float speedRate = 0.0f;
						if (!GameFlow::Get().Context().gameplayPaused) {
							speedRate = (character->speed - FOV_SPEED_MIN) / (FOV_SPEED_MAX - FOV_SPEED_MIN);
							speedRate = speedRate < 0.0f ? 0.0f : (speedRate > 1.0f ? 1.0f : speedRate);
						}
						const float targetFov = BASE_FOV_DEG + MAX_FOV_ADD_DEG * speedRate;
						autoCam->smoothedFovDeg += (targetFov - autoCam->smoothedFovDeg) * SmoothFactor(6.0f, dt);
					}

					aq::Camera* const camera = aq::CameraManager::Get().GetCamera(autoCam->cameraType);
					if (camera) {
						camera->SetPosition(autoCam->smoothedPosition);
						camera->SetTarget(autoCam->smoothedTarget);
						camera->SetUp(autoCam->smoothedUp);
						camera->SetViewAngle(aq::math::DegToRadian(autoCam->smoothedFovDeg));
					}
				});
		}
	}
}
