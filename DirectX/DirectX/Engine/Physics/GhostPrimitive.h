#pragma once
#include "EnginePreCompile.h"


namespace engine
{
	namespace physics
	{
		enum class GhostShapeType : int
		{
			Sphere  = 0,
			Capsule = 1,
			Box     = 2,
		};


		/**
		 * ゴースト形状の基底インターフェース。
		 * Bullet / PhysX に依存しない。エンジン数学型のみ使用。
		 */
		class IGhostShape
		{
		public:
			virtual ~IGhostShape() = default;
			IGhostShape() = default;
			IGhostShape(const IGhostShape&) = delete;
			IGhostShape& operator=(const IGhostShape&) = delete;

			virtual GhostShapeType GetType()            const = 0;
			virtual float          GetBoundingRadius()  const = 0;

			/**
			 * ワールド AABB を計算する。
			 * Broadphase が大まかな候補絞り込みに使う（保守的 AABB でよい）。
			 */
			virtual void GetAabb(const math::Vector3& pos, const math::Quaternion& rot,
			                     math::Vector3& aabbMin, math::Vector3& aabbMax) const = 0;
		};


		/** 球 */
		class GhostSphere : public IGhostShape
		{
		public:
			float radius;
			explicit GhostSphere(float r) : radius(r) {}

			GhostShapeType GetType()           const override { return GhostShapeType::Sphere; }
			float          GetBoundingRadius() const override { return radius; }

			void GetAabb(const math::Vector3& pos, const math::Quaternion&,
			             math::Vector3& aabbMin, math::Vector3& aabbMax) const override
			{
				aabbMin = math::Vector3(pos.x - radius, pos.y - radius, pos.z - radius);
				aabbMax = math::Vector3(pos.x + radius, pos.y + radius, pos.z + radius);
			}
		};


		/** カプセル (Y-Up) */
		class GhostCapsule : public IGhostShape
		{
		public:
			float radius;
			float height;
			GhostCapsule(float r, float h) : radius(r), height(h) {}

			GhostShapeType GetType()           const override { return GhostShapeType::Capsule; }
			float          GetBoundingRadius() const override { return (height * 0.5f) + radius; }

			/** 包含球でのAABB（保守的だが回転に対して常に正しい） */
			void GetAabb(const math::Vector3& pos, const math::Quaternion&,
			             math::Vector3& aabbMin, math::Vector3& aabbMax) const override
			{
				float r = GetBoundingRadius();
				aabbMin = math::Vector3(pos.x - r, pos.y - r, pos.z - r);
				aabbMax = math::Vector3(pos.x + r, pos.y + r, pos.z + r);
			}
		};


		/** ボックス */
		class GhostBox : public IGhostShape
		{
		public:
			math::Vector3 halfExtents;
			explicit GhostBox(const math::Vector3& h) : halfExtents(h) {}

			GhostShapeType GetType()           const override { return GhostShapeType::Box; }
			float          GetBoundingRadius() const override { return halfExtents.Length(); }

			/** 包含球でのAABB（回転対応は保守的だが常に正しい） */
			void GetAabb(const math::Vector3& pos, const math::Quaternion&,
			             math::Vector3& aabbMin, math::Vector3& aabbMax) const override
			{
				float r = GetBoundingRadius();
				aabbMin = math::Vector3(pos.x - r, pos.y - r, pos.z - r);
				aabbMax = math::Vector3(pos.x + r, pos.y + r, pos.z + r);
			}
		};
	}
}
