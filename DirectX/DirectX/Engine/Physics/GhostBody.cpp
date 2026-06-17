#include "aq.h"
#include "GhostBody.h"
#include "GhostBodyManager.h"


namespace aq
{
	namespace physics
	{
		GhostBody::GhostBody()
			: position_(math::Vector3::Zero)
			, rotation_(math::Quaternion::Identity)
		{
		}


		GhostBody::~GhostBody()
		{
			if (GhostBodyManager::IsInitialized()) {
				GhostBodyManager::Get().RemoveBody(this);
			}
		}


		void GhostBody::CreateSphere(void* owner, uint32_t id, float radius, uint32_t attr, uint32_t mask)
		{
			shape_ = std::make_unique<GhostSphere>(radius);
			CreateCore(owner, id, attr, mask);
		}


		void GhostBody::CreateBox(void* owner, uint32_t id, const math::Vector3& halfExtents, uint32_t attr, uint32_t mask)
		{
			shape_ = std::make_unique<GhostBox>(halfExtents);
			CreateCore(owner, id, attr, mask);
		}


		void GhostBody::CreateCapsule(void* owner, uint32_t id, float radius, float height, uint32_t attr, uint32_t mask)
		{
			shape_ = std::make_unique<GhostCapsule>(radius, height);
			CreateCore(owner, id, attr, mask);
		}


		void GhostBody::CreateCore(void* owner, uint32_t id, uint32_t attr, uint32_t mask)
		{
			owner_     = owner;
			ownerId_   = id;
			attribute_ = attr;
			mask_      = mask;
			if (GhostBodyManager::IsInitialized()) {
				GhostBodyManager::Get().AddBody(this);
			}
		}


		void GhostBody::SetPosition(const math::Vector3& pos)
		{
			position_ = pos;
			isDirty_  = true;
		}


		void GhostBody::SetRotation(const math::Quaternion& rot)
		{
			rotation_ = rot;
			isDirty_  = true;
		}


		void GhostBody::ComputeAabb(math::Vector3& aabbMin, math::Vector3& aabbMax) const
		{
			if (shape_) {
				shape_->GetAabb(position_, rotation_, aabbMin, aabbMax);
			}
		}
	}
}
