#pragma once
#include "IBroadphase.h"
#include "GhostBody.h"
#include "btBulletCollisionCommon.h"


namespace engine
{
	namespace physics
	{
		/**
		 * Bullet の Dynamic AABB Tree (btDbvt) を使った Broadphase 実装。
		 * GhostBody の AABB 計算はエンジン数学型で行い、
		 * btDbvt へ渡す際にここで btVector3 に変換する。
		 *
		 * 別アルゴリズムに差し替えたい場合は IBroadphase を継承し
		 * GhostBodyManager::SetBroadphase() に渡す。
		 */
		class BulletDbvtBroadphase : public IBroadphase
		{
		private:
			struct DbvtCollideCallback : btDbvt::ICollide
			{
				PairCallback userCallback;

				void Process(const btDbvtNode* n1, const btDbvtNode* n2)
				{
					GhostBody* b1 = static_cast<GhostBody*>(n1->data);
					GhostBody* b2 = static_cast<GhostBody*>(n2->data);
					userCallback(b1, b2);
				}
			};


			btDbvt* tree_ = nullptr;


			static btDbvtVolume ComputeVolume(GhostBody* body)
			{
				math::Vector3 minV, maxV;
				body->ComputeAabb(minV, maxV);
				return btDbvtVolume::FromMM(
					btVector3(minV.x, minV.y, minV.z),
					btVector3(maxV.x, maxV.y, maxV.z));
			}


		public:
			BulletDbvtBroadphase()
			{
				tree_ = new btDbvt();
			}

			~BulletDbvtBroadphase()
			{
				delete tree_;
			}

			BulletDbvtBroadphase(const BulletDbvtBroadphase&) = delete;
			BulletDbvtBroadphase& operator=(const BulletDbvtBroadphase&) = delete;


			void Add(GhostBody* body) override
			{
				btDbvtVolume volume = ComputeVolume(body);
				btDbvtNode* node = tree_->insert(volume, body);
				body->SetBroadphaseHandle(static_cast<void*>(node));
			}

			void Remove(GhostBody* body) override
			{
				btDbvtNode* node = static_cast<btDbvtNode*>(body->GetBroadphaseHandle());
				if (node) {
					tree_->remove(node);
					body->SetBroadphaseHandle(nullptr);
				}
			}

			void Update(GhostBody* body) override
			{
				btDbvtNode* node = static_cast<btDbvtNode*>(body->GetBroadphaseHandle());
				if (node) {
					btDbvtVolume volume = ComputeVolume(body);
					tree_->update(node, volume);
				}
			}

			void Perform(PairCallback callback) override
			{
				if (tree_ && tree_->m_root) {
					DbvtCollideCallback cb;
					cb.userCallback = callback;
					tree_->collideTT(tree_->m_root, tree_->m_root, cb);
				}
			}
		};
	}
}
