#pragma once

/**
 * ============================================================
 *  物理バックエンドの選択
 *  切り替えはここの #define を 1 箇所変えるだけ。
 *
 *    #define PHYSICS_BACKEND_BULLET   BulletPhysics（現在）
 *    #define PHYSICS_BACKEND_PHYSX    NVIDIA PhysX  （将来対応予定）
 * ============================================================
 */
#define PHYSICS_BACKEND_BULLET


// ---- BulletPhysics ----
#if defined(PHYSICS_BACKEND_BULLET)

#include "BulletPhysics.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "CapsuleCollider.h"

namespace aq
{
	namespace physics
	{
		using PhysicsWorld    = BulletPhysicsWorld;   ///< 物理ワールド
		using RigidBody       = BulletRigidBody;       ///< 剛体
		using BoxCollider     = BulletBoxCollider;     ///< ボックスコライダー
		using SphereCollider  = BulletSphereCollider;  ///< 球コライダー
		using CapsuleCollider = BulletCapsuleCollider; ///< カプセルコライダー
	}
}


// ---- PhysX（将来） ----
#elif defined(PHYSICS_BACKEND_PHYSX)

// #include "PhysXPhysics.h"
// ...
// namespace aq { namespace physics {
//     using PhysicsWorld    = PhysXPhysicsWorld;
//     using RigidBody       = PhysXRigidBody;
//     using BoxCollider     = PhysXBoxCollider;
//     using SphereCollider  = PhysXSphereCollider;
//     using CapsuleCollider = PhysXCapsuleCollider;
// }}

#else
#  error "物理バックエンドが未選択です。PhysicsBackend.h で PHYSICS_BACKEND_BULLET などを定義してください。"
#endif
