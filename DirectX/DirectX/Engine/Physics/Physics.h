#pragma once

/**
 * 物理バックエンドの選択。
 * 切り替えたい場合はここのコメントを変えるだけでよい。
 *
 *   #define USE_BULLET_PHYSICS   → BulletPhysics (現在)
 *   #define USE_PHYSX            → NVIDIA PhysX   (将来対応予定)
 */
#define USE_BULLET_PHYSICS


#if defined(USE_BULLET_PHYSICS)

#include "BulletPhysics.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "CapsuleCollider.h"
#include "PhysicsBody.h"
#include "GhostPrimitive.h"
#include "IBroadphase.h"
#include "IGhostNarrowphase.h"
#include "BulletDbvtBroadphase.h"
#include "BulletGhostNarrowphase.h"
#include "GhostBody.h"
#include "GhostBodyManager.h"
#include "CharacterController.h"


namespace engine
{
	namespace physics
	{
		/**
		 * バックエンドの型エイリアス。
		 * ユーザーコードはこれらの名前だけを使い、Bullet / PhysX 固有の名前を直接書かない。
		 * バックエンドを切り替える際は上の #define と以下の using を変えるだけ。
		 */
		using PhysicsWorld    = BulletPhysicsWorld;
		using RigidBody       = BulletRigidBody;

		using BoxCollider     = BulletBoxCollider;
		using SphereCollider  = BulletSphereCollider;
		using CapsuleCollider = BulletCapsuleCollider;

		// GhostBody 関連は各クラスをそのまま使う（エンジン内に単一実装のため alias 不要）
	}
}

#endif // USE_BULLET_PHYSICS
