/**
 * GhostPrimitive.h
 * ゴースト用の形状関連
 */
#pragma once
#include "src/collision/Types.h"



/** 形状の種類（ID順が判定優先度に関わる） */
enum class GhostShapeType : int
{
	Sphere = 0,
	Capsule = 1,
	Box = 2,
	Num
};




/** 形状基底クラス */
class IGhostShape : public Noncopyable
{
public:
	virtual ~IGhostShape() {}
	virtual GhostShapeType GetType() const = 0;

	/** AABB取得 (Bullet Transformを使用) */
	virtual void GetAabb(const btTransform& t, btVector3& aabbMin, btVector3& aabbMax) const = 0;

	/** 包含球の半径（早期リターン用） */
	virtual float GetBoundingRadius() const = 0;

	/** 詳細判定用にBulletのShapeを作成して返す（呼び出し側で管理する） */
	virtual btCollisionShape* CreateBulletShape() const = 0;
};




/** 球 */
class GhostSphere : public IGhostShape
{
public:
	float radius;


public:
	GhostSphere(float r) : radius(r) {}

	/** 形状タイプ取得 */
	GhostShapeType GetType() const override { return GhostShapeType::Sphere; }
	
	/** 外接球半径はそのまま半径 */
	float GetBoundingRadius() const override { return radius; }

	/** AABB取得 */
	void GetAabb(const btTransform& t, btVector3& min, btVector3& max) const override
	{
		btVector3 ext(radius, radius, radius);
		min = t.getOrigin() - ext; max = t.getOrigin() + ext;
	}

	/** Bulletの形状作成 */
	btCollisionShape* CreateBulletShape() const override
	{
		return new btSphereShape(radius);
	}
};




/** カプセル (Y-Up) */
struct GhostCapsule : public IGhostShape
{
public:
	/** 半径 */
	float radius;
	/** 高さ */
	float height;


public:
	GhostCapsule(float r, float h) : radius(r), height(h) {}
	
	/** 形状タイプ取得 */
	GhostShapeType GetType() const override { return GhostShapeType::Capsule; }
	/** 簡易計算：高さの半分＋半径 */
	float GetBoundingRadius() const override { return (height * 0.5f) + radius; }

	/** AABB取得 */
	void GetAabb(const btTransform& t, btVector3& min, btVector3& max) const override
	{
		btVector3 half(radius, height * 0.5f, radius);
		btVector3 extent = t.getBasis().absolute() * half;
		// 回転してもはみ出さないように少し余裕を見る簡易計算
		extent += btVector3(radius, radius, radius);
		min = t.getOrigin() - extent; max = t.getOrigin() + extent;
	}

	/** Bulletの形状作成 */
	btCollisionShape* CreateBulletShape() const override
	{
		// Bulletのコンストラクタは (radius, cylinder_height)
		// cylinder_height = total_height - 2*radius
		float cylinderHeight = max(0.0f, height - 2.0f * radius);
		return new btCapsuleShape(radius, cylinderHeight);
	}
};




/** ボックス */
struct GhostBox : public IGhostShape
{
public:
	Vector3 halfExtents;


public:
	GhostBox(const Vector3& h) : halfExtents(h) {}
	
	/** 形状タイプ取得 */
	GhostShapeType GetType() const override { return GhostShapeType::Box; }
	/** 外接球半径 */
	float GetBoundingRadius() const override { return halfExtents.Length(); }

	/** AABB取得 */
	void GetAabb(const btTransform& t, btVector3& min, btVector3& max) const override
	{
		btVector3 h(halfExtents.x, halfExtents.y, halfExtents.z);
		btVector3 center = t.getOrigin();
		btVector3 extent = t.getBasis().absolute() * h;
		min = center - extent; max = center + extent;
	}

	/** Bulletの形状作成 */
	btCollisionShape* CreateBulletShape() const override
	{
		return new btBoxShape(btVector3(halfExtents.x, halfExtents.y, halfExtents.z));
	}
};