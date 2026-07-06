#include "aq.h"
#include "SoundSystem.h"
#include "AudioListenerComponent.h"
#include "AudioSourceComponent.h"
#include "AudioEventEmitterComponent.h"
#include "ECS/EntityContext.h"
#include "Sound/SoundEngine.h"
#include "Sound/SoundSource.h"
#include "Sound/SoundClip.h"
#include "Sound/Authoring/Audio.h"
#include "Sound/Authoring/AudioDirector.h"


namespace aq
{
	namespace sound
	{
		namespace
		{
			// クォータニオンから基底ベクトルを回転して取り出す。
			math::Vector3 RotateBasis(const math::Quaternion& q, float bx, float by, float bz)
			{
				const DirectX::XMVECTOR xq = DirectX::XMLoadFloat4(&q.vector);
				const DirectX::XMVECTOR v  = DirectX::XMVector3Rotate(DirectX::XMVectorSet(bx, by, bz, 0.0f), xq);
				math::Vector3 result;
				DirectX::XMStoreFloat3(&result.vector, v);
				return result;
			}

			// (current - previous) / dt で速度を推定する。
			math::Vector3 EstimateVelocity(const math::Vector3& current, const math::Vector3& previous, bool hasPrevious, float dt)
			{
				if (!hasPrevious || dt <= 0.0f) {
					return math::Vector3::Zero;
				}
				math::Vector3 velocity = current - previous;
				velocity.Scale(1.0f / dt);
				return velocity;
			}
		}


		void SoundSystem::Update()
		{
			if (!SoundEngine::IsAvailable()) {
				return;
			}
			SoundEngine& sound = SoundEngine::Get();
			ecs::EntityContext& ctx = ecs::EntityContext::Get();
			const float dt = Engine::GetDeltaTime();

			// ── リスナー（最後に見つかったものを採用）──
			ctx.GetView<ecs::HierarchicalTransformComponent, AudioListenerComponent>().ForEach(
				[&](ecs::Entity, ecs::HierarchicalTransformComponent* htc, AudioListenerComponent* listener)
				{
					const math::Vector3& position = htc->transform.position;
					const math::Vector3  forward   = RotateBasis(htc->transform.rotation, 0.0f, 0.0f, 1.0f);
					const math::Vector3  up        = RotateBasis(htc->transform.rotation, 0.0f, 1.0f, 0.0f);
					const math::Vector3  velocity  = EstimateVelocity(position, listener->previousPosition, listener->hasPrevious, dt);

					sound.GetListener().SetPosition(position);
					sound.GetListener().SetOrientation(forward, up);
					sound.GetListener().SetVelocity(velocity);

					listener->previousPosition = position;
					listener->hasPrevious      = true;
				});

			// ── 発音体 ──
			ctx.GetView<ecs::HierarchicalTransformComponent, AudioSourceComponent>().ForEach(
				[&](ecs::Entity, ecs::HierarchicalTransformComponent* htc, AudioSourceComponent* source)
				{
					const math::Vector3& position = htc->transform.position;

					// 遅延初期化: clip ロード完了時に SoundSource を生成する。
					if (!source->initialized && source->clip && source->clip->IsCompleted()) {
						source->handle = sound.CreateSource(source->clip, source->bus);
						if (SoundSource* src = sound.Resolve(source->handle)) {
							src->SetAttenuation(source->attenuation);
							src->SetDistances(source->minDistance, source->maxDistance);
							src->SetDopplerFactor(source->dopplerFactor);
							src->SetPitch(source->pitch);
							src->SetVolume(source->volume);
							src->SetPosition(position);
							if (source->autoPlay) {
								const LoopRegion loop = source->loop ? LoopRegion{ 0, 1, 0 } : LoopRegion{};
								src->Play(loop);
							}
						}
						source->initialized      = true;
						source->previousPosition = position;
						source->hasPrevious      = true;
						return;
					}

					if (!source->initialized) {
						return;
					}

					if (SoundSource* src = sound.Resolve(source->handle)) {
						const math::Vector3 velocity = EstimateVelocity(position, source->previousPosition, source->hasPrevious, dt);
						src->SetPosition(position);
						src->SetVelocity(velocity);
						// インスペクタ等での実行時変更を反映する。
						src->SetVolume(source->volume);
						src->SetPitch(source->pitch);
					}

					source->previousPosition = position;
					source->hasPrevious      = true;
				});

			// ── オーディオイベント発生主体（データ駆動層の GameObject）──
			ctx.GetView<ecs::HierarchicalTransformComponent, audio::AudioEventEmitterComponent>().ForEach(
				[&](ecs::Entity entity, ecs::HierarchicalTransformComponent* htc, audio::AudioEventEmitterComponent* emitter)
				{
					const uint64_t        go       = static_cast<uint64_t>(entity.GetID());
					const math::Vector3&  position = htc->transform.position;
					const math::Vector3   forward  = RotateBasis(htc->transform.rotation, 0.0f, 0.0f, 1.0f);
					const math::Vector3   up       = RotateBasis(htc->transform.rotation, 0.0f, 1.0f, 0.0f);
					const math::Vector3   velocity = EstimateVelocity(position, emitter->previousPosition, emitter->hasPrevious, dt);

					emitter->goId = go;
					audio::SetGameObjectTransform(go, position, forward, up, velocity);

					if (emitter->autoPlay && !emitter->started && emitter->autoPlayEventId != 0
						&& audio::AudioDirector::IsAvailable()) {
						audio::AudioDirector::Get().PostEvent(emitter->autoPlayEventId, go);
						emitter->started = true;
					}

					emitter->previousPosition = position;
					emitter->hasPrevious      = true;
				});
		}
	}
}
