#include "aq.h"
#include "BulletGhostNarrowphase.h"
#include "GhostBody.h"
#include "BulletPhysics.h"
#include "btBulletCollisionCommon.h"
#include "BulletCollision/CollisionDispatch/btGhostObject.h"
#include "BulletCollision/CollisionDispatch/btCollisionWorld.h"
#include "BulletCollision/CollisionDispatch/btManifoldResult.h"
#include <cassert>


namespace aq
{
	namespace physics
	{
		BulletGhostNarrowphase::~BulletGhostNarrowphase()
		{
			// 残っているエントリをすべてPhysicsWorldから除去する
			if (BulletPhysicsWorld::IsInitialized()) {
				for (auto& [body, entry] : entries_) {
					if (entry.ghostObject) {
						BulletPhysicsWorld::Get().RemoveCollisionObject(*entry.ghostObject);
					}
				}
			}
			entries_.clear();
		}


		void BulletGhostNarrowphase::OnBodyAdded(GhostBody* body)
		{
			assert(body && body->GetShape() && "GhostBody has no shape");
			if (!body || !body->GetShape()) return;
			if (entries_.count(body)) return;  // 登録済み

			Entry entry;
			entry.shape.reset(CreateBulletShape(*body->GetShape()));

			entry.ghostObject = std::make_unique<btGhostObject>();
			entry.ghostObject->setCollisionShape(entry.shape.get());

			// 初期トランスフォーム設定
			const math::Vector3&    pos = body->GetPosition();
			const math::Quaternion& rot = body->GetRotation();
			btTransform t;
			t.setIdentity();
			t.setOrigin(btVector3(pos.x, pos.y, pos.z));
			t.setRotation(btQuaternion(rot.x, rot.y, rot.z, rot.w));
			entry.ghostObject->setWorldTransform(t);

			if (BulletPhysicsWorld::IsInitialized()) {
				BulletPhysicsWorld::Get().AddCollisionObject(*entry.ghostObject);
			}

			entries_.emplace(body, std::move(entry));
		}


		void BulletGhostNarrowphase::OnBodyRemoved(GhostBody* body)
		{
			auto it = entries_.find(body);
			if (it == entries_.end()) return;

			if (it->second.ghostObject && BulletPhysicsWorld::IsInitialized()) {
				BulletPhysicsWorld::Get().RemoveCollisionObject(*it->second.ghostObject);
			}
			entries_.erase(it);
		}


		bool BulletGhostNarrowphase::CheckCollision(GhostBody* a, GhostBody* b)
		{
			auto itA = entries_.find(a);
			auto itB = entries_.find(b);
			if (itA == entries_.end() || itB == entries_.end()) return false;

			btGhostObject* objA = itA->second.ghostObject.get();
			btGhostObject* objB = itB->second.ghostObject.get();

			// btGhostObject のトランスフォームを GhostBody の現在値に同期する
			auto syncTransform = [](btGhostObject* obj, const GhostBody* body) {
				const math::Vector3&    pos = body->GetPosition();
				const math::Quaternion& rot = body->GetRotation();
				btTransform t;
				t.setIdentity();
				t.setOrigin(btVector3(pos.x, pos.y, pos.z));
				t.setRotation(btQuaternion(rot.x, rot.y, rot.z, rot.w));
				obj->setWorldTransform(t);
			};
			syncTransform(objA, a);
			syncTransform(objB, b);

			// btCollisionAlgorithm を使って詳細判定
			auto* dispatcher   = BulletPhysicsWorld::Get().GetCollisionDispatcher();
			auto& dispatchInfo = BulletPhysicsWorld::Get().GetDispatchInfo();

			const btCollisionShape* shapeA = objA->getCollisionShape();
			const btCollisionShape* shapeB = objB->getCollisionShape();
			const btTransform&      transA = objA->getWorldTransform();
			const btTransform&      transB = objB->getWorldTransform();

			btCollisionObjectWrapper wrapA(nullptr, shapeA, objA, transA, -1, -1);
			btCollisionObjectWrapper wrapB(nullptr, shapeB, objB, transB, -1, -1);

			btCollisionAlgorithm* algorithm = dispatcher->findAlgorithm(&wrapA, &wrapB, nullptr, BT_CONTACT_POINT_ALGORITHMS);
			if (!algorithm) return false;

			bool hasContact = false;
			btManifoldResult result(&wrapA, &wrapB);
			algorithm->processCollision(&wrapA, &wrapB, dispatchInfo, &result);

			if (result.getPersistentManifold() &&
				result.getPersistentManifold()->getNumContacts() > 0)
			{
				hasContact = true;
			}

			algorithm->~btCollisionAlgorithm();
			dispatcher->freeCollisionAlgorithm(algorithm);

			return hasContact;
		}


		btCollisionShape* BulletGhostNarrowphase::CreateBulletShape(const IGhostShape& shape)
		{
			switch (shape.GetType()) {
			case GhostShapeType::Sphere: {
				const auto& s = static_cast<const GhostSphere&>(shape);
				return new btSphereShape(s.radius);
			}
			case GhostShapeType::Capsule: {
				const auto& c = static_cast<const GhostCapsule&>(shape);
				float h = c.height - 2.0f * c.radius;
				if (h < 0.0f) h = 0.0f;
				return new btCapsuleShape(c.radius, h);
			}
			case GhostShapeType::Box: {
				const auto& b = static_cast<const GhostBox&>(shape);
				return new btBoxShape(btVector3(b.halfExtents.x, b.halfExtents.y, b.halfExtents.z));
			}
			default:
				assert(false && "Unknown GhostShapeType");
				return new btSphereShape(0.5f);
			}
		}
	}
}
