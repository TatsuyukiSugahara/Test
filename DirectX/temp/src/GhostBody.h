/**
 * GhostBody.h
 * ゴースト用の衝突体本体
 */
#pragma once
#include "GhostPrimitive.h"


class btCollisionObject;
class btCollisionShape;


class GhostBodyManager;


/**
 * ゴースト用衝突体本体
 */
class GhostBody : public Noncopyable
{
	friend class GhostBodyManager;


private:
	IGameObject* owner_ = nullptr;
	uint32_t ownerId_ = 0;
	std::unique_ptr<IGhostShape> shape_;

	/** Broadphase用ハンドル */
	void* broadphaseHandle_ = nullptr;
	bool isDirty_ = true;
	bool isActive_ = true;

	/** 属性 */
	uint32_t attribute_ = ghost::CollisionAttribute::None;
	uint32_t mask_ = ghost::CollisionAttributeMask::All;

	/** 座標 */
	Vector3 position_ = Vector3::Zero;
	Quaternion rotation_ = Quaternion::Identity;

	/** 詳細判定用BulletObject */
	std::unique_ptr<btGhostObject> bulletObject_;
	std::unique_ptr<btCollisionShape> bulletShape_;


public:
	GhostBody();
	~GhostBody();

	/** 初期化 */
	void CreateSphere(IGameObject* owner, const uint32_t id, const float radius, const uint32_t attr, const uint32_t mask);
	void CreateBox(IGameObject* owner, const uint32_t id, const Vector3& half, const uint32_t attr, const uint32_t mask);
	void CreateCapsule(IGameObject* owner, const uint32_t id, const float r, const float h, const uint32_t attr, const uint32_t mask);
	void CreateCore(IGameObject* owner, const uint32_t id, const uint32_t attr, const uint32_t mask);

	
	void SetPosition(const Vector3& pos);
	void SetRotation(const Quaternion& rot);


	const Vector3& GetPosition() const { return position_; }
	const Quaternion& GetRotation() const { return rotation_; }
	IGameObject* GetOwner() const { return owner_; }
	uint32_t GetOwnerId() const { return ownerId_; }
	uint32_t GetAttribute() const { return attribute_; }
	uint32_t GetMask() const { return mask_; }
	bool IsActive() const { return isActive_; }
	void SetActive(bool active) { isActive_ = active; }

	GhostShapeType GetShapeType() const { return shape_ ? shape_->GetType() : GhostShapeType::Sphere; }
	float GetBoundingRadius() const { return shape_ ? shape_->GetBoundingRadius() : 0.0f; }

	
	/** Broadphase連携用 */
	void SetBroadphaseHandle(void* handle) { broadphaseHandle_ = handle; }
	void* GetBroadphaseHandle() const { return broadphaseHandle_; }
	void ComputeAabb(btVector3& min, btVector3& max) const;

	bool IsDirty() const { return isDirty_; }
	void ClearDirty() { isDirty_ = false; }

	/** 詳細判定用 */
	btCollisionObject* GetBulletObject() { return bulletObject_.get(); }

private:
	void RebuildBulletObject();
	btTransform GetBtTransform() const;
};