#include "stdafx.h"
#include "CameraSteeringComponentSystem.h"
#include "Component/TransformComponentSystem.h"
#include "Graphics/Camera.h"
#include "Engine.h"
#include "GameInput.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#endif

namespace app
{
	namespace ecs
	{
		void CameraSteeringSystem::Update()
		{
			// CameraType ごとに最高 priority のコンポーネントを選択する
			constexpr size_t kMaxType = static_cast<size_t>(aq::CameraType::Maximum);
			CameraSteeringComponent* winners[kMaxType] = {};

			aq::ecs::Foreach<CameraSteeringComponent>(
				[&winners](const aq::ecs::Entity&, CameraSteeringComponent* comp)
				{
					if (!comp->isActive) return;

					const size_t idx = static_cast<size_t>(comp->cameraType);
					if (winners[idx] == nullptr || comp->priority > winners[idx]->priority)
						winners[idx] = comp;
				});

			auto& entityCtx = aq::ecs::EntityContext::Get();
			const float dt  = engine::Engine::GetDeltaTime();

			for (size_t i = 0; i < kMaxType; ++i)
			{
				CameraSteeringComponent* const comp = winners[i];
				if (comp == nullptr) continue;

				// --- 注視点を決定 ---
				aq::math::Vector3 targetPos = comp->lookAtPosition;

				if (comp->lookAtMode == CameraLookAtMode::TrackEntity
					&& entityCtx.IsValid(comp->lookAtEntity))
				{
					const auto* const tc = entityCtx.GetComponent<aq::ecs::TransformComponent>(comp->lookAtEntity);
					if (tc)
					{
						targetPos.x = tc->transform.position.x + comp->lookAtPosition.x;
						targetPos.y = tc->transform.position.y + comp->lookAtPosition.y;
						targetPos.z = tc->transform.position.z + comp->lookAtPosition.z;
					}
				}

				// --- カメラ位置を決定 ---
				// ManualView が先: cameraPos を確定したらそのまま進む
				// FollowOnly は else 側で positionMode を参照する
				aq::math::Vector3 cameraPos;

				if (comp->controlMode == CameraControlMode::ManualView)
				{
					const aq::math::Vector2 lookInput = GameInput::Get().GetStick(GameAction::Look);

					comp->viewYawDegrees += lookInput.x * comp->yawSpeedDegreesPerSecond * dt;

					const float ySign = comp->invertViewY ? -1.0f : 1.0f;
					comp->viewPitchDegrees = std::clamp(
						comp->viewPitchDegrees + lookInput.y * ySign * comp->pitchSpeedDegreesPerSecond * dt,
						comp->minViewPitchDegrees,
						comp->maxViewPitchDegrees);

					// キーボード矢印キー（invertViewY はスティックと同じく適用する）
					if (GameInput::Get().IsPressed(GameAction::LookLeft))  comp->viewYawDegrees -= comp->yawSpeedDegreesPerSecond * dt;
					if (GameInput::Get().IsPressed(GameAction::LookRight)) comp->viewYawDegrees += comp->yawSpeedDegreesPerSecond * dt;

					float keyPitchDelta = 0.0f;
					if (GameInput::Get().IsPressed(GameAction::LookUp))   keyPitchDelta += ySign * comp->pitchSpeedDegreesPerSecond * dt;
					if (GameInput::Get().IsPressed(GameAction::LookDown)) keyPitchDelta -= ySign * comp->pitchSpeedDegreesPerSecond * dt;
					comp->viewPitchDegrees = std::clamp(
						comp->viewPitchDegrees + keyPitchDelta,
						comp->minViewPitchDegrees,
						comp->maxViewPitchDegrees);

					const float yaw   = DirectX::XMConvertToRadians(comp->viewYawDegrees);
					const float pitch = DirectX::XMConvertToRadians(comp->viewPitchDegrees);

					// 球面座標 → ワールド空間オフセット（前方 = -Z）
					const DirectX::XMVECTOR offset = DirectX::XMVectorSet(
						 sinf(yaw)  * cosf(pitch) * comp->distanceFromTarget,
						 sinf(pitch)              * comp->distanceFromTarget,
						-cosf(yaw)  * cosf(pitch) * comp->distanceFromTarget,
						0.0f);

					DirectX::XMStoreFloat3(&cameraPos.vector,
						DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&targetPos.vector), offset));
				}
				else  // FollowOnly: positionMode でカメラ位置を決定
				{
					if (comp->positionMode == CameraPositionMode::OffsetFromTarget)
					{
						aq::math::Vector3 worldOffset = comp->cameraOffset;

						if (comp->offsetInLocalSpace
							&& comp->lookAtMode == CameraLookAtMode::TrackEntity
							&& entityCtx.IsValid(comp->lookAtEntity))
						{
							const auto* const tc = entityCtx.GetComponent<aq::ecs::TransformComponent>(comp->lookAtEntity);
							if (tc)
							{
								const DirectX::XMMATRIX rot = DirectX::XMLoadFloat4x4(
									&tc->transform.rotationMatrix.matrix);
								DirectX::XMStoreFloat3(&worldOffset.vector,
									DirectX::XMVector3TransformNormal(
										DirectX::XMLoadFloat3(&comp->cameraOffset.vector), rot));
							}
						}

						cameraPos.x = targetPos.x + worldOffset.x;
						cameraPos.y = targetPos.y + worldOffset.y;
						cameraPos.z = targetPos.z + worldOffset.z;
					}
					else  // FixedPosition
					{
						cameraPos = comp->fixedPosition;
					}
				}

