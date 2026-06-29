#pragma once
#include "ECS/ECS.h"
#include "Math/Vector.h"
#include "Sound/SoundTypes.h"
#include "Sound/SoundHandle.h"
#include "Sound/SoundFwd.h"


namespace aq
{
	namespace sound
	{
		// 3D 発音体をエンティティに紐付けるコンポーネント（§9）。
		// 実体（SoundSource）はエンジンがプール所有し、本コンポーネントは
		// 世代付き SoundSourceHandle のみ保持する（Medium #6）。
		// 破棄/移動時に handle を安全に扱うため、明示的なムーブ/デストラクタを持つ。
		struct AudioSourceComponent : public ecs::IComponent
		{
			ecsComponent(aq::sound::AudioSourceComponent);

			// ── 設定（生成前に指定）──
			RefSoundClip     clip;                                  // 事前ロード済みクリップ
			SoundBusId       bus           = SoundBusId::SE;
			AttenuationModel attenuation   = AttenuationModel::Inverse;
			float            minDistance   = 1.0f;
			float            maxDistance   = 1000.0f;
			float            dopplerFactor = 1.0f;
			float            pitch         = 1.0f;
			float            volume        = 1.0f;
			bool             loop          = false;
			bool             autoPlay      = true;   // clip ロード完了時に自動再生

			// ── ランタイム（SoundSystem が管理。直接触らない）──
			SoundSourceHandle handle;
			bool              initialized      = false;
			math::Vector3     previousPosition = math::Vector3::Zero;
			bool              hasPrevious      = false;

			// ECS は default 構築 + move-construct + destruct を行う（コピーはしない）。
			AudioSourceComponent() = default;
			AudioSourceComponent(const AudioSourceComponent&) = delete;
			AudioSourceComponent& operator=(const AudioSourceComponent&) = delete;
			AudioSourceComponent(AudioSourceComponent&& other) noexcept;
			AudioSourceComponent& operator=(AudioSourceComponent&& other) noexcept;
			~AudioSourceComponent();

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				visitor.Field("volume", volume);
				visitor.Field("pitch", pitch);
				visitor.Field("minDistance", minDistance);
				visitor.Field("maxDistance", maxDistance);
				visitor.Field("loop", loop);
			}
#endif

		private:
			// handle の所有を other から奪い、other を無効化する（dtor の二重破棄を防ぐ）。
			void MoveFrom(AudioSourceComponent&& other) noexcept;
			// 自身が握る SoundSource をエンジンから破棄する。
			void DestroyOwned() noexcept;
		};
	}
}
