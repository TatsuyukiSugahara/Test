#pragma once
#include "BulletPhysics.h"


namespace aq
{
	namespace physics
	{
		/**
		 * ボックスコライダー。
		 * halfExtents: 各軸の半辺長 (例: (0.5, 1.0, 0.5) → 1×2×1 の直方体)
		 */
		class BulletBoxCollider : public ICollider
		{
		public:
			void Create(const math::Vector3& halfExtents)
			{
				shape_ = std::make_unique<btBoxShape>(ToBt(halfExtents));
			}

			CollisionShape* GetShape() const override { return shape_.get(); }

		private:
			std::unique_ptr<btBoxShape> shape_;
		};
	}
}
