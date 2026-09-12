#include "stdafx.h"
#include "SpeedCharacterComponentSystem.h"
#include "SessionComponent.h"
#include "CoinComponentSystem.h"
#include "GameInput.h"
#include "GameAction.h"
#include "Stage/StageData.h"
#include "Component/AnimationComponentSystem.h"
#include "Sound/SoundClip.h"
#include "Sound/SoundEngine.h"


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

			/** ブーストパッド (設計 05 §P21-1) */
			static constexpr float BOOST_SPEED_MULTIPLIER = 1.50f;   // MAX_SPEED に対する倍率 (≒125m/s ≒448km/h)
			static constexpr float BOOST_SEC              = 2.0f;    // [s] ブーストの持続時間
			// ブースト切れ後に MAX_SPEED まで落とす減速度。通常の DRAG_DECEL とは
			// 別にして「ブーストが切れて伸びが止まる」感触を作る。
			static constexpr float BOOST_DECEL            = 18.0f;   // [m/s^2]
			static const char* BOOST_SE_PATH = "Assets/Sound/Boost.wav";

			/** エアトリック (設計 05 §P22-2) */
			// 打ち上げ直後の低空でトリックに入ると、回転し切る前に着地して失敗しか出ない。
			// 通常ジャンプの範囲では始まらない高さをしきい値にして、ジャンプ台専用の操作にする。
			static constexpr float    TRICK_MIN_HEIGHT      = 1.5f;    // [m] これ未満の高さでは A を押してもトリックにしない
			static constexpr float    TRICK_SPIN_SEC        = 0.45f;   // [s] 1 回転にかかる時間
			static constexpr uint32_t TRICK_SCORE           = 300u;    // 1 回転あたりの加点
			static constexpr float    TRICK_BOOST_SEC       = 1.2f;    // [s] 着地成功時のブースト時間
			static constexpr float    TRICK_FAIL_SPEED_RATE = 0.6f;    // 回転の途中で着地したときの速度倍率
			static constexpr float    TRICK_TWO_PI          = 6.28318530718f;   // [rad] 1 回転ぶんの角度
			static const char* TRICK_SPIN_SE_PATH = "Assets/Sound/TrickSpin.wav";
			static const char* TRICK_LAND_SE_PATH = "Assets/Sound/TrickLand.wav";

			/** ループ脱落判定 */
			static constexpr float INVERTED_UP_Y     = 0.25f;   // 路面 up がこれ未満なら「上下逆さ寄り」
			static constexpr float LOOP_MIN_SPEED    = 35.0f;   // [m/s] 逆さ路面に張り付いていられる下限速度

			/** 速度連動の R2 抵抗 (DualSense のアダプティブトリガー) */
			static constexpr float TRIGGER_START_POS    = 0.15f;   // 引き始めから重くする
			static constexpr float TRIGGER_MAX_STRENGTH = 0.9f;    // 最高速での抵抗
			static constexpr float TRIGGER_MIN_SPEED    = 2.0f;    // [m/s] これ未満は解除

			/** アニメ切替 (idle / run / jump) */
			static constexpr float RUN_MIN_SPEED       = 2.0f;         // [m/s] これ以上で走行アニメ
			static constexpr float RUN_PLAYSPEED_BASE  = 0.5f;         // 再生速度 = BASE + speed * SCALE
			static constexpr float RUN_PLAYSPEED_SCALE = 1.0f / 12.0f;
			static constexpr float RUN_PLAYSPEED_MIN   = 0.7f;
			static constexpr float RUN_PLAYSPEED_MAX   = 2.8f;


			// 走行状態から目標アニメを決め、変化したときだけ切り替える
			// (毎フレーム Play すると先頭へ巻き戻ってしまうため)。
			void UpdateCharacterAnimation(aq::ecs::EntityContext& ctx, const aq::ecs::EntityHandle& handle,
			                              SpeedCharacterComponent* character)
			{
				auto* anim = ctx.GetComponent<aq::ecs::AnimationComponent>(handle);
				if (!anim) { return; }

				uint32_t target  = aqHash32("idle");
				bool     looping = true;
				if (character->fallen || !character->grounded) {
					target  = aqHash32("jump");
					looping = false;   // 滞空姿勢は最終フレームで止める
				} else if (character->speed >= RUN_MIN_SPEED) {
					target = aqHash32("run");
				}

				if (character->currentAnimHash != target) {
					anim->Play(target, looping);
					character->currentAnimHash = target;
				}

				// 走行中は足の回転を速度に合わせる (それ以外は等速へ戻す)。
				if (target == aqHash32("run")) {
					anim->SetPlaySpeed(aq::math::Clamp(
						RUN_PLAYSPEED_BASE + character->speed * RUN_PLAYSPEED_SCALE,
						RUN_PLAYSPEED_MIN, RUN_PLAYSPEED_MAX));
				} else {
					anim->SetPlaySpeed(1.0f);
				}
			}


			// 速いほど R2 を重くして加速の手応えを出す (ほぼ停止しているときは解除)。
			// パッドが未接続なら Pad 側で捨てられるので、ここでは接続を気にしない。
			// ブースト SE を鳴らす (コイン取得 SE と同じ経路。サウンド未初期化の環境では黙って無視される)。
			void PlayBoostSE()
			{
				if (aq::sound::SoundEngine::IsAvailable()) {
					auto clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(BOOST_SE_PATH);
					aq::sound::SoundEngine::Get().Play(clip, aq::sound::SoundBusId::SE);
				}
			}


			// トリックの回転開始 SE。
			void PlayTrickSpinSE()
			{
				if (aq::sound::SoundEngine::IsAvailable()) {
					auto clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(TRICK_SPIN_SE_PATH);
					aq::sound::SoundEngine::Get().Play(clip, aq::sound::SoundBusId::SE);
				}
			}


			// トリックの着地成功 SE (失敗時は鳴らさない)。
			void PlayTrickLandSE()
			{
				if (aq::sound::SoundEngine::IsAvailable()) {
					auto clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(TRICK_LAND_SE_PATH);
					aq::sound::SoundEngine::Get().Play(clip, aq::sound::SoundBusId::SE);
				}
			}


			void UpdateTriggerResistance(const float speed)
			{
				const float ratio    = aq::math::Clamp(speed / MAX_SPEED, 0.0f, 1.0f);
				const float strength = (speed < TRIGGER_MIN_SPEED) ? 0.0f : ratio * TRIGGER_MAX_STRENGTH;
				GameInput::Get().SetTriggerResistance(aq::hid::PadAxis::RTrigger, TRIGGER_START_POS, strength);
			}
		}


		/**
		 * 入力転写
		 */
		void PlayerInputSystem::Update()
		{
			// セッション状態はワーカースレッドから読むだけ (書き込みはメインスレッドの状態クラス)。
			const auto* session =
				aq::ecs::EntityContext::Get().GetSingletonComponent<const SessionComponent>();
			if (!session) { return; }
			if (session->gameplayPaused) { return; }

			aq::ecs::Foreach<PlayerInputComponent>([](const aq::ecs::Entity& /*entity*/, PlayerInputComponent* input)
				{
					// 一人プレイ専用: キーボードとパッド 0 を GameInput 経由で合成して読む。
					auto& gameInput = GameInput::Get();

					float moveX = gameInput.GetStick(GameAction::Move).x;
					float moveY = gameInput.GetStick(GameAction::Move).y;
					if (gameInput.IsPressed(GameAction::MoveRight))    { moveX += 1.0f; }
					if (gameInput.IsPressed(GameAction::MoveLeft))     { moveX -= 1.0f; }
					if (gameInput.IsPressed(GameAction::MoveForward))  { moveY += 1.0f; }
					if (gameInput.IsPressed(GameAction::MoveBackward)) { moveY -= 1.0f; }

					input->moveX = aq::math::Clamp(moveX, -1.0f, 1.0f);
					input->moveY = aq::math::Clamp(moveY, -1.0f, 1.0f);
					input->jumpTriggered = gameInput.IsTriggered(GameAction::Confirm);
				});
		}


		/************************************/




		/**
		 * スプライン走行
		 */
		void SpeedCharacterSystem::Update()
		{
			// セッション状態はワーカースレッドから読むだけ (書き込みはメインスレッドの状態クラス)。
			const auto* session =
				aq::ecs::EntityContext::Get().GetSingletonComponent<const SessionComponent>();
			if (!session) { return; }
			if (session->gameplayPaused) { return; }

			const auto stageData = session->activeStage;
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

					// 脱落中はコース追従を離れ、ワールド重力だけで落ちる。
					// 姿勢は剥がれた瞬間のまま (回転を上書きすると落下方向と食い違って見えるため)。
					// 入力も効かない: 復帰手段はリスポーンのみ。
					if (character->fallen) {
						character->worldVelocity.y -= GRAVITY * dt;
						tc->position += character->worldVelocity * dt;
						UpdateCharacterAnimation(ctx, handle, character);
						UpdateTriggerResistance(0.0f);
						return;
					}

					// 加減速 (前入力で加速、後入力でブレーキ、入力なしは緩やかに減速)。
					if (input->moveY > 0.01f) {
						character->speed += input->moveY * ACCELERATION * dt;
					} else if (input->moveY < -0.01f) {
						character->speed += input->moveY * BRAKE_DECEL * dt;
					} else {
						character->speed -= DRAG_DECEL * dt;
					}
					// ブースト中だけ上限を引き上げる。
					const float speedLimit = (character->boostTimer > 0.0f)
						? MAX_SPEED * BOOST_SPEED_MULTIPLIER
						: MAX_SPEED;
					character->speed = aq::math::Clamp(character->speed, 0.0f, speedLimit);
					UpdateTriggerResistance(character->speed);

					// ブーストの残り時間を減らす。切れた後は DRAG_DECEL より強い BOOST_DECEL で
					// MAX_SPEED まで引き戻し、「伸びが止まる」感触を出す。
					if (character->boostTimer > 0.0f) {
						character->boostTimer -= dt;
					} else if (character->speed > MAX_SPEED) {
						character->speed -= BOOST_DECEL * dt;
						if (character->speed < MAX_SPEED) { character->speed = MAX_SPEED; }
					}

					// 前進 + レーン移動。跨ぎ判定に使うので積分前の distance を退避しておく。
					character->prevDistance = character->distance;
					character->distance += character->speed * dt;
					character->lateral  += input->moveX * LATERAL_SPEED * dt;
					character->lateral   = aq::math::Clamp(character->lateral, -lateralLimit, lateralLimit);

					// ブーストパッド: 最高速では 1 フレームで 2m 進むため位置の単純比較では
					// 取りこぼす。前フレームとの区間にパッドが挟まったかで判定する。
					// 複数のパッドを跨いだフレームでも発動は 1 回でよい。
					for (const auto& pad : stageData->boostPads) {
						if (pad.distance <= character->prevDistance) { continue; }
						if (pad.distance >  character->distance)     { break; }   // boostPads は distance 昇順
						if (fabsf(character->lateral - pad.lateral) > pad.width * 0.5f) { continue; }

						// じわじわ加速させると「踏んだ」感触が出ないので即座に跳ね上げる。
						const float boostSpeed = MAX_SPEED * BOOST_SPEED_MULTIPLIER;
						if (character->speed < boostSpeed) { character->speed = boostSpeed; }
						character->boostTimer = BOOST_SEC;
						PlayBoostSE();
						break;
					}

					// ジャンプ台: ブーストパッドとまったく同じ跨ぎ判定。
					// 前進速度は変えず滞空だけを作り、それをトリックの機会に充てる。
					for (const auto& ramp : stageData->ramps) {
						if (ramp.distance <= character->prevDistance) { continue; }
						if (ramp.distance >  character->distance)     { break; }   // ramps は distance 昇順
						if (fabsf(character->lateral - ramp.lateral) > ramp.width * 0.5f) { continue; }

						character->verticalVelocity = ramp.power;
						character->grounded         = false;
						break;
					}

					// ジャンプ / 重力 (height は路面相対)。
					if (character->grounded && input->jumpTriggered) {
						character->verticalVelocity = JUMP_SPEED;
						character->grounded         = false;
					}

					// エアトリックの開始。接地中のジャンプ判定より後に置くことで、
					// 地上では必ずジャンプが優先される (踏み切った直後は height が 0 なので始まらない)。
					if (!character->grounded && character->height >= TRICK_MIN_HEIGHT && input->jumpTriggered) {
						++character->trickPendingCount;
						// 回転していなければ積んだぶんを 1 つ消費して即座に回り始める。
						if (character->trickSpinTimer <= 0.0f) {
							--character->trickPendingCount;
							character->trickSpinTimer = TRICK_SPIN_SEC;
							PlayTrickSpinSE();
						}
					}

					// エアトリックの進行。1 回転し切るたびにチェーンを 1 つ消費して次の回転へ移り、
					// 残っていなければ見た目の回転角を 0 へ戻して終了する。
					if (character->trickSpinTimer > 0.0f) {
						character->trickSpinTimer -= dt;
						character->trickRoll      += (TRICK_TWO_PI / TRICK_SPIN_SEC) * dt;
						if (character->trickSpinTimer <= 0.0f) {
							++character->trickCompletedCount;
							if (character->trickPendingCount > 0) {
								--character->trickPendingCount;
								character->trickSpinTimer = TRICK_SPIN_SEC;
								PlayTrickSpinSE();
							} else {
								character->trickSpinTimer = 0.0f;
								character->trickRoll      = 0.0f;
							}
						}
					}

					if (!character->grounded) {
						character->verticalVelocity -= GRAVITY * dt;
						character->height           += character->verticalVelocity * dt;
						if (character->height <= 0.0f) {
							character->height           = 0.0f;
							character->verticalVelocity = 0.0f;
							character->grounded         = true;

							// 着地の評価。回転を完了し切っていれば加点 + ブースト、
							// 途中で着地したら加点なしで減速させる。
							if (character->trickSpinTimer <= 0.0f && character->trickCompletedCount > 0) {
								auto* score = ctx.GetComponent<PlayerScoreComponent>(handle);
								if (score) {
									score->score += TRICK_SCORE * character->trickCompletedCount;
								}
								// ブーストパッドと同じ扱い。即座に速度を引き上げて伸びを作る。
								const float boostSpeed = MAX_SPEED * BOOST_SPEED_MULTIPLIER;
								if (character->speed < boostSpeed) { character->speed = boostSpeed; }
								character->boostTimer = TRICK_BOOST_SEC;
								PlayTrickLandSE();
							} else if (character->trickSpinTimer > 0.0f) {
								character->speed *= TRICK_FAIL_SPEED_RATE;
							}

							character->trickSpinTimer      = 0.0f;
							character->trickPendingCount   = 0;
							character->trickCompletedCount = 0;
							character->trickRoll           = 0.0f;
						}
					}

					// スプライン評価 → ワールド Transform 書き出し。
					const stage::CourseSpline::Frame frame = stageData->spline.Evaluate(character->distance);
					tc->position = frame.position + frame.right * character->lateral + frame.up * character->height;
					tc->rotation = frame.ToRotation();

					// トリック中だけ進行方向軸まわりの回転を重ねる。
					// operator* は local * parent なので、路面姿勢を先に適用し、
					// ワールド空間の tangent 軸まわりの回転を親側 (後ろ) に掛ける。
					if (character->trickRoll != 0.0f) {
						aq::math::Quaternion roll;
						roll.SetRotation(frame.tangent, character->trickRoll);
						tc->rotation = tc->rotation * roll;
					}

					// ループ頂点付近 (路面 up が下向き) で速度が足りなければ、遠心力で
					// 張り付いていられず重力に負けて剥がれる、という表現。
					// 剥がれた地点の進行方向をそのまま初速にして自由落下へ移す。
					if (character->grounded && frame.up.y < INVERTED_UP_Y && character->speed < LOOP_MIN_SPEED) {
						character->fallen        = true;
						character->grounded      = false;
						character->worldVelocity = frame.tangent * character->speed;
					}

					UpdateCharacterAnimation(ctx, handle, character);
				});
		}
	}
}
