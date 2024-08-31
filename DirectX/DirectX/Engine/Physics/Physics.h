#pragma once

# define USE_BULLET_PHYSICS

// Bullet用のインクルード
#if defined(USE_BULLET_PHYSICS)
#include "BulletPhysics.h"
#endif


namespace engine
{
	namespace physics
	{
		/**
		 * 物理空間
		 */
		template <typename PhysicsWorld>
		class Physics
		{
		private:
			PhysicsWorld physicsWorld_;

		private:
			Physics() {};
			~Physics()
			{
			}


		public:



			/**
			 *
			 */
		private:
			static Physics* sInstance_;

		public:
			void Initialize()
			{
				if (sInstance_ == nullptr) {
					sInstance_ = new Physics();
				}
			}
			Physics& Get()
			{
				return *sInstance_;
			}
			void Finalize()
			{
				if (sInstance_) {
					delete sInstance_;
				}
			}
		};
	}
}