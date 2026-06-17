#pragma once

/**
 * 物理システムの全部入りインクルード。
 * バックエンドの選択・切り替えは PhysicsBackend.h で行う。
 */
#include "PhysicsBackend.h"
#include "PhysicsBody.h"
#include "GhostPrimitive.h"
#include "IBroadphase.h"
#include "IGhostNarrowphase.h"
#include "BulletDbvtBroadphase.h"
#include "BulletGhostNarrowphase.h"
#include "GhostBody.h"
#include "GhostBodyManager.h"
#include "CharacterController.h"
