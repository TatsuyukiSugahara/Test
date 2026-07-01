#pragma once
#include "ECS/ECS.h"
#include "Math/Vector.h"
#include "Sound/SoundTypes.h"
#include "Sound/SoundHandle.h"
#include "Sound/SoundFwd.h"
#include <string>


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
			std::string      clipPath;                              // ★正本（serialize される）。OnDeserialized で clip をロード
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

			// 永続フィールド（JSON 保存/読込）。常時コンパイル。
			// bus/attenuation は enum を int で保存（temp+apply）。clip のロードは OnDeserialized へ退避。
			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.FieldPath("clip", clipPath, "Clip");
				int busI = static_cast<int>(bus);
				visitor.Field("bus", busI, "Bus");
				bus = static_cast<SoundBusId>(busI);
				int attnI = static_cast<int>(attenuation);
				visitor.Field("attenuation", attnI, "Attenuation");
				attenuation = static_cast<AttenuationModel>(attnI);
				visitor.Field("minDistance",   minDistance,   "Min Distance");
				visitor.Field("maxDistance",   maxDistance,   "Max Distance");
				visitor.Field("dopplerFactor", dopplerFactor, "Doppler");
				visitor.Field("pitch",         pitch,         "Pitch");
				visitor.Field("volume",        volume,        "Volume");
				visitor.Field("loop",          loop,          "Loop");
				visitor.Field("autoPlay",      autoPlay,      "Auto Play");
			}

			// deserialize 後に呼ぶ。clipPath からクリップをロードする（.cpp で実装）。
			void OnDeserialized();

		private:
			// handle の所有を other から奪い、other を無効化する（dtor の二重破棄を防ぐ）。
			void MoveFrom(AudioSourceComponent&& other) noexcept;
			// 自身が握る SoundSource をエンジンから破棄する。
			void DestroyOwned() noexcept;
		};
	}
}
