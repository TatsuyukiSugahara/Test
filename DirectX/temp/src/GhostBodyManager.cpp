#include "stdafx.h"
#include "GhostBodyManager.h"
#include "BroadphaseImpl.h"
#include "CollisionHitManager.h"

// Bulletの詳細判定用
#include "BulletCollision/NarrowPhaseCollision/btGjkPairDetector.h"
#include "BulletCollision/NarrowPhaseCollision/btPointCollector.h"


GhostBodyManager* GhostBodyManager::m_instance = nullptr;


GhostBodyManager::GhostBodyManager()
{
	// ここでアルゴリズムを決定（Gridなどへの差し替えはここを変更するだけ）
	broadphase_.reset(new BulletDbvtBroadphase());
}


GhostBodyManager::~GhostBodyManager()
{
	for (auto* body : bodyList_) {
		if (body && body->GetBulletObject()) {
			PhysicsWorld::Get().RemoveCollisionObject(*body->GetBulletObject());
		}
	}
	broadphase_.reset();
	bodyList_.clear();
}


void GhostBodyManager::AddBody(GhostBody* body)
{
	if (body == nullptr || body->GetBulletObject() == nullptr) {
		return;
	}

	if (std::find(bodyList_.begin(), bodyList_.end(), body) != bodyList_.end()) {
		return; // 既に登録済み
	}

	bodyList_.push_back(body);
	broadphase_->Add(body);
	PhysicsWorld::Get().AddCollisionObject(*body->GetBulletObject());
}

void GhostBodyManager::RemoveBody(GhostBody* body)
{
	if (body == nullptr || body->GetBulletObject() == nullptr) {
		return;
	}

	auto it = std::find(bodyList_.begin(), bodyList_.end(), body);
	if (it == bodyList_.end()) {
		return; // 未登録
	}

	bodyList_.erase(it);
	broadphase_->Remove(body);
	PhysicsWorld::Get().RemoveCollisionObject(*body->GetBulletObject());

	if (CollisionHitManager::IsAvailable()) {
		CollisionHitManager::Get().OnBodyRemoved(body);
	}
}

void GhostBodyManager::Update()
{
	// 1. 移動したオブジェクトのBroadphase構造更新
	for (auto* body : bodyList_) {
		if (body->IsDirty()) {
			broadphase_->Update(body);
			body->ClearDirty();
		}
	}

	// 2. Broadphase実行 -> 衝突候補があればコールバック
	broadphase_->Perform([this](GhostBody* a, GhostBody* b)
		{
			this->ProcessCollisionPair(a, b);
		});
}

void GhostBodyManager::ProcessCollisionPair(GhostBody* a, GhostBody* b)
{
	// 基本チェック
	if (a == b) return;
	if (!a->IsActive() || !b->IsActive()) return;

	// 属性フィルタリング
	if (!((a->GetMask() & b->GetAttribute()) && (b->GetMask() & a->GetAttribute()))) {
		return;
	}

	// ★最適化: 形状タイプID順に並べ替える (Sphere:0, Capsule:1, Box:2)
	// これにより (Sphere vs Box) と (Box vs Sphere) の重複実装を防ぐ
	if (a->GetShapeType() > b->GetShapeType()) {
		std::swap(a, b);
	}

	// ★最適化: 包含球 (Bounding Sphere) チェック
	// 複雑な判定をする前に距離で弾く
	float rA = a->GetBoundingRadius();
	float rB = b->GetBoundingRadius();
	float distSq = (a->GetPosition() - b->GetPosition()).LengthSq();
	float sumR = rA + rB;

	if (distSq > sumR * sumR) {
		return; // 届かない
	}

	// --- 詳細判定 (Narrowphase) ---
	bool isHit = false;
	GhostShapeType typeA = a->GetShapeType();
	GhostShapeType typeB = b->GetShapeType();

	// Case: Sphere vs Sphere (最小)
	if (typeA == GhostShapeType::Sphere && typeB == GhostShapeType::Sphere) {
		// Bounding Sphere判定で既に当たっていることが確定している
		isHit = true;
	}
	// Case: その他 (Box同士, Capsule絡みなど)
	else {
		// 複雑な形状はBulletのDispatcherに任せる
		isHit = CheckCollisionBullet(a, b);
	}

	if (isHit) {
		if (registerPairCallback_) {
			registerPairCallback_(a, b);
		}
	}
}

bool GhostBodyManager::CheckCollisionBullet(GhostBody* a, GhostBody* b)
{
	const btCollisionObject* objA = a->GetBulletObject();
	const btCollisionObject* objB = b->GetBulletObject();

	if (!objA || !objB) return false;

	// BulletのAlgorithm探索 (GJK/EPAなど適切なものを探す)
	auto* dispatcher = PhysicsWorld::Get().GetCollisionDispatcher();
	auto& dispatchInfo = PhysicsWorld::Get().GetDispatchInfo();

	// btCollisionObjectWrapperを作成
	const btCollisionShape* shapeA = objA->getCollisionShape();
	const btCollisionShape* shapeB = objB->getCollisionShape();
	const btTransform& transA = objA->getWorldTransform();
	const btTransform& transB = objB->getWorldTransform();

	btCollisionObjectWrapper objWrapA(nullptr, shapeA, objA, transA, -1, -1);
	btCollisionObjectWrapper objWrapB(nullptr, shapeB, objB, transB, -1, -1);

	// アルゴリズム生成
	btCollisionAlgorithm* algorithm = dispatcher->findAlgorithm(
		&objWrapA, &objWrapB,
		nullptr
	);

	bool hasContact = false;
	if (algorithm) {
		btManifoldResult contactPointResult(&objWrapA, &objWrapB);
		algorithm->processCollision(&objWrapA, &objWrapB, dispatchInfo, &contactPointResult);

		// マニフォールドに接触点が1つ以上あればヒット
		if (contactPointResult.getPersistentManifold() && contactPointResult.getPersistentManifold()->getNumContacts() > 0) {
			hasContact = true;
		}

		algorithm->~btCollisionAlgorithm();
		dispatcher->freeCollisionAlgorithm(algorithm);
	}

	return hasContact;
}