				// --- スムージング ---
				if (!comp->smoothingInitialized)
				{
					comp->smoothedTarget   = targetPos;
					comp->smoothedPosition = cameraPos;
					comp->smoothingInitialized = true;
				}
				else
				{
					const float tTarget = 1.0f - expf(-comp->lookAtSharpness   * dt);
					const float tPos    = 1.0f - expf(-comp->positionSharpness * dt);

					DirectX::XMStoreFloat3(&comp->smoothedTarget.vector,
						DirectX::XMVectorLerp(
							DirectX::XMLoadFloat3(&comp->smoothedTarget.vector),
							DirectX::XMLoadFloat3(&targetPos.vector),
							tTarget));

					DirectX::XMStoreFloat3(&comp->smoothedPosition.vector,
						DirectX::XMVectorLerp(
							DirectX::XMLoadFloat3(&comp->smoothedPosition.vector),
							DirectX::XMLoadFloat3(&cameraPos.vector),
							tPos));
				}

				// --- Camera に反映 ---
				aq::Camera* const camera = aq::CameraManager::Get().GetCamera(comp->cameraType);
				if (camera)
				{
					camera->SetTarget(comp->smoothedTarget);
					camera->SetPosition(comp->smoothedPosition);
				}
			}
		}


		void CameraEffectSystem::Update()
		{
			const float dt         = engine::Engine::GetDeltaTime();
			const float totalTime  = engine::Engine::GetTotalTime();

			aq::ecs::Foreach<CameraEffectComponent>(
				[dt, totalTime](const aq::ecs::Entity&, CameraEffectComponent* comp)
				{
					if (!comp->isActive) return;

					if (comp->shakeDuration > 0.0f)
					{
						comp->shakeDuration -= dt;

						if (comp->shakeDuration > 0.0f)
						{
							const float mag = comp->shakeMagnitude;
							comp->shakeOffset.x = sinf(totalTime * 73.1f) * mag;
							comp->shakeOffset.y = cosf(totalTime * 89.7f) * mag;
							comp->shakeOffset.z = 0.0f;
						}
						else
						{
							comp->shakeDuration = 0.0f;
							comp->shakeOffset   = {};
						}
					}

					if (comp->shakeOffset.IsZero()) return;

					aq::Camera* const camera = aq::CameraManager::Get().GetCamera(comp->cameraType);
					if (camera)
					{
						const aq::math::Vector3& pos = camera->GetPosition();
						camera->SetPosition(aq::math::Vector3(
							pos.x + comp->shakeOffset.x,
							pos.y + comp->shakeOffset.y,
							pos.z + comp->shakeOffset.z));
					}
				});
		}


#ifdef AQ_DEBUG_IMGUI
		void CameraEffectSystem::DebugRenderMenu()
		{
			ImGui::MenuItem("Camera Effect", nullptr, &show_);
		}


		void CameraEffectSystem::DebugRender()
		{
			if (!show_) return;

			if (ImGui::Begin("Camera Effect"))
			{
				aq::ecs::Foreach<CameraEffectComponent>(
					[](const aq::ecs::Entity& entity, CameraEffectComponent* comp)
					{
						ImGui::PushID(static_cast<int>(entity.GetID()));
						ImGui::Text("Entity %u | Active: %d", entity.GetID(), comp->isActive);
						ImGui::SliderFloat("shakeMagnitude", &comp->shakeMagnitude, 0.0f, 1.0f);
						ImGui::SliderFloat("shakeDuration",  &comp->shakeDuration,  0.0f, 5.0f);
						ImGui::Separator();
						ImGui::PopID();
					});
			}
			ImGui::End();
		}
#endif


#ifdef AQ_DEBUG_IMGUI
		void CameraSteeringSystem::DebugRenderMenu()
		{
			ImGui::MenuItem("Camera Steering", nullptr, &show_);
		}


		void CameraSteeringSystem::DebugRender()
		{
			if (!show_) return;

			if (ImGui::Begin("Camera Steering"))
			{
				aq::ecs::Foreach<CameraSteeringComponent>(
					[](const aq::ecs::Entity& entity, CameraSteeringComponent* comp)
					{
						ImGui::PushID(static_cast<int>(entity.GetID()));
						ImGui::Text("Entity %u | Active: %d | Priority: %d",
							entity.GetID(), comp->isActive, comp->priority);

						const bool isManual = comp->controlMode == CameraControlMode::ManualView;
						ImGui::Text("ControlMode: %s", isManual ? "ManualView" : "FollowOnly");

						if (isManual)
						{
							ImGui::TextDisabled("positionMode: ignored (ManualView active)");
							ImGui::SliderFloat("viewYaw",   &comp->viewYawDegrees,   -180.0f, 180.0f);
							ImGui::SliderFloat("viewPitch", &comp->viewPitchDegrees,
								comp->minViewPitchDegrees, comp->maxViewPitchDegrees);
							ImGui::SliderFloat("distance",  &comp->distanceFromTarget, 1.0f, 50.0f);
						}
						else
						{
							ImGui::Text("positionMode: %s",
								comp->positionMode == CameraPositionMode::OffsetFromTarget
									? "OffsetFromTarget" : "FixedPosition");
						}

						ImGui::SliderFloat("lookAtSharpness",   &comp->lookAtSharpness,   0.1f, 20.0f);
						ImGui::SliderFloat("positionSharpness", &comp->positionSharpness,  0.1f, 20.0f);
						ImGui::Separator();
						ImGui::PopID();
					});
			}
			ImGui::End();
		}
#endif
	}
}
