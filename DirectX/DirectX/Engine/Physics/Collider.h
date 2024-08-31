#pragma once
#include "Physics.h"

namespace engine
{
	namespace physics
	{
		/**
		 * コライダーのインターフェース
		 */
		class ICollider
		{
		public:
			virtual CollisionShape* GetBody() = 0;
		};
	}
}