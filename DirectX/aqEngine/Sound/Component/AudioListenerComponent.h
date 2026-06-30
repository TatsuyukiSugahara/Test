#pragma once
#include "ECS/ECS.h"
#include "Math/Vector.h"


namespace aq
{
	namespace sound
	{
		// 3D リスナーをエンティティに紐付けるコンポーネント（§9）。
		// 通常はカメラエンティティに付与し、SoundSystem がワールド変換を
		// SoundEngine の SoundListener へ反映する。
		struct AudioListenerComponent : public ecs::IComponent
		{
			ecsComponent(aq::sound::AudioListenerComponent);

			// 速度推定用（前フレームのワールド位置）。SoundSystem が更新する。
			math::Vector3 previousPosition = math::Vector3::Zero;
			bool          hasPrevious      = false;

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				visitor.ReadOnly("prev position", previousPosition);
			}
#endif
		};
	}
}
