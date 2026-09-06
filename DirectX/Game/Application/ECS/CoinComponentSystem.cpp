#include "stdafx.h"
#include "CoinComponentSystem.h"
#include "GameFlow.h"
#include "SpeedCharacterComponentSystem.h"
#include "Component/ParticleComponentSystem.h"
#include "Sound/SoundClip.h"
#include "Sound/SoundEngine.h"


namespace app
{
	namespace ecs
	{
		namespace
		{
			/** 回転演出 */
			static constexpr float SPIN_SPEED = 4.0f;             // [rad/s]
			static constexpr float TWO_PI     = 6.28318530718f;

			/** 取得判定 */
			static constexpr float COARSE_WINDOW  = 4.0f;   // スプライン距離での粗い絞り込み幅 [m]
			static constexpr float COLLECT_RADIUS = 1.5f;   // 3D 距離での取得半径 [m]

			// 専用のコイン SE がまだ無いため、暫定で決定音を流用する。
			// 専用アセットが用意でき次第このパスを差し替える。
			static const char* COLLECT_SE_PATH = "Assets/Sound/Decision.wav";


			// 取得 SE を鳴らす (サウンド無効環境では何もしない)。
			void PlayCollectSE()
			{
				if (aq::sound::SoundEngine::IsAvailable()) {
					auto clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(COLLECT_SE_PATH);
					aq::sound::SoundEngine::Get().Play(clip, aq::sound::SoundBusId::SE);
				}
			}


			// 取得エフェクトを撃つ。エミッタは常駐エンティティなので、
			// 生成せずコイン位置へ移してから Restart する。
			void PlayCollectEffect(const aquadash::GameContext& context, const aq::math::Vector3& position)
			{
				auto& ctx = aq::ecs::EntityContext::Get();
				const aq::ecs::EntityHandle& handle = context.collectFxHandle;
				if (!ctx.IsValid(handle)) { return; }

				auto* tc      = ctx.GetComponent<aq::ecs::TransformComponent>(handle);
				auto* emitter = ctx.GetComponent<aq::ecs::ParticleEmitterComponent>(handle);
				if (!tc || !emitter) { return; }

				tc->position = position;
				emitter->Restart();
			}
		}


		/**
		 * コインの回転演出と取得判定
		 */
		void CoinSystem::Update()
		{
			auto& context = GameFlow::Get().Context();
			if (context.gameplayPaused) { return; }

			const float dt = aq::Engine::GetDeltaTime();

			// 回転演出。取得済みは非表示なので更新しない。
			aq::ecs::Foreach<CoinComponent>([dt](const aq::ecs::Entity& entity, CoinComponent* coin)
				{
					if (coin->collected) { return; }

					auto* tc = aq::ecs::EntityContext::Get().GetComponent<aq::ecs::TransformComponent>(entity.GetHandle());
					if (!tc) { return; }

					// 位相は 1 周ごとに巻き戻す (長時間プレイで float の精度が落ちるため)。
					coin->spinPhase += SPIN_SPEED * dt;
					if (coin->spinPhase > TWO_PI) { coin->spinPhase -= TWO_PI; }

					// operator* は local * parent 合成。路面姿勢の上に Y 軸スピンを載せる。
					aq::math::Quaternion spin;
					spin.SetRotation(aq::math::Vector3(0.0f, 1.0f, 0.0f), coin->spinPhase);
					tc->rotation = spin * coin->baseRotation;
				});

			// 取得判定。コイン数十枚 × プレイヤー数なので総当たりで足りる。
			// 枚数が増えたら distance でソート済みの配列を持ち、窓の範囲だけを見るように絞る。
			aq::ecs::Foreach<SpeedCharacterComponent>(
				[&context](const aq::ecs::Entity& playerEntity, SpeedCharacterComponent* character)
				{
					auto& ctx = aq::ecs::EntityContext::Get();
					const auto playerHandle = playerEntity.GetHandle();
					auto* playerTc = ctx.GetComponent<aq::ecs::TransformComponent>(playerHandle);
					auto* score    = ctx.GetComponent<PlayerScoreComponent>(playerHandle);
					if (!playerTc || !score) { return; }

					const aq::math::Vector3 playerPosition = playerTc->position;
					const float             playerDistance = character->distance;

					aq::ecs::Foreach<CoinComponent>([&](const aq::ecs::Entity& coinEntity, CoinComponent* coin)
						{
							if (coin->collected) { return; }

							// スプライン距離で粗く絞ってから 3D 距離を測る (平方根の回数を減らす)。
							const float distanceDiff = coin->distance - playerDistance;
							if (distanceDiff > COARSE_WINDOW || distanceDiff < -COARSE_WINDOW) { return; }

							auto* coinTc = ctx.GetComponent<aq::ecs::TransformComponent>(coinEntity.GetHandle());
							if (!coinTc) { return; }

							const aq::math::Vector3 diff = coinTc->position - playerPosition;
							if (diff.LengthSq() >= COLLECT_RADIUS * COLLECT_RADIUS) { return; }

							// 取得。破棄はせず表示だけ落とす (ReactivateAll で戻せるようにするため)。
							coin->collected = true;
							if (auto* mesh = ctx.GetComponent<aq::ecs::BoxStaticMeshComponent>(coinEntity.GetHandle())) {
								mesh->SetVisible(false);
							}
							score->coinCount++;

							PlayCollectSE();
							PlayCollectEffect(context, coinTc->position);
						});
				});
		}


		void CoinSystem::ReactivateAll()
		{
			auto& ctx = aq::ecs::EntityContext::Get();

			aq::ecs::Foreach<CoinComponent>([&ctx](const aq::ecs::Entity& entity, CoinComponent* coin)
				{
					// spinPhase は維持する (復活時に回転が飛ばないように)。
					coin->collected = false;

					if (auto* mesh = ctx.GetComponent<aq::ecs::BoxStaticMeshComponent>(entity.GetHandle())) {
						mesh->SetVisible(true);
					}
				});
		}
	}
}
