#include "stdafx.h"
#include "CoinComponentSystem.h"
#include "GameFlow.h"
#include "GameInput.h"
#include "SpeedCharacterComponentSystem.h"
#include "Component/InstancedPointListComponentSystem.h"
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

			// 寸法 (外径 0.9m) は CoinRing メッシュへ焼き込み済みなので、
			// インスタンス点は等倍で置き、ここでは色だけを与える。
			/** インスタンス描画の見た目 */
			static constexpr float COIN_COLOR_R   = 1.00f;   // ゴールド
			static constexpr float COIN_COLOR_G   = 0.82f;
			static constexpr float COIN_COLOR_B   = 0.15f;

			// リング取得音風の 2 音チャイム (専用に生成した SE)。
			// メニューの決定音とは別音源なので、取得音だけが変わる。
			static const char* COLLECT_SE_PATH = "Assets/Sound/CoinGet.wav";

			/** 取得時の振動 (弾けるような短い一発) */
			static constexpr float COLLECT_RUMBLE_LEFT  = 0.5f;
			static constexpr float COLLECT_RUMBLE_RIGHT = 0.8f;
			static constexpr float COLLECT_RUMBLE_SEC   = 0.12f;


			// 取得 SE を鳴らす (サウンド無効環境では何もしない)。
			void PlayCollectSE()
			{
				if (aq::sound::SoundEngine::IsAvailable()) {
					auto clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(COLLECT_SE_PATH);
					aq::sound::SoundEngine::Get().Play(clip, aq::sound::SoundBusId::SE);
				}
			}


			// 取得の手応えを短い振動で返す。ここはワーカースレッドなので値を積むだけで、
			// 実際の送信と自動停止はメインスレッドの入力更新が行う。
			void PlayCollectRumble()
			{
				GameInput::Get().Rumble(COLLECT_RUMBLE_LEFT, COLLECT_RUMBLE_RIGHT, COLLECT_RUMBLE_SEC);
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


			// 未取得のコインだけを Coins エンティティのインスタンス点として積み直す。
			// 取得済みは積まれないので、取得の見た目はこの再構築だけで消える。
			void RebuildCoinInstances(const aquadash::GameContext& context)
			{
				auto& ctx = aq::ecs::EntityContext::Get();
				const aq::ecs::EntityHandle& handle = context.coinInstancesHandle;
				if (!ctx.IsValid(handle)) { return; }

				auto* pointList = ctx.GetComponent<aq::ecs::InstancedPointListComponent>(handle);
				if (!pointList) { return; }

				pointList->ClearInstancePoints();

				aq::ecs::Foreach<CoinComponent>([&ctx, pointList](const aq::ecs::Entity& entity, CoinComponent* coin)
					{
						if (coin->collected) { return; }

						auto* tc = ctx.GetComponent<aq::ecs::TransformComponent>(entity.GetHandle());
						if (!tc) { return; }

						// operator* は local * parent 合成。路面姿勢の上に Y 軸スピンを載せる。
						aq::math::Quaternion spin;
						spin.SetRotation(aq::math::Vector3(0.0f, 1.0f, 0.0f), coin->spinPhase);

						aq::ecs::InstancePoint p;
						p.position = tc->position;
						p.rotation = spin * coin->baseRotation;
						p.scale.Set(1.0f, 1.0f, 1.0f);
						p.color = aq::math::Vector4(COIN_COLOR_R, COIN_COLOR_G, COIN_COLOR_B, 1.0f);
						pointList->AddInstancePoint(p);
					});
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

			// 回転演出。位相だけを進め、姿勢への反映は末尾の再構築が行う。
			// 取得済みは描画されないので更新しない。
			aq::ecs::Foreach<CoinComponent>([dt](const aq::ecs::Entity&, CoinComponent* coin)
				{
					if (coin->collected) { return; }

					// 位相は 1 周ごとに巻き戻す (長時間プレイで float の精度が落ちるため)。
					coin->spinPhase += SPIN_SPEED * dt;
					if (coin->spinPhase > TWO_PI) { coin->spinPhase -= TWO_PI; }
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

							// 取得。破棄はせずフラグだけ立てる (ReactivateAll で戻せるようにするため)。
							// 見た目は末尾の再構築でインスタンスから外れて消える。
							coin->collected = true;
							score->coinCount++;

							PlayCollectSE();
							PlayCollectEffect(context, coinTc->position);
							PlayCollectRumble();
						});
				});

			// 位相と取得結果を反映したインスタンス点へ組み直す。
			RebuildCoinInstances(context);
		}


		void CoinSystem::ReactivateAll()
		{
			aq::ecs::Foreach<CoinComponent>([](const aq::ecs::Entity&, CoinComponent* coin)
				{
					// spinPhase は維持する (復活時に回転が飛ばないように)。
					coin->collected = false;
				});

			// リザルト中 (Update が止まっている間) の呼び出しでも見た目が戻るように即時再構築する。
			RebuildCoinInstances(GameFlow::Get().Context());
		}
	}
}
