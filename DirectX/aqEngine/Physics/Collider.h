#pragma once

class btCollisionShape; // Bullet headers は実装側でインクルード

namespace aq
{
	namespace physics
	{
		// バックエンドの形状型エイリアス
		// PhysX に切り替える場合は PxShape に変更する
		using CollisionShape = btCollisionShape;


		/**
		 * コライダーの基底インターフェース。
		 * 各コライダー (Box/Sphere/Capsule 等) が継承する。
		 */
		class ICollider
		{
		public:
			virtual ~ICollider() = default;
			virtual CollisionShape* GetShape() const = 0;
		};
	}
}
