#include "CharacterController.h"
#include <cfloat>
#include <cmath>
#include <cassert>


namespace engine
{
	namespace physics
	{
		namespace
		{
			constexpr float kWallAngleThreshold = 3.14159265f * 0.3f;  // ~54°以上を壁と判定


			/**
			 * 壁スイープ用コールバック。
			 * ゴーストオブジェクトと自分自身は無視し、
			 * 上方向との角度が閾値を超える面のみを壁として記録する。
			 */
			struct SweepResultWall : public btCollisionWorld::ConvexResultCallback
			{
				bool          isHit   = false;
				math::Vector3 hitPos;
				math::Vector3 startPos;
				math::Vector3 hitNormal;
				btCollisionObject* me = nullptr;
				float dist = FLT_MAX;

				btScalar addSingleResult(
					btCollisionWorld::LocalConvexResult& convexResult,
					bool /*normalInWorldSpace*/) override
				{
					if (convexResult.m_hitCollisionObject == me ||
						convexResult.m_hitCollisionObject->getInternalType() == btCollisionObject::CO_GHOST_OBJECT)
					{
						return 1.0f;
					}

					math::Vector3 hitNormalTmp(
						convexResult.m_hitNormalLocal.x(),
						convexResult.m_hitNormalLocal.y(),
						convexResult.m_hitNormalLocal.z());

					float angle = fabsf(acosf(
						hitNormalTmp.y < -1.0f ? -1.0f : hitNormalTmp.y > 1.0f ? 1.0f : hitNormalTmp.y));

					if (angle >= kWallAngleThreshold) {
						math::Vector3 hitPosTmp(
							convexResult.m_hitPointLocal.x(),
							convexResult.m_hitPointLocal.y(),
							convexResult.m_hitPointLocal.z());

						math::Vector3 vDist = hitPosTmp - startPos;
						vDist.y = 0.0f;
						float distTmp = vDist.Length();
						if (distTmp < dist) {
							hitPos    = hitPosTmp;
							dist      = distTmp;
							hitNormal = hitNormalTmp;
							isHit     = true;
						}
					}
					return 0.0f;
				}
			};
		}


		CharacterController::CharacterController()
			: position_(math::Vector3::Zero)
			, prevPosition_(math::Vector3::Zero)
		{
		}


		CharacterController::~CharacterController()
		{
			RemoveRigidBody();
		}


		void CharacterController::Init(float radius, float height, const math::Vector3& position, uint32_t collisionAttribute)
		{
			position_     = position;
			prevPosition_ = position;
			radius_ = radius;
			height_ = height;

			collider_.Create(radius, height);

			RigidBodyInitData data;
			data.collider = &collider_;
			data.mass     = 0.0f;
			rigidBody_.Create(data);

			btRigidBody* btBody = rigidBody_.GetBody();
			btTransform& trans  = btBody->getWorldTransform();
			trans.setOrigin(btVector3(
				position.x,
				position.y + height_ * 0.5f + radius_,
				position.z));

			btBody->setUserIndex(static_cast<int>(collisionAttribute));
			btBody->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

			BulletPhysicsWorld::Get().AddRigidBody(rigidBody_);
			isInited_ = true;
		}


		const math::Vector3& CharacterController::Execute(const math::Vector3& targetPosition, float deltaTime)
		{
			prevPosition_ = position_;

			if (isRequestTeleport_) {
				position_          = targetPosition;
				verticalVelocity_  = 0.0f;
				isRequestTeleport_ = false;
			} else {
				verticalVelocity_ += gravity_ * deltaTime;

				math::Vector3 nextPosition   = position_;
				math::Vector3 intendedXZPos  = targetPosition;
				intendedXZPos.y = position_.y;  // Y は重力側で管理

				// XZ 壁衝突解決ループ（最大 5 回）
				{
					int loopCount = 0;
					math::Vector3 currentIterPos = position_;

					while (true) {
						math::Vector3 moveDir = intendedXZPos - currentIterPos;
						moveDir.y = 0.0f;
						if (moveDir.Length() < FLT_EPSILON) {
							nextPosition.x = intendedXZPos.x;
							nextPosition.z = intendedXZPos.z;
							break;
						}

						// スイープの開始・終了座標（カプセル中心高さに合わせる）
						math::Vector3 posTmp = currentIterPos;
						posTmp.y += height_ * 0.5f + radius_ + height_ * 0.1f;

						math::Vector3 sweepStart(posTmp.x, posTmp.y, posTmp.z);
						math::Vector3 sweepEnd  (intendedXZPos.x, posTmp.y, intendedXZPos.z);

						SweepResultWall callback;
						callback.me       = rigidBody_.GetBody();
						callback.startPos = posTmp;

						BulletPhysicsWorld::Get().ConvexSweepTestRaw(collider_, sweepStart, sweepEnd, callback);

						if (callback.isHit) {
							// 壁法線方向の押し戻し量を計算
							math::Vector3 vT0(intendedXZPos.x, 0.0f, intendedXZPos.z);
							math::Vector3 vT1(callback.hitPos.x, 0.0f, callback.hitPos.z);
							math::Vector3 vMerikomi = vT0 - vT1;

							math::Vector3 hitNormalXZ = callback.hitNormal;
							hitNormalXZ.y = 0.0f;
							hitNormalXZ.Normalize();

							float fT0 = hitNormalXZ.Dot(vMerikomi);
							math::Vector3 vOffset = hitNormalXZ;
							vOffset.Scale(-fT0 + radius_ + 0.001f);

							intendedXZPos.Add(vOffset);

							currentIterPos   = callback.hitPos;
							currentIterPos.y = position_.y;
						} else {
							nextPosition.x = intendedXZPos.x;
							nextPosition.z = intendedXZPos.z;
							break;
						}

						if (++loopCount >= 5) break;
					}
				}

				position_.x = nextPosition.x;
				position_.z = nextPosition.z;
			}

			// Bullet 剛体の位置を最終座標に同期
			btRigidBody* btBody = rigidBody_.GetBody();
			btBody->setActivationState(DISABLE_DEACTIVATION);
			btTransform& trans = btBody->getWorldTransform();
			trans.setOrigin(btVector3(
				position_.x,
				position_.y + height_ * 0.5f + radius_,
				position_.z));

			return position_;
		}


		void CharacterController::RemoveRigidBody()
		{
			if (isInited_ && BulletPhysicsWorld::IsInitialized()) {
				BulletPhysicsWorld::Get().RemoveRigidBody(rigidBody_);
				isInited_ = false;
			}
		}
	}
}
