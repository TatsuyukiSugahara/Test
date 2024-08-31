#pragma once
#include "Physics.h"

namespace engine
{
	namespace physics
	{
		/**
		 * �R���C�_�[�̃C���^�[�t�F�[�X
		 */
		class ICollider
		{
		public:
			virtual CollisionShape* GetBody() = 0;
		};
	}
}