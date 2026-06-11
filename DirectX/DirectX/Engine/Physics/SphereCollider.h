#pragma once
#include "BulletPhysics.h"


namespace engine
{
	namespace physics
	{
		/**
		 * 球体コライダー。
		 */
		class BulletSphereCollider : public ICollider
		{
		public:
			void Create(float radius)
			{
				shape_ = std::make_unique<btSphereShape>(radius);
			}

			CollisionShape* GetShape() const override { return shape_.get(); }
			float GetRadius() const { return shape_ ? static_cast<float>(shape_->getRadius()) : 0.0f; }

		private:
			std::unique_ptr<btSphereShape> shape_;
		};
	}
}
