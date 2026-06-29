#pragma once
#include <cstdint>
#include "ECS/ECS.h"
#include "Math/Vector.h"


namespace aq
{
	namespace audio
	{
		// エンティティをオーディオの GameObject（3D 発生主体）として登録し、
		// イベントを発火させる ECS コンポーネント（§11）。
		// SoundSystem が毎フレーム Transform を audio::SetGameObjectTransform へ流す。
		// 破棄時に GameObject 登録解除（紐づく 3D ループ音を停止）するため明示的なムーブ/dtor を持つ。
		struct AudioEventEmitterComponent : public ecs::IComponent
		{
			ecsComponent(aq::audio::AudioEventEmitterComponent);

			// ── 設定 ──
			uint32_t autoPlayEventId = 0;     // 生成時に自動発火するイベント（CRC32, 0=なし）
			bool     autoPlay        = true;

			// ── ランタイム（SoundSystem が管理）──
			uint64_t      goId             = 0;
			bool          started          = false;
			math::Vector3 previousPosition = math::Vector3::Zero;
			bool          hasPrevious      = false;

			AudioEventEmitterComponent() = default;
			AudioEventEmitterComponent(const AudioEventEmitterComponent&) = delete;
			AudioEventEmitterComponent& operator=(const AudioEventEmitterComponent&) = delete;
			AudioEventEmitterComponent(AudioEventEmitterComponent&& other) noexcept;
			AudioEventEmitterComponent& operator=(AudioEventEmitterComponent&& other) noexcept;
			~AudioEventEmitterComponent();

		private:
			void MoveFrom(AudioEventEmitterComponent&& other) noexcept;
			void ReleaseGameObject() noexcept;
		};
	}
}
