#pragma once
#include "BulletPhysics.h"


namespace aq
{
	namespace physics
	{
		/**
		 * カプセルコライダー。Y 軸方向に伸びる。
		 * radius: 半径
		 * height: 2 つの半球を除いた胴体部分の高さ
		 */
		class BulletCapsuleCollider : public ICollider
		{
		public:
			void Create(float radius, float height)
			{
				radius_ = radius;
				height_ = height;
				shape_  = std::make_unique<btCapsuleShape>(radius, height);
			}

			CollisionShape* GetShape() const override { return shape_.get(); }
			float GetRadius() const { return radius_; }
			float GetHeight() const { return height_; }

		private:
			std::unique_ptr<btCapsuleShape> shape_;
			float radius_ = 0.0f;
			float height_ = 0.0f;
		};
	}
}
