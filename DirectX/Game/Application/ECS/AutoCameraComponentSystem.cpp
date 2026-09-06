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

					// 出力先: 1 人ならエンジンのメインカメラ、分割時はビュー毎のカメラ
					// (Main は CameraManager が Update するが、ビューカメラはここで Update まで行う)。
					const auto& context = GameFlow::Get().Context();
					if (context.playerCount >= 2)
					{
						aq::Camera* const camera = GameFlow::Get().ViewCamera(autoCam->viewIndex);
						if (camera) {
							float rx, ry, rw, rh;
							aquadash::GetSplitViewRect(context.playerCount, autoCam->viewIndex,
								static_cast<float>(aq::Engine::Get().GetRenderWidth()),
								static_cast<float>(aq::Engine::Get().GetRenderHeight()),
								rx, ry, rw, rh);
							camera->SetViewportSize(rw, rh);   // ビュー矩形のアスペクトを反映
							camera->SetNear(0.1f);
							camera->SetPosition(autoCam->smoothedPosition);
							camera->SetTarget(autoCam->smoothedTarget);
							camera->SetUp(autoCam->smoothedUp);
							camera->Update();
						}
					}
					else
					{
						aq::Camera* const camera = aq::CameraManager::Get().GetCamera(autoCam->cameraType);
						if (camera) {
							camera->SetPosition(autoCam->smoothedPosition);
							camera->SetTarget(autoCam->smoothedTarget);
							camera->SetUp(autoCam->smoothedUp);
						}
					}
				});
		}
	}
}
