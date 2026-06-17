#pragma once
#include "IGhostNarrowphase.h"
#include "GhostPrimitive.h"
#include "btBulletCollisionCommon.h"
#include "BulletCollision/CollisionDispatch/btGhostObject.h"
#include <unordered_map>
#include <memory>


namespace aq
{
	namespace physics
	{
		/**
		 * Bullet を使ったゴーストボディのナローフェーズ実装。
		 *
		 * ・各 GhostBody に対して btGhostObject を内部で生成・管理する。
		 * ・CheckCollision() では btCollisionAlgorithm を直接呼び出して正確な判定を行う。
		 * ・GhostBody 自身は Bullet の型を一切持たない。
		 */
		class BulletGhostNarrowphase : public IGhostNarrowphase
		{
		private:
			struct Entry
			{
				std::unique_ptr<btCollisionShape> shape;
				std::unique_ptr<btGhostObject>    ghostObject;
			};

			std::unordered_map<GhostBody*, Entry> entries_;


		public:
			BulletGhostNarrowphase()  = default;
			~BulletGhostNarrowphase() override;

			BulletGhostNarrowphase(const BulletGhostNarrowphase&) = delete;
			BulletGhostNarrowphase& operator=(const BulletGhostNarrowphase&) = delete;


			void OnBodyAdded  (GhostBody* body) override;
			void OnBodyRemoved(GhostBody* body) override;
			bool CheckCollision(GhostBody* a, GhostBody* b) override;


		private:
			static btCollisionShape* CreateBulletShape(const IGhostShape& shape);
		};
	}
}
