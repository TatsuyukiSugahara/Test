#pragma once
#include "Math/Vector.h"


namespace aq
{
	namespace sound
	{
		// 3D リスナー（§2.3）。通常はカメラに追従させる（ECS の AudioListenerComponent）。
		class SoundListener
		{
		// ── メンバ変数 ──
		private:
			math::Vector3 position_ = math::Vector3(0.0f, 0.0f, 0.0f);
			math::Vector3 forward_  = math::Vector3(0.0f, 0.0f, 1.0f);
			math::Vector3 up_       = math::Vector3(0.0f, 1.0f, 0.0f);
			math::Vector3 velocity_ = math::Vector3(0.0f, 0.0f, 0.0f);

		// ── メンバ関数 ──
		public:
			void SetPosition(const math::Vector3& position) { position_ = position; }
			void SetVelocity(const math::Vector3& velocity) { velocity_ = velocity; }
			void SetOrientation(const math::Vector3& forward, const math::Vector3& up)
			{
				forward_ = forward;
				up_      = up;
			}

			const math::Vector3& GetPosition() const { return position_; }
			const math::Vector3& GetForward()  const { return forward_; }
			const math::Vector3& GetUp()       const { return up_; }
			const math::Vector3& GetVelocity() const { return velocity_; }
		};
	}
}
