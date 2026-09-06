#include "stdafx.h"
#include "SpeedCharacterComponentSystem.h"
#include "GameFlow.h"
#include "GameInput.h"
#include "GameAction.h"
#include "Stage/StageData.h"


namespace app
{
	namespace ecs
	{
		namespace
		{
			/** 走行チューニング (時速 300km ≒ 83m/s。設計 02 §5) */
			static constexpr float MAX_SPEED         = 83.0f;   // [m/s]
			static constexpr float ACCELERATION      = 30.0f;   // [m/s^2] 前入力
			static constexpr float BRAKE_DECEL       = 60.0f;   // [m/s^2] 後入力
			static constexpr float DRAG_DECEL        = 8.0f;    // [m/s^2] 入力なし
			static constexpr float LATERAL_SPEED     = 14.0f;   // [m/s] レーン移動
			static constexpr float LATERAL_MARGIN    = 0.5f;    // 路面端の余白
			static constexpr float JUMP_SPEED        = 13.0f;   // [m/s]
			static constexpr float GRAVITY           = 30.0f;   // [m/s^2]

		}


		/**
		 * 入力転写
		 */
		void PlayerInputSystem::Update()
		{
			if (GameFlow::Get().Context().gameplayPaused) { return; }

			aq::ecs::Foreach<PlayerInputComponent>([](const aq::ecs::Entity& /*entity*/, PlayerInputComponent* input)
				{
					// P1: 全プレイヤーがパッド 0 / キーボード共用。P5 で padIndex 毎の読取に置き換える。
					auto& gameInput = GameInput::Get();

					float moveX = gameInput.GetStick(GameAction::Move).x;
					float moveY = gameInput.GetStick(GameAction::Move).y;
					if (gameInput.IsPressed(GameAction::MoveRight))    { moveX += 1.0f; }
					if (gameInput.IsPressed(GameAction::MoveLeft))     { moveX -= 1.0f; }
					if (gameInput.IsPressed(GameAction::MoveForward))  { moveY += 1.0f; }
					if (gameInput.IsPressed(GameAction::MoveBackward)) { moveY -= 1.0f; }

					input->moveX = moveX < -1.0f ? -1.0f : (moveX > 1.0f ? 1.0f : moveX);
					input->moveY = moveY < -1.0f ? -1.0f : (moveY > 1.0f ? 1.0f : moveY);
					input->jumpTriggered = gameInput.IsTriggered(GameAction::Confirm);
				});
		}


		/************************************/




		/**
		 * スプライン走行
		 */
		void SpeedCharacterSystem::Update()
		{
			auto& context = GameFlow::Get().Context();
			if (context.gameplayPaused) { return; }

			const auto stageData = context.activeStage;
			if (!stageData || !stageData->spline.IsValid()) { return; }

			const float dt = aq::Engine::GetDeltaTime();
			const float lateralLimit = stageData->width * 0.5f - LATERAL_MARGIN;

			aq::ecs::Foreach<SpeedCharacterComponent>(
				[&](const aq::ecs::Entity& entity, SpeedCharacterComponent* character)
				{
					auto& ctx = aq::ecs::EntityContext::Get();
					const auto handle = entity.GetHandle();
					auto* input = ctx.GetComponent<PlayerInputComponent>(handle);
					auto* tc    = ctx.GetComponent<aq::ecs::TransformComponent>(handle);
					if (!input || !tc) { return; }

					// 加減速 (前入力で加速、後入力でブレーキ、入力なしは緩やかに減速)。
					if (input->moveY > 0.01f) {
						character->speed += input->moveY * ACCELERATION * dt;
					} else if (input->moveY < -0.01f) {
						character->speed += input->moveY * BRAKE_DECEL * dt;
					} else {
						character->speed -= DRAG_DECEL * dt;
					}
					if (character->speed < 0.0f)      { character->speed = 0.0f; }
					if (character->speed > MAX_SPEED) { character->speed = MAX_SPEED; }

					// 前進 + レーン移動。
					character->distance += character->speed * dt;
					character->lateral  += input->moveX * LATERAL_SPEED * dt;
					if (character->lateral < -lateralLimit) { character->lateral = -lateralLimit; }
					if (character->lateral >  lateralLimit) { character->lateral =  lateralLimit; }

					// ジャンプ / 重力 (height は路面相対)。
					if (character->grounded && input->jumpTriggered) {
						character->verticalVelocity = JUMP_SPEED;
						character->grounded         = false;
					}
					if (!character->grounded) {
						character->verticalVelocity -= GRAVITY * dt;
						character->height           += character->verticalVelocity * dt;
						if (character->height <= 0.0f) {
							character->height           = 0.0f;
							character->verticalVelocity = 0.0f;
							character->grounded         = true;
						}
					}

					// スプライン評価 → ワールド Transform 書き出し。
					const stage::CourseSpline::Frame frame = stageData->spline.Evaluate(character->distance);
					tc->position = frame.position + frame.right * character->lateral + frame.up * character->height;
					tc->rotation = frame.ToRotation();
				});
		}
	}
}
